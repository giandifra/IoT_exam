#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager
#include <ESP8266WebServer.h>
#include "Ticker.h"
#include <ArduinoJson.h>
#include <ESP8266mDNS.h>
#include "display_helper.h"
#include <InfluxDbClient.h>   /* per libreria di comunicazione con InfluxDB */
#include <InfluxDbCloud.h>    /* per gestire il token di sicurezza */

#define ONE_HOUR 3600000UL
#define TRIGGER_PIN D0
#define ONE_WIRE_PIN D4
#define RELAY_TEMP D3

//#define SCREEN_WIDTH 128 // OLED display width, in pixels
//#define SCREEN_HEIGHT 64 // OLED display height, in pixels

/* costanti per le connesioni */

//#define WIFI_SSID       "esercitazioni"
//#define WIFI_PASSWORD   "uniurbiot2021"

#define INFLUXDB_URL    "http://93.186.254.118:8086"
#define INFLUXDB_ORG    "uniurb"
#define INFLUXDB_BUCKET "test"
#define INFLUXDB_TOKEN "7q44Rz0f0IZYM4SYguqyPB5RPafXPEagZUpRuIUBp3aoDT3HVQzFg5c0Hg_RY8Khk8cH8MjuApdyQsKrFyaF4w=="
#define _measurement "monitor_task"
#define host "ESP_GM_Di_Francesco"
#define location "Sant'Egidio alla Vibrata"
#define room "camera"

/* un'istanza di InlfuxDbClient */
InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN,
                      InfluxDbCloud2CACert);

/* definisco la misura cioè la tabella in cui inserire i dati */
Point sensorsTable(_measurement);

//Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
WiFiManager wifiManager;
OneWire oneWire(ONE_WIRE_PIN);
DallasTemperature sensors(&oneWire);

Ticker ticker;
ESP8266WebServer server(80);

bool serpentina = false;
float maxTempTerra = -127;
float minTempTerra = 127;
float maxTemp = 32;
float minTemp = 30;

String nameT1 = "Temp. terra";
String nameT2 = "Temp";
String reptileName = "Reptile";
String relayName1 = "Disp. 1";

const unsigned long intervalNTP = ONE_HOUR; // Update the time every hour
unsigned long prevNTP = 0;
unsigned long lastNTPResponse = millis();

const unsigned long intervalTemp = 30000;   // Do a temperature measurement every 30s
unsigned long prevTemp = 0;
bool tmpRequested = false;
const unsigned long DS_delay = 750;         // Reading the temperature from the DS18x20 can take up to 750ms

uint32_t timeUNIX = 0;                      // The most recent timestamp received from the time server
float t1;
float t2;


void setup() {
  WiFi.mode(WIFI_STA); // explicitly set modbe, esp defaults to STA+AP

  Serial.begin(115200);
  Serial.setDebugOutput(true);

  pinMode(TRIGGER_PIN, INPUT_PULLUP);
  pinMode(BUILTIN_LED, OUTPUT);
  pinMode(RELAY_TEMP, OUTPUT);

  setupDisplay();
  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  //if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3D for 128x64
  //  Serial.println(F("SSD1306 allocation failed"));
  //  for (;;); // Don't proceed, loop forever
  //}

  display.display();
  delay(500);
  display.clearDisplay();
  
  String savedSSID = wifiManager.getWiFiSSID(true);
  
  printText("Provo a connettermi a " + savedSSID);
  display.display();

  //wm.resetSettings();

  //called after AP mode and config portal has started
  wifiManager.setAPCallback(configModeCallback);

  //called after webserver has started
  wifiManager.setWebServerCallback(startWebServerCallback);

  //called when wifi settings have been changed and connection was successful ( or setBreakAfterConfig(true) )
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  wifiManager.setHostname("reptilesensors");
  wifiManager.setConfigPortalBlocking(false);

  if (wifiManager.autoConnect("ReptileSensorsAP")) {
    Serial.println("connected...yeey :)");
    printText("Connection complete!");
    display.display();

    setupMDNS();
    setupAndStartServer();
    //setupSensorsTag();
  }
  else {
    Serial.println("Configportal running");
    display.clearDisplay();
    printText("Connessione non riuscita!");
    printText("Connettiti alla rete ReptileSensorsAP e configura il WIFI");
    display.display();
  }

  //readAndSaveSensors();

  // Add service to MDNS-SD
  //MDNS.addService("http", "tcp", 80);

  display.clearDisplay();
}

void setupMDNS() {

  if (!MDNS.begin("esp8266")) {             // Start the mDNS responder for esp8266.local
    Serial.println("Error setting up MDNS responder!");
  }
  Serial.println("mDNS responder started");

}

void setupSensorsTag() {

  /* preparo la connessione su influxDB */
  /* aggiiungo i tag nel Point per salvare in modo ordinato i dati sul server */
  sensorsTable.addTag("host", host);
  sensorsTable.addTag("location", location);
  sensorsTable.addTag("room", room);
}


