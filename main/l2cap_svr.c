/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * L2CAP Connection-Oriented Channel echo server for blue-falcon testing.
 * Listens on PSM 0x0080 and echoes any received data back to the client.
 */

#include "bf_test.h"
#include "host/ble_hs.h"
#include "host/ble_l2cap.h"
#include "os/os_mbuf.h"
#include "esp_log.h"

static const char *TAG = "bf_l2cap";

/* SDU receive buffer — one per channel, we only support one connection */
static struct os_mbuf_pool sdu_mbuf_pool;
static struct os_mempool sdu_mempool;
static os_membuf_t sdu_mem[OS_MEMPOOL_SIZE(1, 512)];

static struct os_mbuf *
alloc_sdu_buf(void)
{
    struct os_mbuf *buf = os_mbuf_get_pkthdr(&sdu_mbuf_pool, 0);
    return buf;
}

static int
l2cap_event(struct ble_l2cap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_L2CAP_EVENT_COC_CONNECTED:
        if (event->connect.status == 0) {
            ESP_LOGI(TAG, "L2CAP CoC connected (conn=%d)",
                     event->connect.conn_handle);
        } else {
            ESP_LOGW(TAG, "L2CAP CoC connect failed: %d",
                     event->connect.status);
        }
        break;

    case BLE_L2CAP_EVENT_COC_DISCONNECTED:
        ESP_LOGI(TAG, "L2CAP CoC disconnected (conn=%d)",
                 event->disconnect.conn_handle);
        break;

    case BLE_L2CAP_EVENT_COC_ACCEPT: {
        /* Accept incoming connection — provide an SDU receive buffer */
        struct os_mbuf *sdu = alloc_sdu_buf();
        if (sdu == NULL) {
            ESP_LOGE(TAG, "L2CAP accept: out of SDU buffers");
            return BLE_HS_ENOMEM;
        }
        ble_l2cap_recv_ready(event->accept.chan, sdu);
        ESP_LOGI(TAG, "L2CAP CoC accepted (conn=%d, peer_sdu=%d)",
                 event->accept.conn_handle,
                 event->accept.peer_sdu_size);
        return 0;
    }

    case BLE_L2CAP_EVENT_COC_DATA_RECEIVED: {
        /* Echo received data back */
        struct os_mbuf *rx = event->receive.sdu_rx;
        uint16_t len = OS_MBUF_PKTLEN(rx);
        ESP_LOGI(TAG, "L2CAP recv %u bytes, echoing", len);

        /* Build the echo response */
        struct os_mbuf *tx = ble_hs_mbuf_from_flat(rx->om_data, len);
        if (tx != NULL) {
            int rc = ble_l2cap_send(event->receive.chan, tx);
            if (rc != 0) {
                ESP_LOGW(TAG, "L2CAP send failed: %d", rc);
                os_mbuf_free_chain(tx);
            }
        }

        /* Re-arm the receive buffer */
        struct os_mbuf *next_sdu = alloc_sdu_buf();
        if (next_sdu != NULL) {
            ble_l2cap_recv_ready(event->receive.chan, next_sdu);
        }
        break;
    }

    case BLE_L2CAP_EVENT_COC_TX_UNSTALLED:
        ESP_LOGD(TAG, "L2CAP TX unstalled");
        break;
    }

    return 0;
}

void
l2cap_svr_init(void)
{
    int rc;

    /* Initialize the SDU mbuf pool */
    rc = os_mempool_init(&sdu_mempool, 1, 512, sdu_mem, "l2cap_sdu");
    assert(rc == 0);

    rc = os_mbuf_pool_init(&sdu_mbuf_pool, &sdu_mempool, 512, 1);
    assert(rc == 0);

    /* Register the L2CAP CoC server */
    rc = ble_l2cap_create_server(BF_L2CAP_PSM, 512, l2cap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "l2cap_create_server failed: %d", rc);
    } else {
        ESP_LOGI(TAG, "L2CAP CoC server listening on PSM 0x%04X", BF_L2CAP_PSM);
    }
}
