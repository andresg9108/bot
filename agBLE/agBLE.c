#include "agBLE.h"
#include "agBLE_internal.h"

#include <limits.h>
#include <stdatomic.h>
#include <string.h>

#include "sdkconfig.h"
#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BT_NIMBLE_ENABLED)
#error "agBLE requires CONFIG_BT_ENABLED=y and CONFIG_BT_NIMBLE_ENABLED=y"
#endif
#if !defined(CONFIG_BT_NIMBLE_ROLE_CENTRAL) || !defined(CONFIG_BT_NIMBLE_ROLE_OBSERVER) || \
    !defined(CONFIG_BT_NIMBLE_GATT_CLIENT)
#error "agBLE requires NimBLE central, observer and GATT client support"
#endif

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_gatt.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "os/os_mbuf.h"

typedef enum { IDLE, SCANNING, CONNECTING, DISCOVERING, READY, CLOSING, STOPPING } link_state_t;
typedef enum { NONE, MTU, SERVICE, CHARACTERISTICS, DESCRIPTORS, SUBSCRIBE } discovery_t;
typedef enum { NO_IO, READING, WRITING } io_t;

static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;
static atomic_bool host_owned;

bool agble_owner_claim(void)
{
    return !atomic_exchange(&host_owned, true);
}

void agble_owner_release(void)
{
    atomic_store(&host_owned, false);
}

static _Atomic(TaskHandle_t) host_handle;
static struct {
    atomic_bool initialized;
    atomic_bool synced;
    unsigned callbacks_active;
    atomic_uint generation;
    agble_config_t config;
    SemaphoreHandle_t stopped;
    _Atomic(link_state_t) state;
    _Atomic(discovery_t) discovery;
    _Atomic(io_t) io;
    uint16_t conn;
    uint16_t svc_start, svc_end;
    uint16_t tx, rx, rx_end, cccd;
    uint8_t rx_props;
    ble_uuid_any_t svc_uuid;
} client;

static void emit(agble_event_type_t type, agble_phase_t phase, int status,
                 const agble_address_t *address, int8_t rssi,
                 const uint8_t *data, size_t length, bool from_read)
{
    agble_event_t ev = {
        .type = type, .phase = phase, .status = status, .rssi = rssi,
        .data = data, .length = length, .from_read = from_read,
    };
    if (address) {
        ev.address = *address;
    }
    portENTER_CRITICAL(&state_lock);
    ++client.callbacks_active;
    portEXIT_CRITICAL(&state_lock);
    client.config.callback(&ev, client.config.context);
    portENTER_CRITICAL(&state_lock);
    --client.callbacks_active;
    portEXIT_CRITICAL(&state_lock);
}

static bool uuid_valid(const agble_uuid_t *uuid)
{
    return uuid->length == 2 || uuid->length == 16;
}

static ble_uuid_any_t nimble_uuid(const agble_uuid_t *uuid)
{
    ble_uuid_any_t out = {0};
    if (uuid->length == 2) {
        out.u16.u.type = BLE_UUID_TYPE_16;
        out.u16.value = (uint16_t)uuid->bytes[0] | ((uint16_t)uuid->bytes[1] << 8);
    } else {
        out.u128.u.type = BLE_UUID_TYPE_128;
        memcpy(out.u128.value, uuid->bytes, 16);
    }
    return out;
}

static bool uuid_matches(const agble_uuid_t *wanted, const ble_uuid_t *found)
{
    ble_uuid_any_t uuid = nimble_uuid(wanted);
    return ble_uuid_cmp(&uuid.u, found) == 0;
}

static bool current(uint16_t conn, discovery_t stage)
{
    bool ok;
    portENTER_CRITICAL(&state_lock);
    ok = client.initialized && client.state == DISCOVERING &&
         client.conn == conn && client.discovery == stage;
    portEXIT_CRITICAL(&state_lock);
    return ok;
}

