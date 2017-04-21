#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <PubSubClient.h>
#include <BME280I2C.h>
#include <Ticker.h>

#define OSWATCH_RESET_TIME 30
#define SC_DEBUG false

const char *ssid = "brrrrrr";
const char *password = "brrrrrr";
unsigned int pushcount = 0;
char hostString[16] = {0};
String topic;
WiFiClient espClient;
PubSubClient client(espClient);
BME280I2C bme(5, 5, 5, 1, 5, 2);
Ticker tickerOSWatch;

unsigned long lastmillis;

static unsigned long last_loop;

void ICACHE_RAM_ATTR osWatch(void) {
    unsigned long t = millis();
    unsigned long last_run = abs(t - last_loop);
    if(last_run >= (OSWATCH_RESET_TIME * 1000)) {
      // save the hit here to eeprom or to rtc memory if needed
        ESP.restart();  // normal reboot 
        //ESP.reset();  // hard reset
    }
}

void setup() {
  lastmillis = millis();
  last_loop = millis();
  tickerOSWatch.attach_ms(((OSWATCH_RESET_TIME / 3) * 1000), osWatch);
  Serial.begin(115200);
  delay(10);
  #if SC_DEBUG
  Serial.println("\r\nsetup()");
  #endif
  topic = "sensors/" + String(ESP.getChipId());
  sprintf(hostString, "ESP_%06X", ESP.getChipId());
  #if SC_DEBUG
  Serial.print("Hostname: ");
  Serial.println(hostString);
  #endif
  WiFi.hostname(hostString);

  WiFi.begin(ssid, password);
  unsigned long starttime = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    #if SC_DEBUG
    Serial.print(".");
    #endif
    unsigned long t = millis();
    if (t - starttime > 10000) {
      ESP.restart();
    }
  }
  last_loop = millis();
  #if SC_DEBUG
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  #endif
  
  if (!MDNS.begin(hostString)) {
    Serial.println("Error setting up MDNS responder!");
  }
  #if SC_DEBUG
  Serial.println("mDNS responder started");
  #endif
  MDNS.addService("esp", "tcp", 8080);
  #if SC_DEBUG
  Serial.println("Sending mDNS query");
  #endif
  int n = MDNS.queryService("mqtt", "tcp");
  #if SC_DEBUG
  Serial.println("mDNS query done");
  #endif
  last_loop = millis();
  if (n == 0) {
    Serial.println("no services found");
    ESP.restart();
  }
  else {
    #if SC_DEBUG
    Serial.print(n);
    Serial.println(" service found ");
    Serial.print(MDNS.hostname(0));
    Serial.print(" (");
    Serial.print(MDNS.IP(0));
    Serial.print(":");
    Serial.print(MDNS.port(0));
    Serial.println(")");
    #endif
    client.setServer(MDNS.IP(0), MDNS.port(0));
    
  }

  if (!bme.begin()) {
    Serial.println("Could not find BME280I2C sensor!");
    ESP.restart();
  }
  last_loop = millis();
  while (!client.connected()) {
    #if SC_DEBUG
    Serial.print("Attempting MQTT connection...");
    #endif
    // Attempt to connect
    if (client.connect("ESP8266Client")) {
      #if SC_DEBUG
      Serial.println("connected");
      #endif
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
  last_loop = millis();
  client.loop();
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
    if(pushcount%6 == 0){
    client.publish((topic + "/sys/RSSI").c_str(), String(WiFi.RSSI()).c_str());
    client.publish((topic + "/sys/uptime").c_str(), String(pushcount*5).c_str());
    client.publish((topic + "/sys/freeheap").c_str(), String(ESP.getFreeHeap()).c_str());
    }
    pushcount++;
  }
}
