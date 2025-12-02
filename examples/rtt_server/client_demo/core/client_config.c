/**
 * @file core/client_config.c
 * @brief Implementation of Configuration Loading & Parsing.
 * @details Handles command-line argument parsing to override default UDS
 *          connection parameters (Interface, IDs) at runtime.
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

#include "client_config.h"
#include "../utils/utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> /* for getopt */

/* ==========================================================================
 * Global Variables
 * ========================================================================== */

/**
 * @brief Global Configuration Instance.
 * @details Initialized with default values defined in client_config.h.
 *          These values can be overwritten by client_config_parse_args().
 */
client_runtime_config_t g_uds_cfg = {
    .if_name = DEFAULT_CAN_IF,
    .phys_sa = DEFAULT_PHYS_SA,
    .phys_ta = DEFAULT_PHYS_TA,
    .func_sa = DEFAULT_FUNC_SA
};

/* ==========================================================================
 * Static Helper Functions
 * ========================================================================== */

/**
 * @brief Prints the usage help message to standard output.
 * 
 * @param prog_name The name of the executable (usually argv[0]).
 */
static void print_usage(const char *prog_name) 
{
    printf("Usage: %s [options]\n", prog_name);
    printf("Options:\n");
    printf("  -i <iface>   CAN Interface (default: %s)\n", DEFAULT_CAN_IF);
    printf("  -s <hex_id>  Client Source ID (default: %03X)\n", DEFAULT_PHYS_SA);
    printf("  -t <hex_id>  Server Target ID (default: %03X)\n", DEFAULT_PHYS_TA);
    printf("  -f <hex_id>  Functional ID    (default: %03X)\n", DEFAULT_FUNC_SA);
    printf("  -h           Show this help\n");
    printf("\nExample:\n");
    printf("  %s -i vcan0 -s 7E8 -t 7E0\n", prog_name);
}

/* ==========================================================================
 * Public Functions
 * ========================================================================== */

/**
 * @brief Parses command line arguments to configure the client.
 * @details Uses `getopt` to handle flags. If valid arguments are provided,
 *          it updates the global `g_uds_cfg` structure.
 *          If `-h` is passed or an invalid option is detected, the program exits.
 * 
 * @param argc Argument count from main().
 * @param argv Argument vector from main().
 */
void client_config_parse_args(int argc, char **argv) 
{
    int opt;

    /* 
     * Parse arguments using getopt.
     * optstring "i:s:t:f:h":
     *  - 'i', 's', 't', 'f' require an argument (followed by :).
     *  - 'h' does not require an argument.
     */
    while ((opt = getopt(argc, argv, "i:s:t:f:h")) != -1) {
        switch (opt) {
        case 'i':
            snprintf(g_uds_cfg.if_name, sizeof(g_uds_cfg.if_name), "%s", optarg);
            break;

        case 's':
            /* Base 16 for Hexadecimal IDs */
            g_uds_cfg.phys_sa = (uint32_t)strtoul(optarg, NULL, 16);
            break;

        case 't':
            g_uds_cfg.phys_ta = (uint32_t)strtoul(optarg, NULL, 16);
            break;

        case 'f':
            g_uds_cfg.func_sa = (uint32_t)strtoul(optarg, NULL, 16);
            break;

        case 'h':
            print_usage(argv[0]);
            exit(EXIT_SUCCESS); /* Exit directly on help */

        default:
            /* '?' is returned by getopt on unknown option */
            print_usage(argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    /* Log the final configuration for verification */
    printf("[Config] IF: %s | SA: 0x%X | TA: 0x%X | FUNC: 0x%X\n", 
           g_uds_cfg.if_name, 
           g_uds_cfg.phys_sa, 
           g_uds_cfg.phys_ta, 
           g_uds_cfg.func_sa);
}