static void fail_discovery(uint16_t conn, agble_phase_t phase, int status)
{
    portENTER_CRITICAL(&state_lock);
    bool active = client.state == DISCOVERING && client.conn == conn;
    if (active) {
        client.state = CLOSING;
        client.discovery = NONE;
    }
    portEXIT_CRITICAL(&state_lock);
    if (!active) return;
    emit(AGBLE_ERROR, phase, status, NULL, 0, NULL, 0, false);
    int rc = ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
    if (rc == BLE_HS_ENOTCONN) {
        portENTER_CRITICAL(&state_lock);
        bool report = client.state == CLOSING && client.conn == conn;
        if (report) {
            client.state = IDLE;
            client.io = NO_IO;
            ++client.generation;
        }
        portEXIT_CRITICAL(&state_lock);
        if (report) emit(AGBLE_DISCONNECTED, AGBLE_PHASE_CONNECT, rc, NULL, 0, NULL, 0, false);
    } else if (rc) {
        emit(AGBLE_ERROR, AGBLE_PHASE_CONNECT, rc, NULL, 0, NULL, 0, false);
    }
}

static void ready(uint16_t conn)
{
    portENTER_CRITICAL(&state_lock);
    bool active = client.state == DISCOVERING && client.conn == conn;
    if (active) {
        client.discovery = NONE;
        client.state = READY;
    }
    portEXIT_CRITICAL(&state_lock);
    if (active) emit(AGBLE_READY, AGBLE_PHASE_DISCOVERY, 0, NULL, 0, NULL, 0, false);
}

static int subscribe_cb(uint16_t conn, const struct ble_gatt_error *error,
                        struct ble_gatt_attr *attr, void *arg)
{
    (void)attr;
    (void)arg;
    if (!current(conn, SUBSCRIBE)) return 0;
    if (error->status) fail_discovery(conn, AGBLE_PHASE_SUBSCRIBE, error->status);
    else ready(conn);
    return 0;
}

static int descriptor_cb(uint16_t conn, const struct ble_gatt_error *error,
                         uint16_t chr_handle, const struct ble_gatt_dsc *dsc, void *arg)
{
    (void)arg;
    if (!current(conn, DESCRIPTORS)) return 0;
    if (error->status == 0) {
        if (chr_handle == client.rx && ble_uuid_u16(&dsc->uuid.u) == BLE_GATT_DSC_CLT_CFG_UUID16) {
            client.cccd = dsc->handle;
        }
        return 0;
    }
    if (error->status != BLE_HS_EDONE || client.cccd == 0) {
        fail_discovery(conn, AGBLE_PHASE_SUBSCRIBE,
                       error->status == BLE_HS_EDONE ? BLE_HS_ENOENT : error->status);
        return 0;
    }
    client.discovery = SUBSCRIBE;
    uint8_t value[2] = { (client.rx_props & BLE_GATT_CHR_PROP_NOTIFY) ? 1 : 2, 0 };
    int rc = ble_gattc_write_flat(conn, client.cccd, value, sizeof(value), subscribe_cb, NULL);
    if (rc) fail_discovery(conn, AGBLE_PHASE_SUBSCRIBE, rc);
    return 0;
}

static int characteristic_cb(uint16_t conn, const struct ble_gatt_error *error,
                             const struct ble_gatt_chr *chr, void *arg)
{
    (void)arg;
    if (!current(conn, CHARACTERISTICS)) return 0;
    if (error->status == 0) {
        if (client.rx && !client.rx_end && chr->def_handle > client.rx) {
            client.rx_end = chr->def_handle - 1;
        }
        if (!client.tx && uuid_matches(&client.config.write_char, &chr->uuid.u) &&
            (chr->properties & BLE_GATT_CHR_PROP_WRITE)) {
            client.tx = chr->val_handle;
        }
        if (!client.rx && uuid_matches(&client.config.receive_char, &chr->uuid.u) &&
            (chr->properties & (BLE_GATT_CHR_PROP_READ | BLE_GATT_CHR_PROP_NOTIFY |
                                BLE_GATT_CHR_PROP_INDICATE))) {
            client.rx = chr->val_handle;
            client.rx_props = chr->properties;
        }
        return 0;
    }
    if (error->status != BLE_HS_EDONE || !client.tx || !client.rx) {
        fail_discovery(conn, AGBLE_PHASE_DISCOVERY,
                       error->status == BLE_HS_EDONE ? BLE_HS_ENOENT : error->status);
        return 0;
    }
    if (!(client.rx_props & (BLE_GATT_CHR_PROP_NOTIFY | BLE_GATT_CHR_PROP_INDICATE))) {
        ready(conn); /* read-only receive characteristic */
        return 0;
    }
    if (!client.rx_end) client.rx_end = client.svc_end;
    if (client.rx_end <= client.rx) {
        fail_discovery(conn, AGBLE_PHASE_SUBSCRIBE, BLE_HS_ENOENT);
        return 0;
    }
    client.discovery = DESCRIPTORS;
    int rc = ble_gattc_disc_all_dscs(conn, client.rx, client.rx_end, descriptor_cb, NULL);
    if (rc) fail_discovery(conn, AGBLE_PHASE_SUBSCRIBE, rc);
    return 0;
}

