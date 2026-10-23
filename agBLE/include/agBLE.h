#ifndef AGBLE_H
#define AGBLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* UUIDs are in Bluetooth little-endian byte order: 0x180F -> {0x0f, 0x18}.
 * A 128-bit UUID is the reverse of its usual human-readable hex notation. */
typedef struct {
    uint8_t length;                /* 2 or 16 */
    uint8_t bytes[16];
} agble_uuid_t;

typedef struct {
    uint8_t type;                  /* NimBLE/Bluetooth address type: 0 public, 1 random */
    uint8_t bytes[6];              /* on-air little-endian order; copy from SCAN_RESULT */
} agble_address_t;

typedef enum {
    AGBLE_STACK_READY,
    AGBLE_SCAN_RESULT,
    AGBLE_SCAN_DONE,
    AGBLE_CONNECTED,               /* link established; GATT not ready yet */
    AGBLE_READY,                   /* service/characteristics discovered, CCCD enabled if needed */
    AGBLE_DATA,                    /* notification/indication, or result of agble_read() */
    AGBLE_WRITE_DONE,
    AGBLE_DISCONNECTED,
    AGBLE_ERROR
} agble_event_type_t;

typedef enum {
    AGBLE_PHASE_STACK,
    AGBLE_PHASE_SCAN,
    AGBLE_PHASE_CONNECT,
    AGBLE_PHASE_DISCOVERY,
    AGBLE_PHASE_SUBSCRIBE,
    AGBLE_PHASE_READ,
    AGBLE_PHASE_WRITE
} agble_phase_t;

/* Event and data are valid only until the callback returns; copy to retain.
 * status is a NimBLE host/ATT code (0 = success); ERROR specifies its phase.
 * SCAN_RESULT includes the peer's address and RSSI; DATA includes binary data.
 * DATA.from_read distinguishes an explicit read from an unsolicited update. */
typedef struct {
    agble_event_type_t type;
    agble_phase_t phase;
    int status;
    agble_address_t address;
    int8_t rssi;
    const uint8_t *data;
    size_t length;
    bool from_read;
} agble_event_t;

typedef void (*agble_callback_t)(const agble_event_t *event, void *context);

typedef struct {
    agble_uuid_t service;
    agble_uuid_t write_char;       /* required; must support Write (with response) */
    agble_uuid_t receive_char;     /* required; Read, Notify or Indicate */
    agble_callback_t callback;
    void *context;
} agble_config_t;

/* Single central connection / single owner of the NimBLE host. ESP-IDF 6.1-dev,
 * CONFIG_BT_ENABLED=y, CONFIG_BT_NIMBLE_ENABLED=y, NimBLE central, observer and
 * GATT client enabled, and a BLE-capable target required.
 * Initialize NVS (nvs_flash_init) first; do not initialize NimBLE elsewhere.
 * All functions are nonblocking except deinit. Call public functions serially
 * from one application task (or a callback); callbacks normally run in the
 * NimBLE host task, but manual scan_stop reports SCAN_DONE in the caller task.
 * Do not block callbacks and never call deinit from one. No pairing or
 * encryption is initiated; protected attributes may reject access.
 * The config is copied. AGBLE_STACK_READY signals that scanning may begin. */
esp_err_t agble_init(const agble_config_t *config);

/* 1..INT32_MAX ms, or 0 for continuous legacy passive scanning. Every report
 * is delivered (duplicates possible). Stop scanning before connecting. */
esp_err_t agble_scan_start(uint32_t duration_ms);
esp_err_t agble_scan_stop(void);

/* Address copied from SCAN_RESULT; 1..INT32_MAX ms timeout. AGBLE_READY is
 * required before read/write. A failed attempt reports AGBLE_ERROR (CONNECT).
 * Discovery/subscription errors report ERROR and attempt to disconnect;
 * a failure to terminate is also reported as ERROR (CONNECT). */
