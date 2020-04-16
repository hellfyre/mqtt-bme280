// WiFi data
String wifi_ssid = "";
String wifi_pass = "";

int getString(int address, String &value) {
    char current;

    while (current != '\0') {
        current = EEPROM.read(address++);
        value = value + current;
    }

    return address;
}

void load_preferences() {
    int address = 0;

    EEPROM.begin(512);
    address = getString(address, node_name);
    address = getString(address, wifi_ssid);
    address = getString(address, wifi_pass);
    
    log_info("I am node \"" + node_name + "\"");
}

void start_i2c() {
    // Initialize I2C and environmental sensor BME280. Restart if the sensor fails.
    ESP.wdtFeed();
    Wire.begin();
    if (!bme.begin(0x76, &Wire)) {
        log_error("Could not find BME280I2C sensor!");
        ESP.restart();
    }

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
    ESP.wdtFeed();
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.hostname(host_name.c_str());

    // Connect to the defined WiFi network. This might fail, we give it about 10 seconds
    WiFi.begin(wifi_ssid.c_str(), wifi_pass.c_str());
    unsigned long starttime = millis();
    log_info("Connecting to Wifi " + wifi_ssid + " ...");
    while (WiFi.status() != WL_CONNECTED) {
        delay(250);
        log_debug(".");
        unsigned long t = millis();
        if (t - starttime > 10000) {
            log_warn("Could not connect to WiFi. Retrying in " + String(TIME_TO_SLEEP) + " seconds");
            delay(1000 * TIME_TO_SLEEP);
            ESP.restart();
        }
        ESP.wdtFeed();
    }
    log_info("Connected");
}

void start_mdns_service(String host_name) {
    // Initialize mDNS service
    ESP.wdtFeed();
    if (!MDNS.begin(host_name)) {
        log_error("MDNS Init failed");
        ESP.restart();
    }

    // Set default instance
    MDNS.setInstanceName("BME280 node");

    // Add service _esp._tcp, so we can find the node more easily
    MDNS.addService("_esp", "_tcp", 8080);
}

void search_mdns(uint32_t* ip_addr, uint16_t* port) {
    int numResults = 0;
    int numRetries = 3;

    ESP.wdtFeed();

    while (numResults <= 0 && numRetries > 0) {
        log_info("Trying to find MQTT service");
        numResults = MDNS.queryService("_mqtt", "_tcp");
        if (numResults == 0) {
            log_error("Could not find MQTT server");
        }
        numRetries--;
    }

    if (!numResults) {
        log_warn("No results found. Retrying in " + String(TIME_TO_SLEEP) + " seconds");
        delay(1000 * TIME_TO_SLEEP);
        ESP.restart();
    }
    else {
        log_debug("Found results");
    }

    IPAddress address = NULL;
    for (int i = 0; i <= numResults; i++) {
        address = MDNS.IP(i);
        if (address.isV4()) {
            *ip_addr = address;
            *port = MDNS.port(i);
            log_info("IPV4 address found: " + String(address[0]) + "." + String(address[1]) + "." + String(address[2]) + "." + String(address[3]) + ":" + String(*port));
            break;
        }
        else {
            log_warn("Not an IPv4 address");
        }
    }
}

void start_mqtt(uint32_t ip_addr, uint16_t port, String host_name) {
    ESP.wdtFeed();
    mqtt_client.setServer(ip_addr, port);

    // Set up the MQTT client and connect it to the server
    log_info("Attempting to connect to MQTT server... ");
    while (!mqtt_client.connected()) {
        // Attempt to connect
        if (mqtt_client.connect(host_name.c_str())) {
            log_info("Connected");
            // This is a very dirty hack to concatenate two strings. Apparently
            // there is a bug in String::concat() that the nullbyte of the left
            // operand is not removed upon concatenation. I'm using snprintf(),
            // because I'm too lazy to report or fix the problem in String::concat().
            char humidity_topic[64];
            char temperature_topic[64];
            char pressure_topic[64];
            memset(humidity_topic, '\0', 64);
            memset(temperature_topic, '\0', 64);
            memset(pressure_topic, '\0', 64);
            snprintf(humidity_topic, 64, "%s/%s/%s", topic.c_str(), "humidity", "unit");
            snprintf(temperature_topic, 64, "%s/%s/%s", topic.c_str(), "temperature", "unit");
            snprintf(pressure_topic, 64, "%s/%s/%s", topic.c_str(), "pressure", "unit");
            mqtt_client.publish(humidity_topic, "% RH", true);
            mqtt_client.publish(temperature_topic, "°C", true);
            mqtt_client.publish(pressure_topic, "hPa", true);
        } else {
            String error_msg = translate_mqtt_status(mqtt_client.state());
            log_error("Connection to MQTT server could not be established, rc = " + error_msg + ", retrying in " + String(TIME_TO_SLEEP) + " seconds.");
            delay(1000 * TIME_TO_SLEEP);
            ESP.restart();
        }
    }
}
