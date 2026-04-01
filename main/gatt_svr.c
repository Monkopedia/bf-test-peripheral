/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * GATT service definitions for the blue-falcon test peripheral.
 */

#include <string.h>
#include "bf_test.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "services/gatt/ble_svc_gatt.h"
#include "esp_log.h"

static const char *TAG = "bf_gatt";

/* ---- Characteristic state ---- */

/* Char A: fixed read-only value */
static const uint8_t read_value[] = { 0xBF, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07 };

/* Char B: last value written (write with response) */
static uint8_t write_buf[512];
static uint16_t write_len = 0;

/* Char C: last value written (write without response) */
static uint8_t write_nr_buf[512];
static uint16_t write_nr_len = 0;

/* Char D: notify counter */
static uint8_t notify_counter = 0;
uint16_t bf_notify_chr_handle;

/* Char E: indicate handle */
uint16_t bf_indicate_chr_handle;

/* Char H: notify+indicate counter */
static uint8_t notify_indicate_counter = 0;
uint16_t bf_notify_indicate_chr_handle;

/* Char F: read/write with descriptors */
static uint8_t desc_chr_buf[512];
static uint16_t desc_chr_len = 0;
static uint8_t user_desc_buf[64] = "BF Test Descriptor";
static uint16_t user_desc_len = 18;

/* Char G: secure read value */
static const uint8_t secure_value[] = { 0x53, 0x45, 0x43, 0x55, 0x52, 0x45 }; /* "SECURE" */

/* ---- Access callbacks ---- */

static int
chr_access_test_svc(uint16_t conn_handle, uint16_t attr_handle,
                    struct ble_gatt_access_ctxt *ctxt, void *arg);

static int
chr_access_secure_svc(uint16_t conn_handle, uint16_t attr_handle,
                      struct ble_gatt_access_ctxt *ctxt, void *arg);

static int
dsc_access_user_desc(uint16_t conn_handle, uint16_t attr_handle,
                     struct ble_gatt_access_ctxt *ctxt, void *arg);

/* ---- Descriptor definitions ---- */

/* Characteristic User Description (0x2901) for Char F */
static const struct ble_gatt_dsc_def bf_chr_desc_dscs[] = {
    {
        .uuid = BLE_UUID16_DECLARE(0x2901), /* Char User Description */
        .att_flags = BLE_ATT_F_READ | BLE_ATT_F_WRITE,
        .access_cb = dsc_access_user_desc,
        .min_key_size = 0,
    },
    { 0 } /* sentinel */
};

/* ---- Service definitions ---- */

static const ble_uuid128_t bf_test_svc_uuid    = BF_TEST_SVC_UUID;
static const ble_uuid128_t bf_chr_read_uuid     = BF_CHR_READ_UUID;
static const ble_uuid128_t bf_chr_write_uuid    = BF_CHR_WRITE_UUID;
static const ble_uuid128_t bf_chr_write_nr_uuid = BF_CHR_WRITE_NR_UUID;
static const ble_uuid128_t bf_chr_notify_uuid   = BF_CHR_NOTIFY_UUID;
static const ble_uuid128_t bf_chr_indicate_uuid = BF_CHR_INDICATE_UUID;
static const ble_uuid128_t bf_chr_desc_uuid     = BF_CHR_DESC_UUID;
static const ble_uuid128_t bf_chr_ni_uuid       = BF_CHR_NOTIFY_INDICATE_UUID;
static const ble_uuid128_t bf_secure_svc_uuid   = BF_SECURE_SVC_UUID;
static const ble_uuid128_t bf_chr_secure_uuid   = BF_CHR_SECURE_UUID;