int count = 5;
unsigned long prevCountTemp = 0;

void loop() {
  MDNS.update();
  wifiManager.process();
  server.handleClient();
  unsigned long currentMillis = millis();

  if (digitalRead(TRIGGER_PIN) == LOW ) {
    delay(1000);
    ESP.restart();
  }

  // Every minute, request the temperature
  if (currentMillis - prevTemp > intervalTemp) {
    prevTemp = currentMillis;
    tmpRequested = true;
    readSaveAndShowSensorsData();
    writeToInfluxDB();
  }
}

void readSaveAndShowSensorsData() {
  readAndSaveSensors();
  setUpHotDevice();
  saveMaxAndMinTemp();
  updateDisplay();
}


void setupAndStartServer() {
  Serial.println("setupAndStartServer");
  server.close();
  server.stop();
  server.on("/", handle_root);

  server.on("/update", HTTP_POST, handleLogin);

  server.on("/data", HTTP_GET, []() {

    if (std::isnan(t1) || std::isnan(t2)) {
      Serial.println("Failed to read from DHT sensor!");
      return;
    }

    String webString = "Humiditiy " + String((int)t1) + " C: " + String((int)t2) + " C";
    Serial.println(webString);
    server.send(200, "text/plain", webString);
  });

  server.on("/data.json", []() {

    if (std::isnan(t1) || std::isnan(t2)) {
      Serial.println("Failed to read from sensor!");
      return;
    }

    Serial.println("Reporting " + String((int)t1) + "C and " + String((int)t2) + " C");

    StaticJsonDocument<500> doc;
    JsonObject root = doc.to<JsonObject>();
    root["t1"] = t1;
    root["t2"] = t2;

    String jsonString;
    serializeJson(doc, jsonString);

    Serial.println(jsonString);
    server.send(200, "application/json", jsonString);
  });
  server.begin();
}

unsigned int  timeout   = 120; // seconds to run for
unsigned int  startTime = millis();
bool portalRunning      = false;
bool startAP            = false; // start AP and webserver if true, else start only webserver

void doWiFiManager() {
  // is auto timeout portal running
  if (portalRunning) {
    wifiManager.process();
    if ((millis() - startTime) > (timeout * 1000)) {
      Serial.println("portaltimeout");
      portalRunning = false;
      if (startAP) {
        wifiManager.stopConfigPortal();
      } else {
        wifiManager.stopWebPortal();
      }
    }
  }

  // is configuration portal requested?
  if (digitalRead(TRIGGER_PIN) == LOW && (!portalRunning)) {
    if (startAP) {
      Serial.println("Button Pressed, Starting Config Portal");
      wifiManager.setConfigPortalBlocking(false);
      wifiManager.startConfigPortal();
    }
    else {
      Serial.println("Button Pressed, Starting Web Portal");
      wifiManager.startWebPortal();
    }
    portalRunning = true;
    startTime = millis();
  }
}

void saveMaxAndMinTemp() {
  if (t1 > maxTempTerra) {
    maxTempTerra = t1;
  }

  if (t1 < minTempTerra) {
    minTempTerra = t1;
  }
}

void setUpHotDevice() {
  if (t1 > maxTemp) {
    Serial.println("Spengo Serpentina");
    serpentina = false;
    digitalWrite(RELAY_TEMP, 0);
  } else if (t1 < minTemp) {
    Serial.println("Accendo Serpentina");
    serpentina = true;
    digitalWrite(RELAY_TEMP, 1);
  }
}

void readAndSaveSensors() {
  Serial.println("read And Save Sensors");
  sensors.requestTemperatures();
  t1 = sensors.getTempCByIndex(0);
  t2 = sensors.getTempCByIndex(1);
}


void updateDisplay() {
  String st1 = nameT1 + ": " + String(t1) + " C";
  String st2 = nameT2 + ": " + String(t2) + " C";
  Serial.println(st1);
  Serial.println(st2);
  printTitle(reptileName);
  printText(st1 + String("\n") + st2);
  printText(String("max: ") + maxTempTerra + String(" C\nmin: ") + minTempTerra + String(" C"));

  String serpentinaStatus;
  if (serpentina) {
    serpentinaStatus = relayName1 + ": ON";
  } else {
    serpentinaStatus = relayName1 + ": OFF";
  }
  printText(serpentinaStatus);
  if (WiFi.status() == WL_CONNECTED) {
    String ssid = wifiManager.getWiFiSSID();
    //String ip = wifiManager.;
    String networkInfo;
    if (ssid != NULL) {
      printText("Connesso a: " + ssid);
    }
  } else {
    printText("Connessione assente...");
  }


  display.display();
}

