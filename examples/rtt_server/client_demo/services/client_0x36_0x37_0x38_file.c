/**
 * @file client_0x36_0x37_0x38_file.c
 * @brief UDS File Transfer Service (Upload/Download/Local List).
 * @details Implements client-side file operations using UDS services:
 *          - 0x38 RequestFileTransfer (AddFile/ReadFile)
 *          - 0x36 TransferData (Block-wise transfer)
 *          - 0x37 RequestTransferExit (Finalization & CRC check)
 *          Also includes a local directory listing utility.
 * @author wdfk-prog ()
 * @version 1.0
 * @date 2025-12-02
 * 
 * @copyright Copyright (c) 2025  
 * 
 * @note :
 * @par Change Log:
 * Date       Version Author      Description
 * 2025-12-02 1.0     wdfk-prog   first version
 */
#define LOG_TAG "File"

#include "../core/client.h"
#include "../core/cmd_registry.h"
#include "../core/uds_context.h"
#include "../utils/utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>

/* ==========================================================================
 * Configuration Macros
 * ========================================================================== */

/* ISO 14229-1 defined modes of operation */
#define MOOP_ADD_FILE   0x01    /**< Upload to server */
#define MOOP_READ_FILE  0x04    /**< Download from server */

/** @brief Maximum block size for file buffer (ISO-TP MTU limit). */
#define BLOCK_SIZE_BUFFER 4095

/* ==========================================================================
 * Local File System Utilities
 * ========================================================================== */

/**
 * @brief Handles the 'lls' (Local List) command.
 * @details Lists files and directories in the current local working directory,
 *          displaying size and modification time. Directories are highlighted in blue.
 */
static int handle_lls(int argc, char **argv) 
{
    DIR *d;
    struct dirent *dir;
    struct stat file_stat;
    char time_buf[64];

    (void)argc; 
    (void)argv;
    
    d = opendir(".");
    if (!d) {
        LOG_ERROR("Could not open current directory.");
        return -1;
    }

    printf("\nLocal Directory Listing:\n");
    printf("----------------------------------------------------------------\n");
    printf("%-25s | %-10s | %s\n", "Name", "Size", "Modified");
    printf("----------------------------------------------------------------\n");

    while ((dir = readdir(d)) != NULL) {
        /* Skip current and parent directory pointers */
        if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0) {
            continue;
        }

        if (stat(dir->d_name, &file_stat) == 0) {
            struct tm *tm_info = localtime(&file_stat.st_mtime);
            strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M", tm_info);

            if (S_ISDIR(file_stat.st_mode)) {
                /* Blue color for directory */
                printf("\033[1;34m%-25s\033[0m | %-10s | %s\n", dir->d_name, "<DIR>", time_buf);
            } else {
                printf("%-25s | %-10ld | %s\n", dir->d_name, (long)file_stat.st_size, time_buf);
            }
        } else {
            printf("%-25s | ?          | ?\n", dir->d_name);
        }
    }
    printf("----------------------------------------------------------------\n\n");
    closedir(d);
    return 0;
}

/* ==========================================================================
 * File Transfer Handlers
 * ========================================================================== */

/**
 * @brief Waits for a transfer-stage response and normalizes timeout / NRC handling.
 */
static int wait_transfer_stage(UDSClient_t *client, uint32_t timeout_ms, const char *stage)
{
    uint32_t t_start;
    uint8_t nrc;

    if (client == NULL || timeout_ms == 0u) {
        return -1;
    }

    t_start = sys_tick_get_ms();
    while (client->state != 0) {
        uds_poll();
        if (sys_tick_get_ms() - t_start > timeout_ms) {
            printf("\n");
            LOG_ERROR("%s Timeout after %lu ms", stage, (unsigned long)timeout_ms);
            return -1;
        }
    }

    nrc = uds_get_last_nrc();
    if (nrc != 0u) {
        printf("\n");
        LOG_ERROR("%s Error: 0x%02X", stage, nrc);
        return -1;
    }

    if (client->recv_size == 0u) {
        printf("\n");
        LOG_ERROR("%s Empty response", stage);
        return -1;
    }

    return 0;
}

