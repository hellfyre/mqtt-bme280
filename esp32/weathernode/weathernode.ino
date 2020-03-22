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

#define uS_TO_S_FACTOR 1000000  /* Conversion factor for micro seconds to seconds */

/*********************************************
 * Configuration
 *********************************************/

#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG /* Levels: ESP_LOG_{NONE, ERROR, WARN, INFO, DEBUG, VERBOSE} */
static const char* TAG = "weathernode";

// This node's name
String node_name = TAG;

// How long to sleep between MQTT publishes in seconds
#define TIME_TO_SLEEP 60

WiFiClient wifi_client;
Preferences preferences;
PubSubClient mqtt_client(wifi_client);

Adafruit_BME280 bme; // use I2C interface
Adafruit_Sensor *bme_temp = bme.getTemperatureSensor();
Adafruit_Sensor *bme_pressure = bme.getPressureSensor();
Adafruit_Sensor *bme_humidity = bme.getHumiditySensor();

String topic = String("sensors/");
char chipid[13];

void setup() {
    if (LOG_LOCAL_LEVEL == ESP_LOG_VERBOSE) {
        // Be really verbose, i.e.: print log messages of all components
        esp_log_level_set("*", LOG_LOCAL_LEVEL);
    }
    // Start the watch dog
    esp_task_wdt_init(10, true);
    esp_task_wdt_add(NULL);

    sprintf(chipid, "%X", ESP.getEfuseMac());
    Serial.begin(115200);

    load_preferences();

    // Set up the topic based on the node name
    String host_name = node_name;
    if (node_name.length() == 0) {
        host_name = String("esp-") + String(chipid);
    }
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
    ESP_EARLY_LOGE(TAG, "Entering loop.");
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

    sensors_event_t temperature_event, pressure_event, humidity_event;
    bme_temp->getEvent(&temperature_event);
    bme_pressure->getEvent(&pressure_event);
    bme_humidity->getEvent(&humidity_event);
    ESP_EARLY_LOGD(TAG, "Publishing data...");

    mqtt_client.publish((topic + "/humidity").c_str(), String(humidity_event.relative_humidity).c_str());
    mqtt_client.publish((topic + "/temperature").c_str(), String(temperature_event.temperature).c_str());
    mqtt_client.publish((topic + "/pressure").c_str(), String(pressure_event.pressure).c_str());
    delay(50);

    // Delete task watchdog and go to sleep
    esp_task_wdt_delete(NULL);
    ESP_EARLY_LOGI(TAG, "Going to sleep for %d seconds.", TIME_TO_SLEEP);
    esp_deep_sleep_start();
}