esp_err_t agble_connect(const agble_address_t *address, uint32_t timeout_ms);
esp_err_t agble_disconnect(void);  /* asynchronous; await DISCONNECTED */

/* Write with response; length 1..min(512, current MTU - 3). Data is copied by
 * NimBLE before return. One read or write at a time; completion reported via
 * WRITE_DONE or DATA (from_read=true), or ERROR with READ/WRITE phase;
 * DISCONNECTED supersedes any pending I/O completion. MTU exchange is attempted
 * on connect; if rejected, the link uses its default MTU (normally 23). */
esp_err_t agble_write(const void *data, size_t length);
esp_err_t agble_read(void);        /* single ATT read (at most MTU - 1 bytes);
                                    * requires Read property on receive_char */

/* Only after scanning has stopped and the link has disconnected (or never
 * connected). Stops the owned NimBLE host, deletes its task and releases it.
 * Does not deinitialize NVS; no other user may share this NimBLE instance. */
esp_err_t agble_deinit(void);

/* Peripheral/server mode: an alternative to agble_init(), for a remote BLE
 * central (e.g. a phone or PC) to connect and write commands to this ESP.
 * Only one mode can own NimBLE. Requires NimBLE peripheral + GATT server as
 * well as NVS initialized by the application. A server runs until reboot.
 * The device advertises under `name` (1..20 bytes), with service UUID
 * 0xFFF0, with a command characteristic 0xFFF1 (Write with response) and a
 * status characteristic 0xFFF2 (Read + Notify). The callback receives the
 * entire command (1..512 bytes). The peer should subscribe to 0xFFF2 and
 * read it once on connection to obtain the current value.
 * Data is valid only inside the callback, which runs in NimBLE's host task.
 * SERVER_ERROR.status contains a NimBLE host code. No pairing is required. */
#define AGBLE_SERVER_SERVICE_UUID 0xFFF0
#define AGBLE_SERVER_COMMAND_UUID 0xFFF1
#define AGBLE_SERVER_STATUS_UUID  0xFFF2

typedef enum {
    AGBLE_SERVER_ADVERTISING,
    AGBLE_SERVER_CONNECTED,
    AGBLE_SERVER_DISCONNECTED,
    AGBLE_SERVER_COMMAND,
    AGBLE_SERVER_ERROR
} agble_server_event_type_t;

typedef struct {
    agble_server_event_type_t type;
    int status;
    const uint8_t *data;
    size_t length;
} agble_server_event_t;

typedef void (*agble_server_callback_t)(const agble_server_event_t *event, void *context);

/* Copies name and callback settings; returns when the host task has been
 * launched. Wait for ADVERTISING before trying to connect. On disconnect the
 * device automatically advertises again. */
esp_err_t agble_server_start(const char *name, agble_server_callback_t callback,
                             void *context);

/* Save the current status and notify subscribed peers. Data is copied and can
 * be read through 0xFFF2 even without a subscriber. Maximum 20 bytes, so
 * notifications fit the default BLE MTU (23). Call from an application task
 * or from the server callback; ESP_OK means the update was accepted, not that
 * the remote application received the notification. */
esp_err_t agble_server_publish(const void *data, size_t length);

/* Example (after nvs_flash_init): use your peripheral's UUIDs instead.
 * agble_config_t cfg = {
 *     .service = { .length = 2, .bytes = {0x00, 0xff} },
 *     .write_char = { .length = 2, .bytes = {0x01, 0xff} },
 *     .receive_char = { .length = 2, .bytes = {0x02, 0xff} },
 *     .callback = on_ble_event,
 * };
 * agble_init(&cfg);
 * // in on_ble_event: STACK_READY -> agble_scan_start(5000);
 * // SCAN_RESULT -> save address; SCAN_DONE -> agble_connect(&saved, 10000);
 * // READY -> agble_write(command, sizeof command); agble_read() later;
 * // DATA -> copy event->data before returning; DISCONNECTED -> deinit from app task.
 */

#ifdef __cplusplus
}
#endif
#endif