/**
 * @brief Handles the 'sy' (Send Y-modem style) upload command.
 * @details Initiates a UDS Upload sequence:
 *          1. 0x38 RequestFileTransfer (AddFile)
 *          2. Loop 0x36 TransferData until EOF
 *          3. 0x37 RequestTransferExit with CRC32
 */
static int handle_upload(int argc, char **argv) 
{
    const char *filename;
    FILE *fp;
    long file_pos;
    size_t filesize;
    UDSClient_t *client;
    struct RequestFileTransferResponse resp = {0};
    size_t max_chunk;
    size_t payload_len;
    uint8_t buffer[BLOCK_SIZE_BUFFER];
    uint8_t seq = 1;
    size_t sent_bytes = 0;
    uint32_t crc = 0;
    size_t read_len;
    uint8_t exit_data[4];

    if (argc < 2) return 0;
    
    filename = argv[1];
    fp = fopen(filename, "rb");
    if (!fp) { 
        LOG_ERROR("File not found: %s", filename); 
        return -1; 
    }

    /* Calculate file size */
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        LOG_ERROR("Failed to seek file: %s", filename);
        return -1;
    }
    file_pos = ftell(fp);
    if (file_pos < 0) {
        fclose(fp);
        LOG_ERROR("Failed to query file size: %s", filename);
        return -1;
    }
    filesize = (size_t)file_pos;
    (void)fseek(fp, 0, SEEK_SET);

    LOG_INFO("Uploading '%s' (%lu bytes)...", filename, (unsigned long)filesize);
    client = uds_get_client();

    /* 1. Request: 0x38 AddFile */
    if (UDS_TRANSACTION(UDSSendRequestFileTransfer(client, MOOP_ADD_FILE, filename, 0x00, 4, filesize, filesize), "Initializing") != 0) {
        fclose(fp); 
        return -1;
    }

    /* Determine block size from response */
    if (UDSUnpackRequestFileTransferResponse(client, &resp) != UDS_OK) {
        fclose(fp);
        LOG_ERROR("Invalid RequestFileTransfer response");
        return -1;
    }
    max_chunk = resp.maxNumberOfBlockLength;
    if (max_chunk < 3u) {
        fclose(fp);
        LOG_ERROR("Invalid maxNumberOfBlockLength: %lu", (unsigned long)max_chunk);
        return -1;
    }
    if (max_chunk > (sizeof(buffer) + 2u)) {
        max_chunk = sizeof(buffer) + 2u;
    }
    payload_len = max_chunk - 2u; /* Subtract SID (1) + Sequence (1) */
    
    /* 2. Transfer Loop: 0x36 TransferData */
    while (sent_bytes < filesize) {
        read_len = fread(buffer, 1, payload_len, fp);
        if (read_len == 0u) {
            if (ferror(fp)) {
                printf("\n");
                LOG_ERROR("Read failed while uploading '%s'", filename);
                fclose(fp);
                return -1;
            }
            break;
        }
        
        crc = crc32_calc(crc, buffer, read_len);

        uds_prepare_request(); /* Clear flags */
        if (UDSSendTransferData(client, seq, (uint16_t)(read_len + 2u), buffer, (uint16_t)read_len) != UDS_OK) {
            printf("\n");
            LOG_ERROR("Failed to send block %u", (unsigned)seq);
            fclose(fp);
            return -1;
        }

        if (wait_transfer_stage(client, 2000u, "Upload block") != 0) {
            fclose(fp);
            return -1;
        }
        if (client->recv_size < 2u || client->recv_buf[0] != 0x76u || client->recv_buf[1] != seq) {
            printf("\n");
            LOG_ERROR("Unexpected TransferData response for block %u", (unsigned)seq);
            fclose(fp);
            return -1;
        }

        sent_bytes += read_len;
        seq++;
        utils_render_progress(sent_bytes, filesize, "Uploading");
    }
    printf("\n");
    fclose(fp);

    /* 3. Exit: 0x37 with CRC */
    exit_data[0] = (uint8_t)((crc >> 24) & 0xFF);
    exit_data[1] = (uint8_t)((crc >> 16) & 0xFF);
    exit_data[2] = (uint8_t)((crc >> 8) & 0xFF);
    exit_data[3] = (uint8_t)(crc & 0xFF);

    if (UDS_TRANSACTION(UDSSendRequestTransferExit(client, exit_data, 4), "Finalizing") == 0) {
        LOG_INFO("Upload Complete (CRC: 0x%08X).", crc);
        return 0;
    }
    return -1;
}

