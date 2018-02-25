#include <BME280I2C.h>
#include <EnvironmentCalculations.h>
#include <esp_log.h>
#include <esp_task_wdt.h>
#include <ESPmDNS.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <Wire.h>

#define uS_TO_S_FACTOR 1000000  /* Conversion factor for micro seconds to seconds */

/*********************************************
 * Configuration
 *********************************************/

#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG /* Levels: ESP_LOG_{NONE, ERROR, WARN, INFO, DEBUG, VERBOSE} */
static const char* TAG = "BME280";

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

String translate_mqtt_status(uint8_t status) {
    switch (status) {
        case MQTT_CONNECTION_TIMEOUT:
            return String("Connection timeout");
        case MQTT_CONNECTION_LOST:
            return String("Connection lost");
        case MQTT_CONNECT_FAILED:
            return String("Connect failed");
        case MQTT_DISCONNECTED:
            return String("Disconnected");
        case MQTT_CONNECTED:
            return String("Connected");
        case MQTT_CONNECT_BAD_PROTOCOL:
            return String("Bad protocol");
        case MQTT_CONNECT_BAD_CLIENT_ID:
            return String("Bad client ID");
        case MQTT_CONNECT_UNAVAILABLE:
            return String("Unavailable");
        case MQTT_CONNECT_BAD_CREDENTIALS:
            return String("Bad credentials");
        case MQTT_CONNECT_UNAUTHORIZED:
            return String("Unauthorized");
        default:
            return String("Unknown error");
    }
}
String translate_wifi_status(uint8_t status) {
    switch (status) {
        case WL_NO_SHIELD:
            return String("No WiFi shield installed");
        case WL_IDLE_STATUS:
            return String("Idle");
        case WL_NO_SSID_AVAIL:
            return String("Available");
        case WL_SCAN_COMPLETED:
            return String("Scan completed");
        case WL_CONNECTED:
            return String("Connected");
        case WL_CONNECT_FAILED:
            return String("Connect failed");
        case WL_CONNECTION_LOST:
            return String("Connection lost");
        case WL_DISCONNECTED:
            return String("Disconnected");
        default:
            return String("Unknown error");
    }
}

