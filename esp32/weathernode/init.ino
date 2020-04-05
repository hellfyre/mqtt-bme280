// WiFi data
String wifi_ssid;
String wifi_pass;

void load_preferences() {
    // node_name, wifi_ssid and wifi_pass are global
    preferences.begin("prefs", true);
    node_name = preferences.getString("node_name", "unknown_node");
    wifi_ssid = preferences.getString("wifi_ssid", "wifi");
    wifi_pass = preferences.getString("wifi_pass", "secret");
    preferences.end();

    ESP_EARLY_LOGI(TAG, "I am node \"%s\"", node_name.c_str());
}

float read_battery_voltage() {
    float voltage = 0;
    btStart();
    voltage = ((float) rom_phy_get_vdd33()) / 1000.0;
    btStop();

    return voltage;
}

void publish_data(float voltage) {
    // bme is global
    ESP_EARLY_LOGI(TAG, "Publishing data...");
    mqtt_client.publish((topic + "/humidity").c_str(), String(bme.readHumidity()).c_str());
    mqtt_client.publish((topic + "/temperature").c_str(), String(bme.readTemperature()).c_str());
    mqtt_client.publish((topic + "/pressure").c_str(), String(bme.readPressure() / 100).c_str());
    mqtt_client.publish((topic + "/battery").c_str(), String(voltage).c_str());
    delay(50);
}

void initialize_bme() {
    // Initialize I2C and environmental sensor BME280. Restart if the sensor fails.
    Wire.begin();
    uint16_t counter = 3;
    bool bme_found = false;
    while (counter > 0 && !(bme_found = bme.begin(0x76, &Wire))) {
        ESP_EARLY_LOGE(TAG, "Could not find BME280I2C sensor, retrying ...");
        counter--;
        delay(10);
    }

    if (!bme_found) {
        ESP_EARLY_LOGE(TAG, "Could not find BME280I2C sensor, restarting ...");
        ESP.restart();
    }
    ESP_EARLY_LOGD(TAG, "Found BME280I2C sensor");

    bme.setSampling(
        Adafruit_BME280::MODE_FORCED,
        Adafruit_BME280::SAMPLING_X8, // temperature
        Adafruit_BME280::SAMPLING_X8, // pressure
        Adafruit_BME280::SAMPLING_X8, // humidity
        Adafruit_BME280::FILTER_X4
    );
}

void start_wifi(String host_name) {
    // Set up WiFi
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(host_name.c_str());

    // Connect to the defined WiFi network. This might fail, we give it about 10 seconds
    WiFi.begin(wifi_ssid.c_str(), wifi_pass.c_str());
    unsigned long starttime = millis();
    ESP_EARLY_LOGI(TAG, "Connecting to Wifi %s ...", wifi_ssid.c_str());
    while (WiFi.status() != WL_CONNECTED) {
        esp_task_wdt_reset();
        delay(500);
        ESP_EARLY_LOGD(TAG, ".");
        unsigned long t = millis();
        if (t - starttime > 10000) {
            ESP_EARLY_LOGW(TAG, "Could not connect to WiFi. Going back to sleep for %d seconds.", TIME_TO_SLEEP);
            esp_task_wdt_delete(NULL);
            esp_deep_sleep_start();
        }
    }
    ESP_EARLY_LOGI(TAG, "Connected!");
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

void search_mdns(uint32_t& ip_addr, uint16_t& port) {
    mdns_result_t * result = NULL;
    uint16_t timeout = 2000;

    while (!result && timeout <= 8000) {
        esp_task_wdt_reset();
        ESP_EARLY_LOGI(TAG, "Trying to find MQTT service with timeout %d milliseconds", timeout);
        esp_err_t err = mdns_query_ptr("_mqtt", "_tcp", timeout, 20,  &result);
        if (err) {
            ESP_EARLY_LOGE(TAG, "MDNS query failed");
            ESP.restart();
        }
        timeout *= 2;
    }

    if (!result) {
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

    if (LOG_LOCAL_LEVEL == ESP_LOG_DEBUG) {
        // Convert uint32_t IP address to IPAddress object for logging purposes
        IPAddress ip_addr_obj = IPAddress(address->addr.u_addr.ip4.addr);
        ESP_EARLY_LOGI(TAG, "IPV4 address found: %d.%d.%d.%d:%d", ip_addr_obj[0], ip_addr_obj[1], ip_addr_obj[2], ip_addr_obj[3], result->port);
    }

    ip_addr = address->addr.u_addr.ip4.addr;
    port = result->port;
}

void start_mqtt(uint32_t ip_addr, uint16_t port, String host_name) {
    mqtt_client.setServer(ip_addr, port);

    // Set up the MQTT client and connect it to the server
    ESP_EARLY_LOGI(TAG, "Attempting to connect to MQTT server... ");
    while (!mqtt_client.connected()) {
        // Attempt to connect
        if (mqtt_client.connect(host_name.c_str())) {
            ESP_EARLY_LOGI(TAG, "Connected!");

            // Publish units under persistent topics
            mqtt_client.publish((topic + "/humidity/unit").c_str(), "% RH", true);
            mqtt_client.publish((topic + "/temperature/unit").c_str(), "°C", true);
            mqtt_client.publish((topic + "/pressure/unit").c_str(), "hPa", true);
            mqtt_client.publish((topic + "/battery/unit").c_str(), "V", true);
        } else {
            String error_msg = translate_mqtt_status(mqtt_client.state());
            ESP_EARLY_LOGE(TAG, "Connection to MQTT server could not be established, rc=%s, Going back to sleep for %d seconds.", error_msg.c_str(), TIME_TO_SLEEP);
            esp_task_wdt_delete(NULL);
            esp_deep_sleep_start();
        }
    }
}
