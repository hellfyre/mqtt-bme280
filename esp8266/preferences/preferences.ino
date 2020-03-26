#include <EEPROM.h>

const String node_name = "arbeitszimmer";
const String wifi_ssid = "wifi";
const String wifi_pass = "secret";

int putString(int address, String value) {
    int i;
    for (i = 0; i < value.length(); i++) {
        EEPROM.write(address + i, value.charAt(i));
    }
    EEPROM.write(address + i, '\0');
    i++;

    return address + i;
}

int getString(int address, String &value) {
    char current;

    while (current != '\0') {
        current = EEPROM.read(address++);
        value = value + current;
    }

    return address;
}

void setup() {
    Serial.begin(115200);
    EEPROM.begin(512);

    Serial.println("Starting configuration");
    int address = 0;
    address = putString(address, node_name);
    Serial.println("Writing node_name " + node_name);
    address = putString(address, wifi_ssid);
    Serial.print("Writing wifi_ssid " + wifi_ssid);
    address = putString(address, wifi_pass);
    Serial.print("Writing wifi_pass " + wifi_pass);
    EEPROM.commit();
    Serial.println("Configuration done");
}

void loop() {
    int address = 0;
    String node_name = "";
    String wifi_ssid = "";
    String wifi_pass = "";

    address = getString(address, node_name);
    address = getString(address, wifi_ssid);
    address = getString(address, wifi_pass);
    Serial.print("node_name "); Serial.println(node_name);
    Serial.print("wifi_ssid "); Serial.println(wifi_ssid);
    Serial.print("wifi_pass "); Serial.println(wifi_pass);
    delay(1000);
}