static int service_cb(uint16_t conn, const struct ble_gatt_error *error,
                      const struct ble_gatt_svc *svc, void *arg)
{
    (void)arg;
    if (!current(conn, SERVICE)) return 0;
    if (error->status == 0) {
        if (!client.svc_start) {
            client.svc_start = svc->start_handle;
            client.svc_end = svc->end_handle;
        }
        return 0;
    }
    if (error->status != BLE_HS_EDONE || !client.svc_start) {
        fail_discovery(conn, AGBLE_PHASE_DISCOVERY,
                       error->status == BLE_HS_EDONE ? BLE_HS_ENOENT : error->status);
        return 0;
    }
    client.discovery = CHARACTERISTICS;
    int rc = ble_gattc_disc_all_chrs(conn, client.svc_start, client.svc_end,
                                      characteristic_cb, NULL);
    if (rc) fail_discovery(conn, AGBLE_PHASE_DISCOVERY, rc);
    return 0;
}

static void discover_service(uint16_t conn)
{
    if (!current(conn, MTU)) return;
    client.discovery = SERVICE;
    int rc = ble_gattc_disc_svc_by_uuid(conn, &client.svc_uuid.u, service_cb, NULL);
    if (rc) fail_discovery(conn, AGBLE_PHASE_DISCOVERY, rc);
}

static int mtu_cb(uint16_t conn, const struct ble_gatt_error *error,
                  uint16_t mtu, void *arg)
{
    (void)error; /* Continue with the default MTU when exchange is rejected. */
    (void)mtu;
    (void)arg;
    discover_service(conn);
    return 0;
}

static int read_cb(uint16_t conn, const struct ble_gatt_error *error,
                   struct ble_gatt_attr *attr, void *arg)
{
    portENTER_CRITICAL(&state_lock);
    bool active = client.state == READY && client.conn == conn && client.io == READING &&
                  client.generation == (unsigned)(uintptr_t)arg;
    if (active) client.io = NO_IO;
    portEXIT_CRITICAL(&state_lock);
    if (!active) return 0;
    if (error->status) {
        emit(AGBLE_ERROR, AGBLE_PHASE_READ, error->status, NULL, 0, NULL, 0, false);
        return 0;
    }
    uint8_t value[512];
    size_t length = OS_MBUF_PKTLEN(attr->om);
    if (length > sizeof(value) || os_mbuf_copydata(attr->om, 0, length, value)) {
        emit(AGBLE_ERROR, AGBLE_PHASE_READ, BLE_HS_EMSGSIZE, NULL, 0, NULL, 0, false);
    } else {
        emit(AGBLE_DATA, AGBLE_PHASE_READ, 0, NULL, 0, value, length, true);
    }
    return 0;
}

static int write_cb(uint16_t conn, const struct ble_gatt_error *error,
                    struct ble_gatt_attr *attr, void *arg)
{
    (void)attr;
    portENTER_CRITICAL(&state_lock);
    bool active = client.state == READY && client.conn == conn && client.io == WRITING &&
                  client.generation == (unsigned)(uintptr_t)arg;
    if (active) client.io = NO_IO;
    portEXIT_CRITICAL(&state_lock);
    if (!active) return 0;
    emit(error->status ? AGBLE_ERROR : AGBLE_WRITE_DONE, AGBLE_PHASE_WRITE,
         error->status, NULL, 0, NULL, 0, false);
    return 0;
}

