#include <Adafruit_BME280.h>
#include <EnvironmentCalculations.h>
#include <esp_log.h>
#include <esp_task_wdt.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <SPI.h>
#include <WiFi.h>
#include <Wire.h>

extern "C" int rom_phy_get_vdd33();

#define uS_TO_S_FACTOR 1000000  /* Conversion factor for micro seconds to seconds */

/*********************************************
    Configuration
 *********************************************/

// Logging
#define LOG_LOCAL_LEVEL ESP_LOG_INFO /* Levels: ESP_LOG_{NONE, ERROR, WARN, INFO, DEBUG, VERBOSE} */
static const char* TAG = "weathernode";

// How long to sleep between MQTT publishes in seconds
#define TIME_TO_SLEEP 60

Adafruit_BME280 bme; // use I2C interface

// Global variables
WiFiClient wifi_client;
Preferences preferences;
PubSubClient mqtt_client(wifi_client);
String node_name;
String topic = String("sensors/");

void setup() {
    // Start the watch dog
    esp_task_wdt_init(10, true);
    esp_task_wdt_add(NULL);

    Serial.begin(115200);

    load_preferences();

    // Set up the topic based on the node name
    String host_name = node_name;
    topic += host_name;

    delay(10);
    // Feed the watchdog
    esp_task_wdt_reset();

    // Set wakeup time
    esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);

    start_i2c();
    // Feed the watchdog
    esp_task_wdt_reset();

    start_wifi(host_name);
    // Feed the watchdog
    esp_task_wdt_reset();

    start_mdns_service(host_name);
    // Feed the watchdog
    esp_task_wdt_reset();

    uint32_t ip_addr;
    uint16_t port;
    search_mdns(&ip_addr, &port);
    // Feed the watchdog
    esp_task_wdt_reset();

    start_mqtt(ip_addr, port, host_name);
    // Feed the watchdog
    esp_task_wdt_reset();

    ESP_EARLY_LOGD(TAG, "Setup done.\n");
}

void loop() {
    ESP_EARLY_LOGI(TAG, "Entering loop.");
    // Feed the watchdog
    esp_task_wdt_reset();
    mqtt_client.loop();

    if (!mqtt_client.connected() || WiFi.status() != WL_CONNECTED) {
        ESP_EARLY_LOGE(TAG, "For some reason we got disconnected!");
        ESP_EARLY_LOGE(TAG, "WiFi status: %s", translate_wifi_status(WiFi.status()).c_str());
        ESP_EARLY_LOGE(TAG, "MQTT status: %s", translate_mqtt_status(mqtt_client.state()).c_str());
        ESP_EARLY_LOGE(TAG, "Going back to sleep");
        esp_task_wdt_delete(NULL);
        esp_deep_sleep_start();
    }
    else {
        ESP_EARLY_LOGD(TAG, "Connected to MQTT server");
    }

    ESP_EARLY_LOGI(TAG, "Publishing data...");
    esp_task_wdt_reset();
    bme.takeForcedMeasurement();
    mqtt_client.publish((topic + "/humidity").c_str(), String(bme.readHumidity()).c_str());
    mqtt_client.publish((topic + "/temperature").c_str(), String(bme.readTemperature()).c_str());
    mqtt_client.publish((topic + "/pressure").c_str(), String(bme.readPressure()/100).c_str());
    mqtt_client.publish((topic + "/battery").c_str(), String(rom_phy_get_vdd33()/1000.0).c_str());
    delay(50);

    // Delete task watchdog and go to sleep
    esp_task_wdt_delete(NULL);
    ESP_EARLY_LOGI(TAG, "Going to sleep for %d seconds.", TIME_TO_SLEEP);
    esp_deep_sleep_start();
}
