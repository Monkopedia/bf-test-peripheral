/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * bf-test-peripheral: main entry point.
 *
 * Sets up advertising, connection handling, security manager, and a
 * 1-second notify timer.
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "host/ble_store.h"
#include "bf_test.h"

/* Forward declaration — defined in NimBLE store config module */
void ble_store_config_init(void);

static const char *TAG = "bf_main";
static const char *DEVICE_NAME = "BF-Test";

static uint16_t conn_handle = BLE_HS_CONN_HANDLE_NONE;
static bool notify_enabled = false;
static bool notify_indicate_enabled = false;
static TimerHandle_t notify_timer;

/* Forward declarations */
static int gap_event(struct ble_gap_event *event, void *arg);
static void start_advertise(void);

/* ---- Advertising ---- */

static void
start_advertise(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    int rc;

    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    fields.name = (uint8_t *)DEVICE_NAME;
    fields.name_len = strlen(DEVICE_NAME);
    fields.name_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_set_fields failed: %d", rc);
        return;
    }

    /* Scan response with service UUID so scan filters work */
    struct ble_hs_adv_fields rsp_fields;
    memset(&rsp_fields, 0, sizeof(rsp_fields));

    static ble_uuid128_t svc_uuid = BF_TEST_SVC_UUID;
    static ble_uuid_any_t svc_uuids[1];
    svc_uuids[0].u128 = svc_uuid;
    rsp_fields.uuids128 = (ble_uuid128_t *)svc_uuids;
    rsp_fields.num_uuids128 = 1;
    rsp_fields.uuids128_is_complete = 1;

    /* Manufacturer-specific data: company ID 0xFFFF (test), payload "BF" */
    static uint8_t mfg_data[] = { 0xFF, 0xFF, 'B', 'F' };
    rsp_fields.mfg_data = mfg_data;
    rsp_fields.mfg_data_len = sizeof(mfg_data);

    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_rsp_set_fields failed: %d", rc);
        return;
    }

    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                           &adv_params, gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_start failed: %d", rc);
    }
    ESP_LOGI(TAG, "Advertising started");
}

/* ---- GAP event handler ---- */

static int
gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "Connection %s (handle=%d)",
                 event->connect.status == 0 ? "established" : "failed",
                 event->connect.conn_handle);
        if (event->connect.status == 0) {
            conn_handle = event->connect.conn_handle;
        } else {
            start_advertise();
        }
        break;

    case BLE_GAP_EVENT_PARING_COMPLETE:
        ESP_LOGI(TAG, "Pairing complete: status=%d",
                 event->pairing_complete.status);
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Disconnected (reason=%d)", event->disconnect.reason);
        conn_handle = BLE_HS_CONN_HANDLE_NONE;
        notify_enabled = false;
        notify_indicate_enabled = false;
        start_advertise();
        break;

    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "Subscribe: handle=%d, notify=%d, indicate=%d",
                 event->subscribe.attr_handle,
                 event->subscribe.cur_notify,
                 event->subscribe.cur_indicate);
        if (event->subscribe.attr_handle == bf_notify_chr_handle) {
            notify_enabled = event->subscribe.cur_notify;
            /* Send an immediate value so clients don't wait for the next timer tick */
            if (notify_enabled) {
                gatt_svr_notify_char_d(conn_handle);
            }
        }
        if (event->subscribe.attr_handle == bf_notify_indicate_chr_handle) {
            notify_indicate_enabled = event->subscribe.cur_notify ||
                                     event->subscribe.cur_indicate;
            if (notify_indicate_enabled) {
                gatt_svr_notify_char_h(conn_handle);
            }
        }
        break;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU updated: conn_handle=%d, mtu=%d",
                 event->mtu.conn_handle, event->mtu.value);
        break;

    case BLE_GAP_EVENT_ENC_CHANGE:
        ESP_LOGI(TAG, "Encryption change: status=%d", event->enc_change.status);
        break;

    case BLE_GAP_EVENT_PASSKEY_ACTION: {
        ESP_LOGI(TAG, "Passkey action: action=%d",
                 event->passkey.params.action);
        if (event->passkey.params.action == BLE_SM_IOACT_NUMCMP) {
            struct ble_sm_io pkey = {0};
            pkey.action = BLE_SM_IOACT_NUMCMP;
            pkey.numcmp_accept = 1;
            ble_sm_inject_io(event->passkey.conn_handle, &pkey);
        }
        /* For BLE_SM_IOACT_NONE (Just Works), no action needed */
        break;
    }

    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        /* Delete old bond and allow re-pairing */
        struct ble_gap_conn_desc desc;
        ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
        ble_store_util_delete_peer(&desc.peer_id_addr);
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }

    case BLE_GAP_EVENT_ADV_COMPLETE:
        start_advertise();
        break;
    }

    return 0;
}

/* ---- Notify timer ---- */

static void
notify_timer_cb(TimerHandle_t timer)
{
    if (notify_enabled) {
        gatt_svr_notify_char_d(conn_handle);
    }
    if (notify_indicate_enabled) {
        gatt_svr_notify_char_h(conn_handle);
    }
}

/* ---- NimBLE host callbacks ---- */

static void
on_sync(void)
{
    int rc;

    /* Use best available address (public or random) */
    rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "ensure_addr failed: %d", rc);
        return;
    }

    start_advertise();
}

static void
on_reset(int reason)
{
    ESP_LOGW(TAG, "BLE host reset: %d", reason);
}

static void
nimble_host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

/* ---- Security manager configuration ---- */

static void
configure_security(void)
{
    /* Just Works pairing (NoInputNoOutput) */
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist |= BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist |= BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
}

/* ---- Entry point ---- */

void
app_main(void)
{
    int rc;

    /* Initialize NVS (used by NimBLE for bond storage) */
    rc = nvs_flash_init();
    if (rc == ESP_ERR_NVS_NO_FREE_PAGES || rc == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        rc = nvs_flash_init();
    }
    ESP_ERROR_CHECK(rc);

    /* Initialize NimBLE */
    rc = nimble_port_init();
    ESP_ERROR_CHECK(rc);

    /* Configure host callbacks */
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    /* Configure security */
    configure_security();

    /* Initialize bond key storage */
    ble_store_config_init();

    /* Set device name */
    rc = ble_svc_gap_device_name_set(DEVICE_NAME);
    assert(rc == 0);

    /* Initialize GATT services */
    rc = gatt_svr_init();
    assert(rc == 0);

    /* Initialize L2CAP CoC echo server */
    l2cap_svr_init();

    /* Create 1-second notify timer */
    notify_timer = xTimerCreate("notify", pdMS_TO_TICKS(1000), pdTRUE,
                                NULL, notify_timer_cb);
    xTimerStart(notify_timer, 0);

    /* Start NimBLE host task */
    nimble_port_freertos_init(nimble_host_task);

    ESP_LOGI(TAG, "bf-test-peripheral started");
}
