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
    topic += node_name;
    delay(10);

    // Set wakeup time
    esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);

    initialize_bme();

    ESP_EARLY_LOGD(TAG, "Setup done.\n");
}

void loop() {
    ESP_EARLY_LOGD(TAG, "Entering loop.");

    // Take measurements; measure voltage before enabling WiFi
    float voltage = read_battery_voltage();
    bme.takeForcedMeasurement();

    uint32_t ip_addr;
    uint16_t port;

    start_wifi(node_name);
    start_mdns_service(node_name);
    search_mdns(ip_addr, port);
    start_mqtt(ip_addr, port, node_name);
    publish_data(voltage);
    WiFi.disconnect();

    // Delete task watchdog and go to sleep
    esp_task_wdt_delete(NULL);
    ESP_EARLY_LOGI(TAG, "Going to sleep for %d seconds.", TIME_TO_SLEEP);
    esp_deep_sleep_start();
}
