#include <Adafruit_BME280.h>
#include <EnvironmentCalculations.h>
#include <ESP8266mDNS.h>
#include <ESP8266WiFi.h>
#include <EEPROM.h>
#include <PubSubClient.h>
#include <Wire.h>

#define uS_TO_S_FACTOR 1000000  /* Conversion factor for micro seconds to seconds */

/*********************************************
    Configuration
 *********************************************/

// Logging
#define LOG_LEVEL LOG_LEVEL_INFO /* Levels: LOG_LEVEL_{NONE, ERROR, WARN, INFO, DEBUG, VERBOSE} */

// How long to sleep between MQTT publishes in seconds
#define TIME_TO_SLEEP 60

Adafruit_BME280 bme; // use I2C interface
Adafruit_Sensor *bme_temp = bme.getTemperatureSensor();
Adafruit_Sensor *bme_pressure = bme.getPressureSensor();
Adafruit_Sensor *bme_humidity = bme.getHumiditySensor();

// Global variables
WiFiClient wifi_client;
PubSubClient mqtt_client(wifi_client);
String node_name;
String host_name;
String topic = String("sensors/");

void setup() {
    Serial.begin(115200);

    load_preferences();

    // Set up the topic based on the node name
    host_name = node_name;
    topic += host_name;
    delay(10);

    start_i2c();
    start_wifi(host_name);
    start_mdns_service(host_name);
    
    uint32_t ip_addr;
    uint16_t port;
    search_mdns(&ip_addr, &port);
    start_mqtt(ip_addr, port, host_name);

    log_debug("Setup done");
}

void loop() {
    log_info("Entering loop");
    ESP.wdtFeed();
    mqtt_client.loop();

    if (WiFi.status() != WL_CONNECTED) {
        log_warn("Reconnecting to WiFi");
        start_wifi(host_name);
    }
    if (!mqtt_client.connected()) {
        log_warn("Reconnecting to MQTT");
        uint32_t ip_addr;
        uint16_t port;
        search_mdns(&ip_addr, &port);
        start_mqtt(ip_addr, port, host_name);
    }
    else {
        log_debug("Connected to MQTT server");
    }

    sensors_event_t temperature_event, pressure_event, humidity_event;
    bme_temp->getEvent(&temperature_event);
    bme_pressure->getEvent(&pressure_event);
    bme_humidity->getEvent(&humidity_event);
    log_info("Publishing data...");

    ESP.wdtFeed();
    mqtt_client.publish((topic + "/humidity").c_str(), String(humidity_event.relative_humidity).c_str());
    mqtt_client.publish((topic + "/temperature").c_str(), String(temperature_event.temperature).c_str());
    mqtt_client.publish((topic + "/pressure").c_str(), String(pressure_event.pressure).c_str());
    delay(50);

    // Go to sleep
    log_info("Done publishing, going to sleep for " + String(TIME_TO_SLEEP) + " seconds");
    delay(1000 * TIME_TO_SLEEP);
}