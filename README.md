# bf-test-peripheral

A dedicated BLE peripheral firmware for integration testing
[blue-falcon](https://github.com/Reedyuk/blue-falcon), a Kotlin
Multiplatform Bluetooth library.

Runs on an ESP32-C6 (or C3/S3) and exposes a carefully designed GATT
profile that exercises every operation in blue-falcon's API: scanning,
connecting, reading, writing, notifications, indications, descriptors,
bonding, MTU negotiation, and L2CAP channels.

## Why this exists

blue-falcon supports Android, iOS, macOS, Windows, and JS — but has no
test suite. BLE libraries are notoriously hard to test because every
operation requires a real radio and a cooperating remote device. This
firmware *is* that cooperating device: a cheap, deterministic, always-on
test fixture you can plug into USB and forget about.

The [linux-native target effort](https://github.com/Monkopedia/blue-falcon)
(adding `linuxX64`/`linuxArm64` via sdbus-kotlin + BlueZ) uses this
peripheral for validation, but it's useful for testing any blue-falcon
platform.

## Quick start

### Pre-built firmware

Download the latest firmware from
[Releases](https://github.com/Monkopedia/bf-test-peripheral/releases)
and flash with `esptool`:

```sh
esptool.py --chip esp32c6 --port /dev/ttyUSB0 \
    write_flash 0x0 bootloader.bin 0x8000 partition-table.bin 0x10000 bf-test-peripheral.bin
```

### Building from source

Requires [ESP-IDF v5.x](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/get-started/).

```sh
idf.py set-target esp32c6
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

### Hardware

Developed on **ESP32-C6-WROOM-1**. Any ESP32 with BLE support works
(C3, S3, etc.) — just change the `set-target` argument. A ~$5 dev board
is all you need.

## GATT profile

The firmware advertises as **`BF-Test`** with two GATT services, an
L2CAP echo server, and manufacturer data in the scan response.

### Service 1: BF Test Service

UUID: `0000BF10-1000-2000-8000-00805F9B34FB`

| Char | UUID (short) | Properties              | Behavior                                  |
|------|-------------|-------------------------|-------------------------------------------|
| A    | `BFA1`      | Read                    | Returns fixed 8 bytes: `BF 01 02 03 04 05 06 07` |
| B    | `BFA2`      | Read, Write             | Stores last write; echoes it via Char E indication |
| C    | `BFA3`      | Read, Write No Response | Stores last write                         |
| D    | `BFA4`      | Read, Notify            | Sends incrementing counter every 1s       |
| E    | `BFA5`      | Read, Indicate          | Indicates value written to Char B         |
| F    | `BFA6`      | Read, Write + Descriptor| User Description descriptor (0x2901), read/write |
| H    | `BFA7`      | Read, Notify, Indicate  | Counter every 1s; supports both notify and indicate |

### Service 2: BF Secure Service

UUID: `0000BF20-1000-2000-8000-00805F9B34FB`

| Char | UUID (short) | Properties              | Behavior                                  |
|------|-------------|-------------------------|-------------------------------------------|
| G    | `BFB1`      | Read (encrypted)        | Returns `SECURE`; requires pairing first  |

### L2CAP CoC echo server

PSM: **`0x0080`**. Accepts one connection at a time. Echoes all
received data back to the sender.

### Advertising data

| Field | Value |
|-------|-------|
| Device name | `BF-Test` (complete) |
| Flags | General discoverable, BR/EDR not supported |
| TX power | Auto |
| 128-bit service UUID | BF Test Service (in scan response) |
| Manufacturer data | Company `0xFFFF` (reserved for testing), payload `BF` |

## blue-falcon API coverage

Every public method in blue-falcon's `BlueFalcon` class and every
callback in `BlueFalconDelegate` can be exercised against this
peripheral:

| blue-falcon API                          | How to test                          |
|------------------------------------------|--------------------------------------|
| `scan(filters)` / `didDiscoverDevice`    | Scan for `BF-Test` or filter by service UUID |
| `connect` / `disconnect`                 | Connect to the device                |
| `connectionState`                        | Check after connect/disconnect       |
| `discoverServices`                       | Two services returned                |
| `discoverCharacteristics`                | 8 characteristics across services    |
| `readCharacteristic`                     | Read Char A (fixed value) or B/C (verify writes) |
| `writeCharacteristic`                    | Write to Char B (write-with-response)|
| `writeCharacteristicWithoutEncoding`     | Write to Char C (write-no-response)  |
| `notifyCharacteristic` / `didCharacteristicValueChanged` | Subscribe to Char D |
| `indicateCharacteristic`                 | Subscribe to Char E, then write Char B |
| `notifyAndIndicateCharacteristic`        | Subscribe to Char H                  |
| `readDescriptor` / `writeDescriptor`     | Char F's User Description (0x2901)   |
| `createBond` / `didBondStateChanged`     | Read Char G (forces Just Works pairing) |
| `changeMTU` / `didUpdateMTU`            | MTU negotiated automatically at connect |
| `openL2capChannel` / `didOpenL2capChannel` | Connect to PSM 0x0080             |
| `didRssiUpdate`                          | Read RSSI on any active connection   |
| `retrievePeripheral`                     | Look up by address after discovery   |

The remaining methods (`requestConnectionPriority`, `removeBond`,
`clearPeripherals`) are client-side only and don't require peripheral
cooperation.

## Project structure

```
bf-test-peripheral/
  CMakeLists.txt          # ESP-IDF project root
  sdkconfig.defaults      # NimBLE + BLE-only + security manager
  main/
    CMakeLists.txt        # Component: main.c, gatt_svr.c, l2cap_svr.c
    bf_test.h             # UUIDs, handles, function declarations
    main.c                # Entry point, advertising, GAP events, notify timer
    gatt_svr.c            # GATT service/characteristic definitions and callbacks
    l2cap_svr.c           # L2CAP CoC echo server
```

## CI

Every push and PR builds firmware for three targets (esp32c6, esp32c3,
esp32s3) using the `espressif/idf:v5.4` Docker image. Tagged releases
(`v*`) also publish firmware binaries as GitHub Release assets.

## Contributing

Issues and PRs welcome. If you're adding a characteristic or service,
please update the coverage table above.

## License

Apache-2.0. See [LICENSE](LICENSE).
