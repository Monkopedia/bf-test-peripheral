/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * bf-test-peripheral: BLE GATT server for integration testing blue-falcon.
 *
 * Exposes two services covering the blue-falcon API surface:
 *
 *   Service 1  "BF Test Service"        (BF10)
 *     Char A   READ                     (BFA1) - returns a fixed 8-byte value
 *     Char B   WRITE (with response)    (BFA2) - stores last written value
 *     Char C   WRITE_NO_RSP             (BFA3) - stores last written value
 *     Char D   NOTIFY                   (BFA4) - ticks a counter every second
 *     Char E   INDICATE                 (BFA5) - echoes writes to Char B
 *     Char F   READ|WRITE + descriptors (BFA6) - has a user description descriptor
 *     Char H   NOTIFY|INDICATE          (BFA7) - ticks counter, supports both
 *
 *   Service 2  "BF Secure Service"      (BF20)
 *     Char G   READ (encrypted)         (BFB1) - forces pairing to read
 *
 *   L2CAP CoC server on PSM 0x0080 — echoes received data back.
 */

#ifndef BF_TEST_H
#define BF_TEST_H

#include "nimble/nimble_port.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 128-bit UUIDs (base: BF??0000-1000-2000-8000-00805F9B34FB) ---- */

/* Service 1: BF Test Service */
#define BF_TEST_SVC_UUID                                                \
    BLE_UUID128_INIT(0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,  \
                     0x00, 0x20, 0x00, 0x10, 0x10, 0xBF, 0x00, 0x00)

/* Char A: read-only */
#define BF_CHR_READ_UUID                                                \
    BLE_UUID128_INIT(0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,  \
                     0x00, 0x20, 0x00, 0x10, 0xA1, 0xBF, 0x00, 0x00)

/* Char B: write with response */
#define BF_CHR_WRITE_UUID                                               \
    BLE_UUID128_INIT(0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,  \
                     0x00, 0x20, 0x00, 0x10, 0xA2, 0xBF, 0x00, 0x00)

/* Char C: write without response */
#define BF_CHR_WRITE_NR_UUID                                            \
    BLE_UUID128_INIT(0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,  \
                     0x00, 0x20, 0x00, 0x10, 0xA3, 0xBF, 0x00, 0x00)

/* Char D: notify */
#define BF_CHR_NOTIFY_UUID                                              \
    BLE_UUID128_INIT(0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,  \
                     0x00, 0x20, 0x00, 0x10, 0xA4, 0xBF, 0x00, 0x00)

/* Char E: indicate */
#define BF_CHR_INDICATE_UUID                                            \
    BLE_UUID128_INIT(0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,  \
                     0x00, 0x20, 0x00, 0x10, 0xA5, 0xBF, 0x00, 0x00)

/* Char F: read/write with descriptors */
#define BF_CHR_DESC_UUID                                                \
    BLE_UUID128_INIT(0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,  \
                     0x00, 0x20, 0x00, 0x10, 0xA6, 0xBF, 0x00, 0x00)

/* Char H: notify + indicate */
#define BF_CHR_NOTIFY_INDICATE_UUID                                     \
    BLE_UUID128_INIT(0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,  \
                     0x00, 0x20, 0x00, 0x10, 0xA7, 0xBF, 0x00, 0x00)

/* Service 2: BF Secure Service */
#define BF_SECURE_SVC_UUID                                              \
    BLE_UUID128_INIT(0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,  \
                     0x00, 0x20, 0x00, 0x10, 0x20, 0xBF, 0x00, 0x00)

/* Char G: encrypted read */
#define BF_CHR_SECURE_UUID                                              \
    BLE_UUID128_INIT(0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,  \
                     0x00, 0x20, 0x00, 0x10, 0xB1, 0xBF, 0x00, 0x00)

/* L2CAP CoC PSM (must be >= 0x0080 for dynamic range) */
#define BF_L2CAP_PSM  0x0080

/* GATT handles (populated during registration) */
extern uint16_t bf_notify_chr_handle;
extern uint16_t bf_indicate_chr_handle;
extern uint16_t bf_notify_indicate_chr_handle;

/* Initialize the GATT server and register services. */
int gatt_svr_init(void);

/* Called from the notify timer to send a tick on Char D and Char H. */
void gatt_svr_notify_tick(uint16_t conn_handle);

/* Called when Char B is written to echo via Char E indication. */
void gatt_svr_indicate_echo(uint16_t conn_handle, const uint8_t *data, uint16_t len);

/* Initialize the L2CAP CoC echo server. */
void l2cap_svr_init(void);

#ifdef __cplusplus
}
#endif

#endif /* BF_TEST_H */
