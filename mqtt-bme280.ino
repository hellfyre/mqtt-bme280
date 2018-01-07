#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <PubSubClient.h>
#include <BME280I2C.h>
#include <EnvironmentCalculations.h>
#include <Ticker.h>
#include <Wire.h>

//timeout of the loop watchdog
#define OSWATCH_RESET_TIME 30
//interval in ms between publishes
#define PUBLISH_INTERVAL 10000
//enables sketch serial debug
#define SC_DEBUG false

// wifi ssid
const char *ssid = "brrrrr";
// wifi password
const char *password = "brrrrr";
// node name
const String node_name = "";

unsigned int pushcount = 0;
char hostString[16] = {0};
String topic;
unsigned long lastmillis;
volatile unsigned long last_loop;

WiFiClient espClient;
PubSubClient client(espClient);

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

Ticker tickerOSWatch;

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

  //attach the interrupt for the software watchdog, this will restart the ESP if the code hangs for ~30 seconds
  tickerOSWatch.attach_ms(((OSWATCH_RESET_TIME / 3) * 1000), osWatch);
  
  Serial.begin(115200);
  delay(10);
  #if SC_DEBUG
  Serial.println("\r\nsetup()");
  #endif
  Wire.begin();

  //initialize environmental sensor and restart if it fails
  if (!bme.begin()) {
    Serial.println("Could not find BME280I2C sensor!");
    ESP.restart();
  }

  //setup the hostname and topic based on the ESP's chip-id
  if(node_name.length()>0){
    topic = "sensors/" + node_name;  
  } else {
    topic = "sensors/" + String(ESP.getChipId());  
  }
  
  sprintf(hostString, "ESP_%06X", ESP.getChipId());
  #if SC_DEBUG
  Serial.print("Hostname: ");
  Serial.println(hostString);
  #endif
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.hostname(hostString);

  #if SC_DEBUG
  byte mac[6];
  WiFi.macAddress(mac);
  Serial.printf("\r\nConnecting to %s\r\n", ssid);
  Serial.printf("MAC: %x:%x:%x:%x:%x:%x\r\n", mac[5], mac[4], mac[3], mac[2], mac[1], mac[0]);
  #endif

  //connect to the defined WiFi network, this might fail, but we give it about 10 seconds
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

  //start up the mDNS service and announce our presence (nice to have for debugging purposes)
  if (!MDNS.begin(hostString)) {
    Serial.println("Error setting up MDNS responder!");
  }
  #if SC_DEBUG
  Serial.println("mDNS responder started");
  #endif
  MDNS.addService("esp", "tcp", 8080);

  //query for an announced mqtt serice on the network
  #if SC_DEBUG
  Serial.println("Sending mDNS query");
  #endif
  int n = MDNS.queryService("mqtt", "tcp");
  #if SC_DEBUG
  Serial.println("mDNS query done");
  #endif
  last_loop = millis();

  //if a service has been found connect to it (this will always connect to the first responding)
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
  last_loop = millis();

  //setup the mqtt client and connect it to the server
  while (!client.connected()) {
    #if SC_DEBUG
    Serial.print("Attempting MQTT connection...");
    #endif
    // Attempt to connect
    //if (client.connect("sensor_" + ESP.getChipId())) {
    if (client.connect(node_name.c_str())) {
      #if SC_DEBUG
      Serial.print("connected");
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
  lastmillis = millis() + PUBLISH_INTERVAL +1;
  last_loop = millis();
}

void loop() {
  last_loop = millis();
  client.loop();
  
  float temp(NAN), pres(NAN), hum(NAN);
  if (last_loop > lastmillis + PUBLISH_INTERVAL) {
    lastmillis = millis();
    if (!client.connected() || WiFi.status() != WL_CONNECTED) {
      Serial.println("For some reason we disconnected!");
      Serial.println("WiFi status: " + WiFi.status());
      Serial.println("MQTT status: " + client.state());
      ESP.restart();
    }
    bme.read(pres, temp, hum, BME280::TempUnit_Celsius, BME280::PresUnit_hPa);
    temp = temp - 0.15;
    client.publish((topic + "/humidity").c_str(), String(hum).c_str());
    client.publish((topic + "/temperature").c_str(), String(temp).c_str());
    delay(50);
    client.publish((topic + "/pressure").c_str(), String(pres).c_str());
    client.publish((topic + "/dew_point").c_str(), String(EnvironmentCalculations::DewPoint(temp, hum, BME280::TempUnit_Celsius)).c_str());
    delay(50);
    if(pushcount%6 == 0){
    client.publish((topic + "/sys/RSSI").c_str(), String(WiFi.RSSI()).c_str());
    client.publish((topic + "/sys/uptime").c_str(), String(pushcount*5).c_str());
    client.publish((topic + "/sys/freeheap").c_str(), String(ESP.getFreeHeap()).c_str());
    #if SC_DEBUG
    Serial.print("\r\nAll is well");
    #endif
    }
    #if SC_DEBUG
    Serial.print(".");
    #endif
    pushcount++;
  }
  delay(50);
}
