#include "agBLE.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "agbot";

static void on_ble_event(const agble_server_event_t *event, void *context)
{
    (void)context;
    switch (event->type) {
    case AGBLE_SERVER_COMMAND:
        if (event->length == 1 && (event->data[0] == '0' || event->data[0] == '1' ||
                                   event->data[0] == 0 || event->data[0] == 1)) {
            int level = event->data[0] == '1' || event->data[0] == 1;
            esp_err_t err = gpio_set_level(GPIO_NUM_4, level);
            if (err == ESP_OK) {
                const uint8_t status = level ? '1' : '0';
                err = agble_server_publish(&status, sizeof(status));
                if (err == ESP_OK) ESP_LOGI(TAG, "GPIO 4: %d", level);
                else ESP_LOGE(TAG, "No se pudo publicar el estado: %s", esp_err_to_name(err));
            } else {
                ESP_LOGE(TAG, "GPIO 4: %s", esp_err_to_name(err));
            }
        } else {
            ESP_LOGW(TAG, "Comando ignorado: enviar un byte 0 o 1 (ASCII o binario)");
        }
        break;
    case AGBLE_SERVER_ADVERTISING:
        ESP_LOGI(TAG, "BLE agbot: servicio 0xFFF0, comandos 0xFFF1, estado 0xFFF2");
        break;
    case AGBLE_SERVER_CONNECTED:
        ESP_LOGI(TAG, "Cliente BLE conectado");
        break;
    case AGBLE_SERVER_DISCONNECTED:
        ESP_LOGI(TAG, "Cliente BLE desconectado; reanudando anuncios");
        break;
    case AGBLE_SERVER_ERROR:
        ESP_LOGE(TAG, "Error BLE: %d", event->status);
        break;
    }
}

void app_main(void)
{
    gpio_config_t output = {
        .pin_bit_mask = 1ULL << GPIO_NUM_4,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&output));
    ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_4, 0));

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(agble_server_start("agbot", on_ble_event, NULL));
    const uint8_t initial_status = '0';
    ESP_ERROR_CHECK(agble_server_publish(&initial_status, sizeof(initial_status)));
}
