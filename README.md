# bf-test-peripheral

ESP32-C6 BLE GATT server for integration testing
[blue-falcon](https://github.com/Reedyuk/blue-falcon), a Kotlin
Multiplatform Bluetooth library.

## GATT Profile

The firmware exposes two services covering the blue-falcon API surface:

### Service 1: BF Test Service (`0000BF10-...`)

| Char | UUID       | Properties          | Purpose                              |
|------|------------|---------------------|--------------------------------------|
| A    | `BFA1`     | Read                | Returns fixed 8-byte value           |
| B    | `BFA2`     | Read, Write         | Stores last written value; triggers indication on Char E |
| C    | `BFA3`     | Read, Write No Rsp  | Stores last written value            |
| D    | `BFA4`     | Read, Notify        | Ticks a counter every 1 second       |
| E    | `BFA5`     | Read, Indicate      | Echoes writes to Char B              |
| F    | `BFA6`     | Read, Write + Desc  | Has a writable User Description descriptor (0x2901) |

### Service 2: BF Secure Service (`0000BF20-...`)

| Char | UUID       | Properties          | Purpose                              |
|------|------------|---------------------|--------------------------------------|
| G    | `BFB1`     | Read (encrypted)    | Forces pairing before read succeeds  |

### Advertising

- Device name: `BF-Test`
- Scan response includes the BF Test Service UUID and manufacturer data
  (`0xFFFF` + `"BF"`)

## blue-falcon API Coverage

| blue-falcon API              | Exercised by         |
|------------------------------|----------------------|
| `scan` / `didDiscoverDevice` | Advertising + scan response data |
| `connect` / `disconnect`     | Any connection       |
| `discoverServices`           | Two services present |
| `discoverCharacteristics`    | Seven characteristics across services |
| `readCharacteristic`         | Char A (fixed), Char B/C (verify writes) |
| `writeCharacteristic`        | Char B (write-with-response) |
| `writeCharacteristicWithoutEncoding` | Char C (write-no-response) |
| `notifyCharacteristic`       | Char D (1s counter) |
| `indicateCharacteristic`     | Char E (echoes Char B writes) |
| `readDescriptor` / `writeDescriptor` | Char F's User Description |
| `createBond`                 | Char G (requires encryption) |
| `changeMTU` / `didUpdateMTU` | MTU negotiation at connect time |

## Hardware

Targets **ESP32-C6-WROOM-1** but should work on any ESP32 variant with
BLE support (C3, S3, etc.) with minimal sdkconfig changes.

## Building

Requires [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/get-started/)
v5.x.

```sh
idf.py set-target esp32c6
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

## License

Apache-2.0. See [LICENSE](LICENSE).
