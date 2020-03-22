#include <Preferences.h>

const String node_name = "arbeitszimmer";
const String wifi_ssid = "wifi";
const String wifi_pass = "secret";

Preferences preferences;

void setup() {
    Serial.begin(115200);

    Serial.println("Starting configuration");
    preferences.begin("prefs", false);
    preferences.putString("node_name", node_name);
    Serial.print("Writing node_name "); Serial.println(node_name);
    preferences.putString("wifi_ssid", wifi_ssid);
    Serial.print("Writing wifi_ssid "); Serial.println(wifi_ssid);
    preferences.putString("wifi_pass", wifi_pass);
    Serial.print("Writing wifi_pass ") ; Serial.println(wifi_pass);
    preferences.end();
    Serial.println("Configuration done");
}

void loop() {
    preferences.begin("prefs", true);
    Serial.print("node_name "); Serial.println(preferences.getString("node_name", "foo"));
    Serial.print("wifi_ssid "); Serial.println(preferences.getString("wifi_ssid", "bar"));
    Serial.print("wifi_pass "); Serial.println(preferences.getString("wifi_pass", "baz"));
    preferences.end();
    delay(1000);
}