void start_i2c() {
    // Initialize I2C and environmental sensor BME280. Restart if the sensor fails.
    Wire.begin();
    if (!bme.begin()) {
        ESP_EARLY_LOGE(TAG, "Could not find BME280I2C sensor!");
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
    ESP_EARLY_LOGI(TAG, "Connecting to Wifi %s ...", wifi_ssid);
    while (WiFi.status() != WL_CONNECTED) {
        delay(250);
        ESP_EARLY_LOGD(TAG, ".");
        unsigned long t = millis();
        if (t - starttime > 10000) {
            ESP.restart();
        }
    }
    ESP_EARLY_LOGI(TAG, " Connected!");
}

void start_mdns_service(String host_name) {
    // Initialize mDNS service
    esp_err_t err = mdns_init();
    if (err) {
        ESP_EARLY_LOGE(TAG, "MDNS Init failed: %d", err);
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
    uint16_t timeout = 2000;

    while (!result && timeout <= 8000) {
        ESP_EARLY_LOGI(TAG, "Trying to find MQTT service with timeout %d milliseconds", timeout);
        esp_err_t err = mdns_query_ptr("_mqtt", "_tcp", timeout, 20,  &result);
        if (err) {
            ESP_EARLY_LOGE(TAG, "MDNS query failed");
            ESP.restart();
        }
        timeout *= 2;
    }
    
    if(!result){
        ESP_EARLY_LOGW(TAG, "No results found. Going back to sleep for %d seconds.", TIME_TO_SLEEP);
        esp_task_wdt_delete(NULL);
        esp_deep_sleep_start();
    }
    else {
        ESP_EARLY_LOGD(TAG, "Found results.");
    }
    
    mdns_ip_addr_t * address = NULL;
    while (result->ip_protocol != MDNS_IP_PROTOCOL_V4) {
        if (result->next) {
            result = result->next;
        }
        else {
            ESP_EARLY_LOGW(TAG, "Result set did not contain any IPV4 records. Going back to sleep for %d seconds.", TIME_TO_SLEEP);
            esp_task_wdt_delete(NULL);
            esp_deep_sleep_start();
        }
    }
    ESP_EARLY_LOGD(TAG, "IPV4 record found.");
    
    address = result->addr;
    while (address->addr.type != MDNS_IP_PROTOCOL_V4) {
        if (address->next) {
            address = address->next;
        }
        else {
            ESP_EARLY_LOGW(TAG, "Record did not contain any IPV4 addresses. Going back to sleep for %d seconds.", TIME_TO_SLEEP);
            esp_task_wdt_delete(NULL);
            esp_deep_sleep_start();
        }
    }
    IPAddress ip_addr_obj = IPAddress(address->addr.u_addr.ip4.addr);
    ESP_EARLY_LOGI(TAG, "IPV4 address found: %d.%d.%d.%d:%d", ip_addr_obj[0], ip_addr_obj[1], ip_addr_obj[2], ip_addr_obj[3], result->port);
    *ip_addr = address->addr.u_addr.ip4.addr;
    *port = result->port;
}

void start_mqtt(uint32_t ip_addr, uint16_t port, String host_name) {
    mqtt_client.setServer(ip_addr, port);

    // Set up the MQTT client and connect it to the server
    ESP_EARLY_LOGI(TAG, "Attempting to connect to MQTT server... ");
    while (!mqtt_client.connected()) {
        // Attempt to connect
        if (mqtt_client.connect(host_name.c_str())) {
            ESP_EARLY_LOGI(TAG, "connected!");
            mqtt_client.publish((topic + "/humidity/unit").c_str(), "% RH", true);
            mqtt_client.publish((topic + "/temperature/unit").c_str(), "°C", true);
            mqtt_client.publish((topic + "/pressure/unit").c_str(), "Pa", true);
            mqtt_client.publish((topic + "/dew_point/unit").c_str(), "°C", true);
        } else {
            String error_msg = String(mqtt_client.state());
            if (LOG_LOCAL_LEVEL > 0) {
                error_msg = translate_mqtt_status(mqtt_client.state());
            }
            ESP_EARLY_LOGE(TAG, "Connection to MQTT server could not be established, rc=%s", error_msg);
            ESP.restart();
        }
    }
}

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
    
    ESP_EARLY_LOGD(TAG, "Setup done.\n");
}

void loop() {
    ESP_EARLY_LOGE(TAG, "Entering loop.");
    // Feed the watchdog
    esp_task_wdt_reset();
    mqtt_client.loop();
    float temp(NAN), pres(NAN), hum(NAN);
    
    if (!mqtt_client.connected() || WiFi.status() != WL_CONNECTED) {
        ESP_EARLY_LOGE(TAG, "For some reason we got disconnected!");
        ESP_EARLY_LOGE(TAG, "WiFi status: %s", translate_wifi_status(WiFi.status()));
        ESP_EARLY_LOGE(TAG, "MQTT status: %s", translate_mqtt_status(mqtt_client.state()));
        ESP_EARLY_LOGE(TAG, "Going back to sleep");
        esp_task_wdt_delete(NULL);
        esp_deep_sleep_start();
    }
    else {
        ESP_EARLY_LOGD(TAG, "Connected to MQTT server");
    }

    bme.read(pres, temp, hum, BME280::TempUnit_Celsius, BME280::PresUnit_hPa);
    ESP_EARLY_LOGD(TAG, "Publishing data...");
    
    mqtt_client.publish((topic + "/humidity").c_str(), String(hum).c_str());
    mqtt_client.publish((topic + "/temperature").c_str(), String(temp).c_str());
    delay(50);
    
    mqtt_client.publish((topic + "/pressure").c_str(), String(pres).c_str());
    mqtt_client.publish((topic + "/dew_point").c_str(), String(EnvironmentCalculations::DewPoint(temp, hum, BME280::TempUnit_Celsius)).c_str());
    delay(50);  
    
    // Delete task watchdog and go to sleep
    esp_task_wdt_delete(NULL);
    ESP_EARLY_LOGI(TAG, "Going to sleep for %d seconds.", TIME_TO_SLEEP);
    esp_deep_sleep_start();
}