/**
 * @brief Handles the 'ry' (Receive Y-modem style) download command.
 * @details Initiates a UDS Download sequence:
 *          1. 0x38 RequestFileTransfer (ReadFile)
 *          2. Loop 0x36 TransferData to request blocks
 *          3. 0x37 RequestTransferExit
 */
static int handle_download(int argc, char **argv) 
{
    const char *filename;
    FILE *fp;
    UDSClient_t *client;
    struct RequestFileTransferResponse resp = {0};
    size_t total_size;
    uint8_t seq = 1;
    size_t received_bytes = 0;
    uint32_t crc = 0;
    int eof = 0;
    size_t data_len;
    int rc = -1;

    if (argc < 2) return 0;
    
    filename = argv[1];
    fp = fopen(filename, "wb");
    if (!fp) { 
        LOG_ERROR("Cannot write %s", filename); 
        return -1; 
    }

    client = uds_get_client();

    /* 1. Request: 0x38 ReadFile */
    if (UDS_TRANSACTION(UDSSendRequestFileTransfer(client, MOOP_READ_FILE, filename, 0x00, 0, 0, 0), "Initializing") != 0) {
        goto cleanup;
    }

    if (UDSUnpackRequestFileTransferResponse(client, &resp) != UDS_OK) {
        LOG_ERROR("Invalid RequestFileTransfer response");
        goto cleanup;
    }
    total_size = resp.fileSizeUncompressed;
    LOG_INFO("Remote File Size: %lu bytes", (unsigned long)total_size);

    /* 2. Transfer Loop: 0x36 TransferData */
    while (!eof) {
        uds_prepare_request();
        if (UDSSendTransferData(client, seq, 2, NULL, 0) != UDS_OK) {
            LOG_ERROR("Failed to request block %u", (unsigned)seq);
            goto cleanup;
        }

        if (wait_transfer_stage(client, 3000u, "Download block") != 0) {
            goto cleanup;
        }
        if (client->recv_size < 2u || client->recv_buf[0] != 0x76u || client->recv_buf[1] != seq) {
            LOG_ERROR("Unexpected TransferData response for block %u", (unsigned)seq);
            goto cleanup;
        }

        /* Extract Data: [SID] [Seq] [Data...] */
        data_len = (size_t)client->recv_size - 2u;
        
        if (data_len > 0u) {
            if (fwrite(&client->recv_buf[2], 1, data_len, fp) != data_len) {
                LOG_ERROR("Write failed while downloading '%s'", filename);
                goto cleanup;
            }
            crc = crc32_calc(crc, &client->recv_buf[2], data_len);
            received_bytes += data_len;
            
            utils_render_progress(received_bytes, total_size, "Downloading");
            
            seq++;
            /* Check for EOF based on size if known */
            if (total_size > 0u && received_bytes >= total_size) {
                eof = 1;
            }
        } else {
            /* Zero-length payload typically indicates EOF */
            eof = 1;
        }
    }
    printf("\n");

    /* 3. Exit: 0x37 */
    if (UDS_TRANSACTION(UDSSendRequestTransferExit(client, NULL, 0), "Finalizing") == 0) {
        LOG_INFO("Download Complete. Local CRC: 0x%08X", crc);
        rc = 0;
    }

cleanup:
    fclose(fp);
    if (rc != 0) {
        remove(filename);
    }
    return rc;
}

/* ==========================================================================
 * Initialization
 * ========================================================================== */

void client_file_svc_init(void) 
{
    /* Register LLS */
    cmd_register("lls", handle_lls, "List Local Files", NULL);
    
    /* Hijack 'sy' for Upload with hint */
    cmd_register("sy", handle_upload, "Upload File (UDS)", " <local_file>");
    
    /* Hijack 'ry' for Download with hint */
    cmd_register("ry", handle_download, "Download File (UDS)", " <remote_file>");
}