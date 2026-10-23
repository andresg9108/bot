#include "agBLE.h"
#include "agBLE_internal.h"

#include <stdatomic.h>
#include <string.h>

#include "sdkconfig.h"
#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BT_NIMBLE_ENABLED) || \
    !defined(CONFIG_BT_NIMBLE_ROLE_PERIPHERAL) || !defined(CONFIG_BT_NIMBLE_GATT_SERVER)
#error "agBLE server requires NimBLE peripheral and GATT server support"
#endif

#include "freertos/FreeRTOS.h"
#include "host/ble_att.h"
#include "host/ble_gatt.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "os/os_mbuf.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static struct {
    char name[21];
    agble_server_callback_t callback;
    void *context;
    uint8_t own_addr;
    uint16_t status_handle;
    uint8_t status[20];
    size_t status_length;
    atomic_bool started;
    atomic_bool synced;
} server;

static portMUX_TYPE status_lock = portMUX_INITIALIZER_UNLOCKED;
static const ble_uuid16_t service_uuid = BLE_UUID16_INIT(AGBLE_SERVER_SERVICE_UUID);

static void notify(agble_server_event_type_t type, int status,
                   const uint8_t *data, size_t length)
{
    agble_server_event_t event = {
        .type = type, .status = status, .data = data, .length = length,
    };
    server.callback(&event, server.context);
}

static int command_access(uint16_t conn, uint16_t attr,
                          struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn;
    (void)attr;
    (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_UNLIKELY;

    size_t length = OS_MBUF_PKTLEN(ctxt->om);
    if (!length || length > 512) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;

    uint8_t data[512];
    if (os_mbuf_copydata(ctxt->om, 0, length, data)) return BLE_ATT_ERR_UNLIKELY;
    notify(AGBLE_SERVER_COMMAND, 0, data, length);
    return 0;
}

static int status_access(uint16_t conn, uint16_t attr,
                         struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn;
    (void)attr;
    (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_UNLIKELY;

    uint8_t data[sizeof(server.status)];
    portENTER_CRITICAL(&status_lock);
    size_t length = server.status_length;
    memcpy(data, server.status, length);
    portEXIT_CRITICAL(&status_lock);
    return os_mbuf_append(ctxt->om, data, length) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
}

static const struct ble_gatt_chr_def characteristics[] = {
    {
        .uuid = BLE_UUID16_DECLARE(AGBLE_SERVER_COMMAND_UUID),
        .access_cb = command_access,
        .flags = BLE_GATT_CHR_F_WRITE,
    },
    {
        .uuid = BLE_UUID16_DECLARE(AGBLE_SERVER_STATUS_UUID),
        .access_cb = status_access,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &server.status_handle,
    },
    {0},
};

static const struct ble_gatt_svc_def services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &service_uuid.u,
        .characteristics = characteristics,
    },
    {0},
};

static int gap_event(struct ble_gap_event *event, void *arg);

static void advertise(void)
{
    struct ble_hs_adv_fields fields = {
        .flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP,
        .name = (uint8_t *)server.name,
        .name_len = strlen(server.name),
        .name_is_complete = 1,
        .uuids16 = &service_uuid,
        .num_uuids16 = 1,
        .uuids16_is_complete = 1,
    };
    int rc = ble_gap_adv_set_fields(&fields);
    if (!rc) {
        struct ble_gap_adv_params params = {
            .conn_mode = BLE_GAP_CONN_MODE_UND,
            .disc_mode = BLE_GAP_DISC_MODE_GEN,
        };
        rc = ble_gap_adv_start(server.own_addr, NULL, BLE_HS_FOREVER,
                               &params, gap_event, NULL);
    }
    notify(rc ? AGBLE_SERVER_ERROR : AGBLE_SERVER_ADVERTISING, rc, NULL, 0);
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (!event->connect.status) {
            notify(AGBLE_SERVER_CONNECTED, 0, NULL, 0);
        } else {
            notify(AGBLE_SERVER_ERROR, event->connect.status, NULL, 0);
            advertise();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        notify(AGBLE_SERVER_DISCONNECTED, event->disconnect.reason, NULL, 0);
        advertise();
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        advertise();
        break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == server.status_handle &&
            event->subscribe.cur_notify) {
            portENTER_CRITICAL(&status_lock);
            bool has_status = server.status_length != 0;
            portEXIT_CRITICAL(&status_lock);
            if (has_status) ble_gatts_chr_updated(server.status_handle);
        }
        break;
    default:
        break;
    }
    return 0;
}

static void on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (!rc) rc = ble_hs_id_infer_auto(0, &server.own_addr);
    if (rc) notify(AGBLE_SERVER_ERROR, rc, NULL, 0);
    else {
        server.synced = true;
        advertise();
    }
}

static void on_reset(int reason)
{
    server.synced = false;
    notify(AGBLE_SERVER_ERROR, reason, NULL, 0);
}

static void host_task(void *arg)
{
    (void)arg;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t agble_server_start(const char *name, agble_server_callback_t callback,
                             void *context)
{
    if (!name || !callback) return ESP_ERR_INVALID_ARG;
    size_t name_length = strnlen(name, sizeof(server.name));
    if (!name_length || name_length >= sizeof(server.name)) return ESP_ERR_INVALID_ARG;
    if (!agble_owner_claim()) return ESP_ERR_INVALID_STATE;

    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        agble_owner_release();
        return err;
    }
    memcpy(server.name, name, name_length + 1);
    server.callback = callback;
    server.context = context;
    server.status_handle = 0;
    server.status_length = 0;
    server.synced = false;

#if CONFIG_BT_NIMBLE_GAP_SERVICE
    ble_svc_gap_init();
    int rc = ble_svc_gap_device_name_set(server.name);
    if (rc) goto fail;
#else
    int rc;
#endif
    ble_svc_gatt_init();
    rc = ble_gatts_count_cfg(services);
    if (rc) goto fail;
    rc = ble_gatts_add_svcs(services);
    if (rc) goto fail;

    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    server.started = true;
    err = nimble_port_freertos_init(host_task);
    if (err == ESP_OK) return ESP_OK;
    server.started = false;
    ble_hs_cfg.sync_cb = NULL;
    ble_hs_cfg.reset_cb = NULL;
    goto cleanup;

fail:
    err = ESP_FAIL;
cleanup:
    (void)nimble_port_deinit();
    agble_owner_release();
    return err;
}

esp_err_t agble_server_publish(const void *data, size_t length)
{
    if (!data || !length || length > sizeof(server.status)) return ESP_ERR_INVALID_ARG;
    if (!server.started) return ESP_ERR_INVALID_STATE;

    portENTER_CRITICAL(&status_lock);
    memcpy(server.status, data, length);
    server.status_length = length;
    portEXIT_CRITICAL(&status_lock);

    if (server.synced && server.status_handle) {
        ble_gatts_chr_updated(server.status_handle);
    }
    return ESP_OK;
}
