#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "pico/cyw43_arch.h"

#include "lwip/apps/mqtt.h"
#include "lwip/altcp_tls.h"

#include "home_experiment_inferencing.h"

#define LDR_PIN 26
#define LED_PIN CYW43_WL_GPIO_LED_PIN

const char* WIFI_SSID = "YOUR_WIFI";
const char* WIFI_PASS = "YOUR_PASS";

const char* MQTT_HOST = "192.168.1.10";
const int   MQTT_PORT = 8883;   // for TLS

mqtt_client_t *client;

/* CA certification */
const char *ca_cert =
"-----BEGIN CERTIFICATE-----\n"
"...YOUR_CA_CERT...\n"
"-----END CERTIFICATE-----\n";

/* sensor */
float read_light() {
    adc_select_input(0);
    return adc_read();
}

/* MQTT connection finished */
static void mqtt_connection_cb(mqtt_client_t *client, void *arg, mqtt_connection_status_t status) {
    if (status == MQTT_CONNECT_ACCEPTED) {
        printf("MQTT TLS Connected\n");
    } else {
        printf("MQTT Connect Failed: %d\n", status);
    }
}

/* publish */
void mqtt_send(float light, float dark) {

    char payload[128];

    snprintf(payload, sizeof(payload),
        "{\"light\":%.0f,\"dark\":%.2f}",
        light, dark);

    mqtt_publish(client,
        "sensor/room1",
        payload,
        strlen(payload),
        1,   // QoS1
        0,
        NULL,
        NULL);
}

int main() {

    stdio_init_all();

    adc_init();
    adc_gpio_init(LDR_PIN);

    /* Wi-Fi start */
    if (cyw43_arch_init()) return 1;

    cyw43_arch_enable_sta_mode();

    if (cyw43_arch_wifi_connect_timeout_ms(
        WIFI_SSID,
        WIFI_PASS,
        CYW43_AUTH_WPA2_AES_PSK,
        30000)) {
        printf("WiFi failed\n");
        return 1;
    }

    printf("WiFi connected\n");

    /* TLS configuration */
    struct altcp_tls_config *tls_config =
        altcp_tls_create_config_client(ca_cert, strlen(ca_cert));

    /* MQTT client */
    client = mqtt_client_new();

    struct mqtt_connect_client_info_t ci = {};
    ci.client_id = "pico2w-node";
    ci.client_user = "user";
    ci.client_pass = "password";

    ip_addr_t broker_ip;
    ipaddr_aton(MQTT_HOST, &broker_ip);

    mqtt_client_connect(
        client,
        &broker_ip,
        MQTT_PORT,
        mqtt_connection_cb,
        NULL,
        &ci);

    while (true) {

        float light = read_light();

        /* Edge Impulse */
        float features[1] = { light };

        signal_t signal;
        numpy::signal_from_buffer(features, 1, &signal);

        ei_impulse_result_t result;

        if (run_classifier(&signal, &result, false) == EI_IMPULSE_OK) {

            float dark = result.classification[1].value;

            if (dark > 0.8)
                cyw43_arch_gpio_put(LED_PIN, 1);
            else
                cyw43_arch_gpio_put(LED_PIN, 0);

            mqtt_send(light, dark);
        }

        sleep_ms(1000);
    }
}