void handleLogin() {                         // If a POST request is made to URI /login
  if ( ! server.hasArg("t1") || ! server.hasArg("t2") || ! server.hasArg("reptileName") || !server.hasArg("relay1")) { // If the POST request doesn't have username and password data
    server.send(400, "text/plain", "400: Invalid Request");         // The request is invalid, so send HTTP status 400
    return;
  }

  String tmp1 = server.arg("t1");
  if (tmp1 != NULL) {
    nameT1 = tmp1;
    Serial.println("Nuovo nome sensore 1: " + nameT1);
  }

  String tmp2 = server.arg("t2");
  if (tmp2 != NULL) {

    nameT2 = tmp2;
    Serial.println("Nuovo nome sensore 2: " + nameT2);
  }

  String tmp3 = server.arg("reptileName");
  if (tmp3 != NULL) {
    reptileName = tmp3;
    Serial.println("Nuovo nome rettile: " + reptileName);
  }


  String tmp4 = server.arg("relay1");
  if (tmp4 != NULL) {

    relayName1 = tmp4;
    Serial.println("Nuovo nome dispositivo 1: " + relayName1);
  }

  String tmp5 = server.arg("maxTemp");
  if (tmp5 != NULL) {
    maxTemp  = String(tmp5).toFloat();
    Serial.println("Nuovo parametro temp. max " + String(maxTemp));
  }

  String tmp6 = server.arg("minTemp");
  if (tmp6 != NULL) {
    minTemp  = String(tmp6).toFloat();
    Serial.println("Nuovo parametro temp. min " + String(minTemp));
  }

  updateDisplay();
  server.send(200, "text/html", "Configurazione completata");

}

// Handle root url (/)
void handle_root() {

  String t1Html = "<p>" + nameT1 + ": " + String(t1) + " C</p>";
  String t2Html = "<p>" + nameT2 + ": " + String(t2) + " C</p>";
  String hotDeviceHtml;
  if (serpentina) {
    hotDeviceHtml = "<p>" + relayName1 + ": ATTIVO</p>";
  } else {
    hotDeviceHtml = "<p>" + relayName1 + ": SPENTO</p>";
  }

  String reptileNameString = "<h2>" + reptileName + "</h2>";
  String maxTempString = "<p>Temperatura massima impostata a: " + String(maxTemp) + " C</p>";
  String minTempString = "<p>Temperatura minima impostata a: " + String(minTemp) + " C</p>";
  String updateButton = "<button onClick=\"window.location.reload();\">Aggiorna</button></br></br>";
  String maxTempInput = "<input type=\"number\" name=\"maxTemp\" maxlength=\"2\" placeholder=\"Temperatura massima\"></br>";
  String minTempInput = "<input type=\"number\" name=\"minTemp\" maxlength=\"2\" placeholder=\"Temperatura minima\"></br>";
  String form = "<form action=\"/update\" method=\"POST\"><input type=\"text\" name=\"reptileName\" maxlength=\"10\" placeholder=\"Nome rettile\"></br><input type=\"text\" name=\"t1\" maxlength=\"13\" placeholder=\"Nome sensore 1\"></br><input type=\"text\" name=\"t2\" maxlength=\"13\" placeholder=\"Nome Sensore 2\"></br><input type=\"text\" name=\"relay1\" placeholder=\"Nome Dispositivo 1\"></br>" + maxTempInput + minTempInput + "</br><input type=\"submit\" value=\"Salva\"></form>";
  String HTML = "<!DOCTYPE html><html><body><h1>Reptile Sensors</h1>" + reptileNameString + t1Html + t2Html + maxTempString + minTempString +  hotDeviceHtml + updateButton + form + "</body></html>" ;

  server.send(200, "text/html", HTML);
}

/* funzione per scrivere su influxDB */
void writeToInfluxDB() {
  sensorsTable.clearFields();
  sensorsTable.addField("temperature_hot_zone", t1);
  sensorsTable.addField("temperature_cold_zone", t2);
  sensorsTable.addField("heating_mat_relay", serpentina);
  Serial.print("Writing: ");
  Serial.println(sensorsTable.toLineProtocol());

  /* ora scriviamo sul server */
  if (!client.writePoint(sensorsTable)) {
    Serial.print("InfluxDB write failed ");
    Serial.println(client.getLastErrorMessage());
  }
}

void tick() {
  //toggle state
  int state = digitalRead(BUILTIN_LED);
  digitalWrite(BUILTIN_LED, !state);
}

//gets called when WiFiManager enters configuration mode
void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());
  //if you used auto generated SSID, print it
  Serial.println(myWiFiManager->getConfigPortalSSID());
  //entered config mode, make led toggle faster
  ticker.attach(0.2, tick);
}

void startWebServerCallback () {
  Serial.println("startWebServerCallback");
  //server.begin();
}

//called when wifi settings have been changed and connection was successful ( or setBreakAfterConfig(true) )
void saveConfigCallback() {
  Serial.println("saveConfigCallback");
  setupAndStartServer();
}