static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        /* Service 1: BF Test Service */
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &bf_test_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                /* Char A: Read-only */
                .uuid = &bf_chr_read_uuid.u,
                .access_cb = chr_access_test_svc,
                .flags = BLE_GATT_CHR_F_READ,
            },
            {
                /* Char B: Write with response */
                .uuid = &bf_chr_write_uuid.u,
                .access_cb = chr_access_test_svc,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
            },
            {
                /* Char C: Write without response */
                .uuid = &bf_chr_write_nr_uuid.u,
                .access_cb = chr_access_test_svc,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                /* Char D: Notify */
                .uuid = &bf_chr_notify_uuid.u,
                .access_cb = chr_access_test_svc,
                .val_handle = &bf_notify_chr_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            {
                /* Char E: Indicate */
                .uuid = &bf_chr_indicate_uuid.u,
                .access_cb = chr_access_test_svc,
                .val_handle = &bf_indicate_chr_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_INDICATE,
            },
            {
                /* Char F: Read/Write with descriptors */
                .uuid = &bf_chr_desc_uuid.u,
                .access_cb = chr_access_test_svc,
                .descriptors = (struct ble_gatt_dsc_def *)bf_chr_desc_dscs,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
            },
            {
                /* Char H: Notify + Indicate */
                .uuid = &bf_chr_ni_uuid.u,
                .access_cb = chr_access_test_svc,
                .val_handle = &bf_notify_indicate_chr_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_INDICATE,
            },
            { 0 } /* sentinel */
        },
    },
    {
        /* Service 2: BF Secure Service */
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &bf_secure_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                /* Char G: Encrypted read */
                .uuid = &bf_chr_secure_uuid.u,
                .access_cb = chr_access_secure_svc,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC,
            },
            { 0 } /* sentinel */
        },
    },
    { 0 } /* sentinel */
};

/* ---- Test service access callback ---- */

static int
chr_access_test_svc(uint16_t conn_handle, uint16_t attr_handle,
                    struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    const ble_uuid_t *uuid = ctxt->chr->uuid;
    int rc;

    /* Char A: read-only fixed value */
    if (ble_uuid_cmp(uuid, &bf_chr_read_uuid.u) == 0) {
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            rc = os_mbuf_append(ctxt->om, read_value, sizeof(read_value));
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        return BLE_ATT_ERR_UNLIKELY;
    }

    /* Char B: write with response (also readable to verify) */
    if (ble_uuid_cmp(uuid, &bf_chr_write_uuid.u) == 0) {
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            rc = os_mbuf_append(ctxt->om, write_buf, write_len);
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
            uint16_t om_len = OS_MBUF_PKTLEN(ctxt->om);
            if (om_len > sizeof(write_buf)) {
                return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
            }
            rc = ble_hs_mbuf_to_flat(ctxt->om, write_buf, sizeof(write_buf), &write_len);
            if (rc != 0) {
                return BLE_ATT_ERR_UNLIKELY;
            }
            ESP_LOGI(TAG, "Char B written, %u bytes", write_len);
            /* Echo to Char E via indication */
            gatt_svr_indicate_echo(conn_handle, write_buf, write_len);
            return 0;
        }
        return BLE_ATT_ERR_UNLIKELY;
    }

    /* Char C: write without response (also readable to verify) */
    if (ble_uuid_cmp(uuid, &bf_chr_write_nr_uuid.u) == 0) {
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            rc = os_mbuf_append(ctxt->om, write_nr_buf, write_nr_len);
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
            uint16_t om_len = OS_MBUF_PKTLEN(ctxt->om);
            if (om_len > sizeof(write_nr_buf)) {
                return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
            }
            rc = ble_hs_mbuf_to_flat(ctxt->om, write_nr_buf, sizeof(write_nr_buf), &write_nr_len);
            if (rc != 0) {
                return BLE_ATT_ERR_UNLIKELY;
            }
            ESP_LOGI(TAG, "Char C written (no-rsp), %u bytes", write_nr_len);
            return 0;
        }
        return BLE_ATT_ERR_UNLIKELY;
    }

    /* Char D: notify (readable for current counter value) */
    if (ble_uuid_cmp(uuid, &bf_chr_notify_uuid.u) == 0) {
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            rc = os_mbuf_append(ctxt->om, &notify_counter, sizeof(notify_counter));
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        return BLE_ATT_ERR_UNLIKELY;
    }

    /* Char E: indicate (readable for last indicated value) */
    if (ble_uuid_cmp(uuid, &bf_chr_indicate_uuid.u) == 0) {
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            rc = os_mbuf_append(ctxt->om, write_buf, write_len);
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        return BLE_ATT_ERR_UNLIKELY;
    }

    /* Char H: notify+indicate (readable for current counter) */
    if (ble_uuid_cmp(uuid, &bf_chr_ni_uuid.u) == 0) {
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            rc = os_mbuf_append(ctxt->om, &notify_indicate_counter,
                                sizeof(notify_indicate_counter));
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        return BLE_ATT_ERR_UNLIKELY;
    }

    /* Char F: read/write with descriptors */
    if (ble_uuid_cmp(uuid, &bf_chr_desc_uuid.u) == 0) {
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            rc = os_mbuf_append(ctxt->om, desc_chr_buf, desc_chr_len);
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
            uint16_t om_len = OS_MBUF_PKTLEN(ctxt->om);
            if (om_len > sizeof(desc_chr_buf)) {
                return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
            }
            rc = ble_hs_mbuf_to_flat(ctxt->om, desc_chr_buf, sizeof(desc_chr_buf), &desc_chr_len);
            if (rc != 0) {
                return BLE_ATT_ERR_UNLIKELY;
            }
            ESP_LOGI(TAG, "Char F written, %u bytes", desc_chr_len);
            return 0;
        }
        return BLE_ATT_ERR_UNLIKELY;
    }

    return BLE_ATT_ERR_UNLIKELY;
}

