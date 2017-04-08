#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <PubSubClient.h>
#include <BME280I2C.h>


const char* ssid     = "Stratum0";
const char* password = "sie wissen schon";
char hostString[16] = {0};
String topic;
WiFiClient espClient;
PubSubClient client(espClient);
BME280I2C bme(5, 5, 5, 1, 5, 2);
unsigned long lastmillis;

void setup() {
  lastmillis = millis();
  Serial.begin(115200);
  delay(100);
  Serial.println("\r\nsetup()");
  topic = "sensors/" + String(ESP.getChipId());
  sprintf(hostString, "ESP_%06X", ESP.getChipId());
  Serial.print("Hostname: ");
  Serial.println(hostString);
  WiFi.hostname(hostString);

  WiFi.begin(ssid, password);
  unsigned long starttime = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
    if (millis() - starttime > 10000) {
      ESP.restart();
    }
  }
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  if (!MDNS.begin(hostString)) {
    Serial.println("Error setting up MDNS responder!");
  }
  Serial.println("mDNS responder started");
  MDNS.addService("esp", "tcp", 8080);

  Serial.println("Sending mDNS query");
  int n = MDNS.queryService("mqtt", "tcp");
  Serial.println("mDNS query done");
  if (n == 0) {
    Serial.println("no services found");
    ESP.restart();
  }
  else {
    Serial.print(n);
    Serial.println(" service found ");
    Serial.print(MDNS.hostname(0));
    Serial.print(" (");
    Serial.print(MDNS.IP(0));
    client.setServer(MDNS.IP(0), MDNS.port(0));
    Serial.print(":");
    Serial.print(MDNS.port(0));
    Serial.println(")");
  }

  while (!bme.begin()) {
    Serial.println("Could not find BME280I2C sensor!");
    ESP.restart();
  }
  
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect("ESP8266Client")) {
      Serial.println("connected");
      client.publish((topic + "/humidity/unit").c_str(), "% RH", true);
      client.publish((topic + "/temperature/unit").c_str(), "°C", true);
      client.publish((topic + "/pressure/unit").c_str(), "Pa", true);
      client.publish((topic + "/dew_point/unit").c_str(), "°C", true);
    } else {
      Serial.print("failed, rc=");
      Serial.println(client.state());
      ESP.restart();
    }
  }
}

void loop() {
  float temp(NAN), pres(NAN), hum(NAN);
  if (millis() > lastmillis + 5000) {
    lastmillis = millis();
    if (!client.connected() || WiFi.status() != WL_CONNECTED) {
      ESP.restart();
    }
    bme.read(pres, temp, hum, true, 0x00);
    client.publish((topic + "/humidity").c_str(), String(hum).c_str());
    client.publish((topic + "/temperature").c_str(), String(temp).c_str());
    client.publish((topic + "/pressure").c_str(), String(pres).c_str());
    client.publish((topic + "/dew_point").c_str(), String(bme.dew(temp, hum, true)).c_str());
  }
}
