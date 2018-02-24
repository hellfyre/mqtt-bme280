#include <BME280I2C.h>
#include <EnvironmentCalculations.h>
#include <esp_task_wdt.h>
#include <ESPmDNS.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <Wire.h>

#define uS_TO_S_FACTOR 1000000  /* Conversion factor for micro seconds to seconds */

/*********************************************
 * Configuration
 *********************************************/

#define DEBUG_OUTPUT true

// How long to sleep between MQTT publishes in seconds
#define TIME_TO_SLEEP 60

// WiFi data
const char *wifi_ssid = "somewifi";
const char *wifi_pass = "secret";

// This node's name
const String node_name = "node1";

WiFiClient wifi_client;
PubSubClient mqtt_client(wifi_client);

// The BME280 sensor configuration
BME280I2C::Settings settings(
    BME280::OSR_X16,
    BME280::OSR_X16,
    BME280::OSR_X16,
    BME280::Mode_Forced,
    BME280::StandbyTime_1000ms,
    BME280::Filter_8,
    BME280::SpiEnable_False,
    0x76 // I2C address. I2C specific.
);
BME280I2C bme(settings);

/*********************************************/

String topic = String("sensors/");
char chipid[13];

void start_i2c() {
    // Initialize I2C and environmental sensor BME280. Restart if the sensor fails.
    Wire.begin();
    if (!bme.begin()) {
        #ifdef DEBUG_OUTPUT
        Serial.println("Could not find BME280I2C sensor!");
        #endif
        ESP.restart();
    }
}

void start_wifi(String host_name) {
    // Set up WiFi
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(host_name.c_str());
    
    // Connect to the defined WiFi network, this might fail, but we give it about 10 seconds
    WiFi.begin(wifi_ssid, wifi_pass);
    unsigned long starttime = millis();
    Serial.printf("Connecting to Wifi %s", wifi_ssid);
    while (WiFi.status() != WL_CONNECTED) {
        delay(250);
        Serial.print(".");
        unsigned long t = millis();
        if (t - starttime > 10000) {
            ESP.restart();
        }
    }
    Serial.println("\r\nConnected!");
}

void start_mdns_service(String host_name) {
    // Initialize mDNS service
    esp_err_t err = mdns_init();
    if (err) {
        Serial.printf("MDNS Init failed: %d\r\n", err);
        ESP.restart();
    }

    // Set hostname
    mdns_hostname_set(host_name.c_str());
    
    // Set default instance
    mdns_instance_name_set("BME280 node");

    // Add service _esp._tcp, so we can find the node more easily
    mdns_service_add(NULL, "_esp", "_tcp", 8080, NULL, 0);
}

void search_mdns(uint32_t* ip_addr, uint16_t* port) {
    mdns_result_t * result = NULL;
    uint16_t timeout = 1000;

    while (!result && timeout <= 16000) {
        Serial.printf("Trying to find MQTT service with timeout %d milliseconds\r\n", timeout);
        esp_err_t err = mdns_query_ptr("_mqtt", "_tcp", timeout, 20,  &result);
        if (err) {
            Serial.println("Query Failed");
            ESP.restart();
        }
        timeout *= 2;
    }
    
    if(!result){
        Serial.printf("No results found. Going back to sleep for %d seconds.\r\n", TIME_TO_SLEEP);
        esp_deep_sleep_start();
    }
    else {
        Serial.println("Found results.");
    }
    
    mdns_ip_addr_t * address = NULL;
    while (result->ip_protocol != MDNS_IP_PROTOCOL_V4) {
        if (result->next) {
            result = result->next;
        }
        else {
            Serial.printf("Result set did not contain any IPV4 records. Going back to sleep for %d seconds.\r\n", TIME_TO_SLEEP);
            esp_deep_sleep_start();
        }
    }
    Serial.println("IPV4 record found.");
    
    address = result->addr;
    while (address->addr.type != MDNS_IP_PROTOCOL_V4) {
        if (address->next) {
            address = address->next;
        }
        else {
            Serial.printf("Record did not contain any IPV4 addresses. Going back to sleep for %d seconds.\r\n", TIME_TO_SLEEP);
            esp_deep_sleep_start();
        }
    }
    IPAddress ip_addr_obj = IPAddress(address->addr.u_addr.ip4.addr);
    Serial.printf("IPV4 address found: %d.%d.%d.%d:%d\r\n", ip_addr_obj[0], ip_addr_obj[1], ip_addr_obj[2], ip_addr_obj[3], result->port);
    *ip_addr = address->addr.u_addr.ip4.addr;
    *port = result->port;
}

void start_mqtt(uint32_t ip_addr, uint16_t port, String host_name) {
    mqtt_client.setServer(ip_addr, port);

    // Set up the MQTT client and connect it to the server
    Serial.print("Attempting to connect to MQTT server... ");
    while (!mqtt_client.connected()) {
        // Attempt to connect
        if (mqtt_client.connect(host_name.c_str())) {
            Serial.print("connected!");
            mqtt_client.publish((topic + "/humidity/unit").c_str(), "% RH", true);
            mqtt_client.publish((topic + "/temperature/unit").c_str(), "°C", true);
            mqtt_client.publish((topic + "/pressure/unit").c_str(), "Pa", true);
            mqtt_client.publish((topic + "/dew_point/unit").c_str(), "°C", true);
        } else {
            Serial.print("failed, rc=");
            Serial.println(mqtt_client.state());
            ESP.restart();
        }
    }
}

void setup() {
    // Start the watch dog
    esp_task_wdt_init(60, true);
    esp_task_wdt_add(NULL);
    
    sprintf(chipid, "%X", ESP.getEfuseMac());
    Serial.begin(115200);
    
    // Set up the topic based on the node name
    String host_name = node_name;
    if (node_name.length() == 0) {
        host_name = String("esp-") + String(chipid);
    }
    topic += host_name;
    
    delay(10);
    // Feed the watchdog
    esp_task_wdt_reset();
    
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
    
    // Set wakeup time
    esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
    
    Serial.println("\r\nSetup done.\r\n\n");
}

void loop() {
    Serial.println("Entering loop");
    // Feed the watchdog
    esp_task_wdt_reset();
    mqtt_client.loop();
    float temp(NAN), pres(NAN), hum(NAN);
    
    if (!mqtt_client.connected() || WiFi.status() != WL_CONNECTED) {
        Serial.println("For some reason we disconnected!");
        Serial.println("WiFi status: " + WiFi.status());
        Serial.println("MQTT status: " + mqtt_client.state());
        ESP.restart(); //TODO: go to sleep instead?
    }
    else {
        Serial.println("Connected to MQTT server");
    }

    bme.read(pres, temp, hum, BME280::TempUnit_Celsius, BME280::PresUnit_hPa);
    
    mqtt_client.publish((topic + "/humidity").c_str(), String(hum).c_str());
    mqtt_client.publish((topic + "/temperature").c_str(), String(temp).c_str());
    delay(50);
    
    mqtt_client.publish((topic + "/pressure").c_str(), String(pres).c_str());
    mqtt_client.publish((topic + "/dew_point").c_str(), String(EnvironmentCalculations::DewPoint(temp, hum, BME280::TempUnit_Celsius)).c_str());
    delay(50);  
    
    // Go to sleep
    Serial.printf("Going to sleep for %d seconds.\r\n", TIME_TO_SLEEP);
    esp_deep_sleep_start();
}