/* ---- Secure service access callback ---- */

static int
chr_access_secure_svc(uint16_t conn_handle, uint16_t attr_handle,
                      struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    /* Char G: encrypted read - NimBLE enforces encryption via READ_ENC flag,
     * so if we get here the link is already encrypted. */
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        int rc = os_mbuf_append(ctxt->om, secure_value, sizeof(secure_value));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

/* ---- Descriptor access callback ---- */

static int
dsc_access_user_desc(uint16_t conn_handle, uint16_t attr_handle,
                     struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_DSC) {
        int rc = os_mbuf_append(ctxt->om, user_desc_buf, user_desc_len);
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_DSC) {
        uint16_t om_len = OS_MBUF_PKTLEN(ctxt->om);
        if (om_len > sizeof(user_desc_buf)) {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        int rc = ble_hs_mbuf_to_flat(ctxt->om, user_desc_buf, sizeof(user_desc_buf), &user_desc_len);
        if (rc != 0) {
            return BLE_ATT_ERR_UNLIKELY;
        }
        ESP_LOGI(TAG, "User desc written, %u bytes", user_desc_len);
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

/* ---- Notify/Indicate helpers (called from main.c) ---- */

void
gatt_svr_notify_char_d(uint16_t conn_handle)
{
    if (conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return;
    }
    notify_counter++;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(&notify_counter, sizeof(notify_counter));
    if (om) {
        ble_gatts_notify_custom(conn_handle, bf_notify_chr_handle, om);
    }
}

void
gatt_svr_notify_char_h(uint16_t conn_handle)
{
    if (conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return;
    }
    notify_indicate_counter++;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(&notify_indicate_counter, sizeof(notify_indicate_counter));
    if (om) {
        ble_gatts_notify_custom(conn_handle, bf_notify_indicate_chr_handle, om);
    }
}

void
gatt_svr_indicate_echo(uint16_t conn_handle, const uint8_t *data, uint16_t len)
{
    if (conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return;
    }
    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (om) {
        ble_gatts_indicate_custom(conn_handle, bf_indicate_chr_handle, om);
    }
}

/* ---- Init ---- */

int
gatt_svr_init(void)
{
    int rc;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    rc = ble_gatts_count_cfg(gatt_svr_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_count_cfg failed: %d", rc);
        return rc;
    }

    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_add_svcs failed: %d", rc);
        return rc;
    }

    return 0;
}
