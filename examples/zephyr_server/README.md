# Zephyr Server Example

UDS server as a [Zephyr freestanding application](https://docs.zephyrproject.org/latest/develop/application/index.html#zephyr-freestanding-application).

This example uses Zephyr's native CAN + ISO-TP stack (`CONFIG_CAN`, `CONFIG_ISOTP`).

Tested on:
- `nucleo_g474re` (FDCAN1, pins PA11/PA12)
- `native_sim` (connected to host `vcan` interface on linux)

Physical addressing: request `0x7E0` / response `0x7E8`. Functional (broadcast) request: `0x7DF`.

# Building

```sh
source ~/zephyrproject/.venv/bin/activate
source ~/zephyrproject/zephyr/zephyr-env.sh
```


```sh
west build -b nucleo_g474re .
west flash --runner pyocd
```

```sh
west build -b native_sim . 
./build/zephyr/zephyr.exe -rt
```

`native_sim` defaults to using the linux virtual socketcan interface `vcan0`.
Enable it on the host with:
```sh
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0
```

then, on the host,
```sh
cansend vcan0 7DF#023E000000000000
candump vcan0
  vcan0  7DF   [8]  02 3E 00 00 00 00 00 00  # <-- message from host
  vcan0  7E8   [3]  02 7E 00                 # <-- response from server
```