static int gap_cb(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    portENTER_CRITICAL(&state_lock);
    ++client.callbacks_active;
    portEXIT_CRITICAL(&state_lock);
    switch (event->type) {
    case BLE_GAP_EVENT_DISC: {
        if (client.state != SCANNING) break;
        agble_address_t address = { .type = event->disc.addr.type };
        memcpy(address.bytes, event->disc.addr.val, sizeof(address.bytes));
        emit(AGBLE_SCAN_RESULT, AGBLE_PHASE_SCAN, 0, &address,
             event->disc.rssi, NULL, 0, false);
        break;
    }
    case BLE_GAP_EVENT_DISC_COMPLETE:
        if (client.state == SCANNING) {
            client.state = IDLE;
            emit(AGBLE_SCAN_DONE, AGBLE_PHASE_SCAN, event->disc_complete.reason,
                 NULL, 0, NULL, 0, false);
        }
        break;
    case BLE_GAP_EVENT_CONNECT:
        if (client.state != CONNECTING) break;
        if (event->connect.status) {
            client.state = IDLE;
            emit(AGBLE_ERROR, AGBLE_PHASE_CONNECT, event->connect.status,
                 NULL, 0, NULL, 0, false);
            break;
        }
        portENTER_CRITICAL(&state_lock);
        client.conn = event->connect.conn_handle;
        client.svc_start = client.svc_end = client.tx = client.rx = client.rx_end = client.cccd = 0;
        client.rx_props = 0;
        ++client.generation;
        client.io = NO_IO;
        client.discovery = MTU;
        client.state = DISCOVERING;
        portEXIT_CRITICAL(&state_lock);
        emit(AGBLE_CONNECTED, AGBLE_PHASE_CONNECT, 0, NULL, 0, NULL, 0, false);
        if (current(client.conn, MTU)) {
            int rc = ble_gattc_exchange_mtu(client.conn, mtu_cb, NULL);
            if (rc) discover_service(client.conn);
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT: {
        portENTER_CRITICAL(&state_lock);
        bool report = (client.state == DISCOVERING || client.state == READY ||
                       client.state == CLOSING) &&
                      client.conn == event->disconnect.conn.conn_handle;
        if (report) {
            client.discovery = NONE;
            client.io = NO_IO;
            ++client.generation;
            client.state = IDLE;
        }
        portEXIT_CRITICAL(&state_lock);
        if (report) {
            emit(AGBLE_DISCONNECTED, AGBLE_PHASE_CONNECT, event->disconnect.reason,
                 NULL, 0, NULL, 0, false);
        }
        break;
    }
    case BLE_GAP_EVENT_NOTIFY_RX:
        if (client.state == READY && client.conn == event->notify_rx.conn_handle &&
            client.rx == event->notify_rx.attr_handle) {
            uint8_t value[512];
            size_t length = OS_MBUF_PKTLEN(event->notify_rx.om);
            if (length > sizeof(value) || os_mbuf_copydata(event->notify_rx.om, 0, length, value)) {
                emit(AGBLE_ERROR, AGBLE_PHASE_READ, BLE_HS_EMSGSIZE,
                     NULL, 0, NULL, 0, false);
            } else {
                emit(AGBLE_DATA, AGBLE_PHASE_READ, 0, NULL, 0, value, length, false);
            }
        }
        break;
    default:
        break;
    }
    portENTER_CRITICAL(&state_lock);
    --client.callbacks_active;
    portEXIT_CRITICAL(&state_lock);
    return 0;
}

static void on_sync(void)
{
    portENTER_CRITICAL(&state_lock);
    ++client.callbacks_active;
    bool stopping = client.state == STOPPING;
    portEXIT_CRITICAL(&state_lock);
    if (!stopping) {
        int rc = ble_hs_util_ensure_addr(0);
        if (rc) {
            emit(AGBLE_ERROR, AGBLE_PHASE_STACK, rc, NULL, 0, NULL, 0, false);
        } else {
            portENTER_CRITICAL(&state_lock);
            client.synced = true;
            portEXIT_CRITICAL(&state_lock);
            emit(AGBLE_STACK_READY, AGBLE_PHASE_STACK, 0, NULL, 0, NULL, 0, false);
        }
    }
    portENTER_CRITICAL(&state_lock);
    --client.callbacks_active;
    portEXIT_CRITICAL(&state_lock);
}

static void on_reset(int reason)
{
    portENTER_CRITICAL(&state_lock);
    ++client.callbacks_active;
    bool stopping = client.state == STOPPING;
    client.synced = false;
    if (!stopping) client.state = IDLE;
    client.io = NO_IO;
    ++client.generation;
    client.discovery = NONE;
    portEXIT_CRITICAL(&state_lock);
    if (!stopping) emit(AGBLE_ERROR, AGBLE_PHASE_STACK, reason, NULL, 0, NULL, 0, false);
    portENTER_CRITICAL(&state_lock);
    --client.callbacks_active;
    portEXIT_CRITICAL(&state_lock);
}

static void host_task(void *arg)
{
    (void)arg;
    host_handle = xTaskGetCurrentTaskHandle();
    nimble_port_run();
    /* The owner deletes this suspended task after observing completion.  Giving
     * the semaphore before self-deletion used to race with a second init(). */
    xSemaphoreGive(client.stopped);
    vTaskSuspend(NULL);
}

esp_err_t agble_init(const agble_config_t *config)
{
    if (!config || !config->callback || !uuid_valid(&config->service) ||
        !uuid_valid(&config->write_char) || !uuid_valid(&config->receive_char)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!agble_owner_claim()) return ESP_ERR_INVALID_STATE;
    client.stopped = xSemaphoreCreateBinary();
    if (!client.stopped) {
        agble_owner_release();
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        vSemaphoreDelete(client.stopped);
        client.stopped = NULL;
        agble_owner_release();
        return err;
    }
    client.config = *config;
    client.svc_uuid = nimble_uuid(&config->service);
    client.state = IDLE;
    client.synced = false;
    client.callbacks_active = 0;
    client.initialized = true;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    err = nimble_port_freertos_init(host_task);
    if (err != ESP_OK) {
        client.initialized = false;
        ble_hs_cfg.sync_cb = NULL;
        ble_hs_cfg.reset_cb = NULL;
        esp_err_t cleanup = nimble_port_deinit();
        vSemaphoreDelete(client.stopped);
        client.stopped = NULL;
        agble_owner_release();
        return cleanup == ESP_OK ? err : cleanup;
    }
    return ESP_OK;
}

esp_err_t agble_scan_start(uint32_t duration_ms)
{
    if (duration_ms > INT32_MAX) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&state_lock);
    bool ok = client.initialized && client.synced && client.state == IDLE;
    if (ok) client.state = SCANNING;
    portEXIT_CRITICAL(&state_lock);
    if (!ok) return ESP_ERR_INVALID_STATE;
    uint8_t own_addr;
    int rc = ble_hs_id_infer_auto(0, &own_addr);
    if (!rc) {
        struct ble_gap_disc_params params = { .passive = 1, .filter_duplicates = 0 };
        rc = ble_gap_disc(own_addr, duration_ms ? (int32_t)duration_ms : BLE_HS_FOREVER,
                          &params, gap_cb, NULL);
    }
    if (rc) {
        portENTER_CRITICAL(&state_lock);
        if (client.state == SCANNING) client.state = IDLE;
        portEXIT_CRITICAL(&state_lock);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t agble_scan_stop(void)
{
    if (client.state != SCANNING) return ESP_ERR_INVALID_STATE;
    int rc = ble_gap_disc_cancel();
    if (rc && rc != BLE_HS_EALREADY) return ESP_FAIL;
    portENTER_CRITICAL(&state_lock);
    bool report = client.state == SCANNING;
    if (report) client.state = IDLE;
    portEXIT_CRITICAL(&state_lock);
    if (report) emit(AGBLE_SCAN_DONE, AGBLE_PHASE_SCAN, 0, NULL, 0, NULL, 0, false);
    return ESP_OK;
}

esp_err_t agble_connect(const agble_address_t *address, uint32_t timeout_ms)
{
    if (!address || address->type > 1 || !timeout_ms || timeout_ms > INT32_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    portENTER_CRITICAL(&state_lock);
    bool ok = client.initialized && client.synced && client.state == IDLE;
    if (ok) client.state = CONNECTING;
    portEXIT_CRITICAL(&state_lock);
    if (!ok) return ESP_ERR_INVALID_STATE;
    uint8_t own_addr;
    int rc = ble_hs_id_infer_auto(0, &own_addr);
    if (!rc) {
        ble_addr_t peer = { .type = address->type };
        memcpy(peer.val, address->bytes, sizeof(peer.val));
        rc = ble_gap_connect(own_addr, &peer, (int32_t)timeout_ms, NULL, gap_cb, NULL);
    }
    if (rc) {
        portENTER_CRITICAL(&state_lock);
        if (client.state == CONNECTING) client.state = IDLE;
        portEXIT_CRITICAL(&state_lock);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t agble_disconnect(void)
{
    if (!client.initialized) return ESP_ERR_INVALID_STATE;
    if (client.state == CONNECTING) {
        int rc = ble_gap_conn_cancel();
        return rc ? ESP_FAIL : ESP_OK; /* CONNECT error callback follows */
    }
    portENTER_CRITICAL(&state_lock);
    link_state_t prior = client.state;
    bool ok = prior == READY || prior == DISCOVERING || prior == CLOSING;
    uint16_t conn = client.conn;
    if (ok) {
        client.state = CLOSING;
        client.io = NO_IO;
        ++client.generation;
    }
    portEXIT_CRITICAL(&state_lock);
    if (!ok) return ESP_ERR_INVALID_STATE;
    int rc = ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
    if (rc) {
        portENTER_CRITICAL(&state_lock);
        if (client.state == CLOSING && client.conn == conn) client.state = prior;
        portEXIT_CRITICAL(&state_lock);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t agble_write(const void *data, size_t length)
{
    if (!data || !length || length > 512) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&state_lock);
    bool ok = client.initialized && client.state == READY && client.io == NO_IO;
    uint16_t conn = 0, tx = 0;
    unsigned generation = 0;
    if (ok) {
        client.io = WRITING;
        generation = ++client.generation;
        conn = client.conn;
        tx = client.tx;
    }
    portEXIT_CRITICAL(&state_lock);
    if (!ok) return ESP_ERR_INVALID_STATE;
    uint16_t mtu = ble_att_mtu(conn);
    if (mtu < 4 || length > mtu - 3) {
        io_t expected = WRITING;
        if (client.generation == generation) atomic_compare_exchange_strong(&client.io, &expected, NO_IO);
        return mtu < 4 ? ESP_ERR_INVALID_STATE : ESP_ERR_INVALID_SIZE;
    }
    if (client.state != READY || client.generation != generation) return ESP_ERR_INVALID_STATE;
    int rc = ble_gattc_write_flat(conn, tx, data, (uint16_t)length,
                                  write_cb, (void *)(uintptr_t)generation);
    if (rc) {
        io_t expected = WRITING;
        if (client.generation == generation) atomic_compare_exchange_strong(&client.io, &expected, NO_IO);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t agble_read(void)
{
    portENTER_CRITICAL(&state_lock);
    bool ok = client.initialized && client.state == READY && client.io == NO_IO;
    bool readable = ok && (client.rx_props & BLE_GATT_CHR_PROP_READ);
    uint16_t conn = 0, rx = 0;
    unsigned generation = 0;
    if (readable) {
        client.io = READING;
        generation = ++client.generation;
        conn = client.conn;
        rx = client.rx;
    }
    portEXIT_CRITICAL(&state_lock);
    if (!ok) return ESP_ERR_INVALID_STATE;
    if (!readable) return ESP_ERR_NOT_SUPPORTED;
    if (client.state != READY || client.generation != generation) return ESP_ERR_INVALID_STATE;
    int rc = ble_gattc_read(conn, rx, read_cb, (void *)(uintptr_t)generation);
    if (rc) {
        io_t expected = READING;
        if (client.generation == generation) atomic_compare_exchange_strong(&client.io, &expected, NO_IO);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t agble_deinit(void)
{
    if (xTaskGetCurrentTaskHandle() == host_handle) {
        return ESP_ERR_INVALID_STATE;
    }
    portENTER_CRITICAL(&state_lock);
    bool ok = client.initialized && client.state == IDLE && !client.callbacks_active;
    if (ok) client.state = STOPPING;
    portEXIT_CRITICAL(&state_lock);
    if (!ok) return ESP_ERR_INVALID_STATE;
    int rc = nimble_port_stop();
    if (rc) {
        portENTER_CRITICAL(&state_lock);
        client.state = IDLE;
        portEXIT_CRITICAL(&state_lock);
        return ESP_FAIL;
    }
    xSemaphoreTake(client.stopped, portMAX_DELAY);
    nimble_port_freertos_deinit();
    esp_err_t err = nimble_port_deinit();
    vSemaphoreDelete(client.stopped);
    client.stopped = NULL;
    host_handle = NULL;
    client.initialized = false;
    client.synced = false;
    ble_hs_cfg.sync_cb = NULL;
    ble_hs_cfg.reset_cb = NULL;
    agble_owner_release();
    return err;
}
