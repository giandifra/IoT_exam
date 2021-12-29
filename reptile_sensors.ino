#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ArduinoJson.h>
#include "Ticker.h"

#include "display_helper.h"
#include "influxdb_helper.h"

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <AutoConnect.h>
#include <ESP8266mDNS.h>

#define ONE_HOUR 3600000UL
#define TRIGGER_PIN D0
#define ONE_WIRE_PIN D4
#define RELAY_TEMP D3
#define CP_PASSWORD "iot_2021"
#define AC_DEBUG

OneWire oneWire(ONE_WIRE_PIN);
DallasTemperature sensors(&oneWire);

Ticker ticker;

ESP8266WebServer Server;          // Replace with WebServer for ESP32
AutoConnect      Portal(Server);
//AutoConnectConfig config("ciaociao", "iot2021");
AutoConnectConfig Config;

bool serpentina = false;
float maxTempTerra = -127;
float minTempTerra = 127;
float maxTemp = 32;
float minTemp = 30;

String nameT1 = "Hot zone";
String nameT2 = "Cold zone";
String reptileName = "Reptile";
String relayName1 = "Mat relay";

const unsigned long intervalNTP = ONE_HOUR; // Update the time every hour
unsigned long prevNTP = 0;
unsigned long lastNTPResponse = millis();

const unsigned long intervalTemp = 30000;   // Do a temperature measurement every 30s
unsigned long prevTemp = 0;
//const unsigned long DS_delay = 750;         // Reading the temperature from the DS18x20 can take up to 750ms

float t1;
float t2;
String cpIP;

void setup() {
  delay(1000);
  Serial.begin(115200);
  Serial.setDebugOutput(true);

  pinMode(TRIGGER_PIN, INPUT_PULLUP);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(RELAY_TEMP, OUTPUT);

  setupDisplay();

  printText("Provo a connettermi ad una rete conosciuta ");
  display.display();

  setupAutoConnect();
  readAndSaveSensors();
  display.clearDisplay();
}

bool whileCP(void) {
  bool  rc = false;
  // Here, something to process while the captive portal is open.
  // To escape from the captive portal loop, this exit function returns false.
  // rc = true;, or rc = false;
  return rc;
}

void setupAutoConnect() {
  Serial.println("setupAutoConnect");

  Config.apid = "Reptile-Sensors-Portal";
  Config.psk  = CP_PASSWORD;
  Config.autoSave  = AC_SAVECREDENTIAL_NEVER;
  Config.hostName  = "Reptile-Sensors";
  Config.retainPortal = true;
  Config.ticker = true;

  Portal.config(Config);
  Portal.whileCaptivePortal(whileCP);
  Portal.onDetect(onStartCaptivePortal);
  Portal.onConnect(onConnect);

  setupServer();
  if (Portal.begin()) {
    Serial.println("WiFi connected: " + WiFi.localIP().toString());
  }
}

void showNoWiFiConnectionMessage(String ip) {
  Serial.println("showNoWiFiConnectionMessage");
  display.clearDisplay();
  printText("NO WIFI - CP Attivo");
  String message = "Connettiti a Reptile-Sensors-Portal e configura il WIFI";
  if (!message.isEmpty()) {
    message = message + ": " + cpIP;
  }
  printText(message);
  display.display();
}

bool onStartCaptivePortal(IPAddress& ip) {
  Serial.println("onStartCaptivePortal");
  cpIP = ip.toString();
  digitalWrite(LED_BUILTIN, HIGH);
  Serial.println("C.P. started, IP:" + cpIP);
  showNoWiFiConnectionMessage(cpIP);
  return true;
}

void onConnect(IPAddress& ipaddr) {
  Serial.println("onConnect");
  Serial.print("WiFi connected with ");
  Serial.print(WiFi.SSID());
  Serial.print(", IP:");
  Serial.println(ipaddr.toString());
  Serial.println("Connection complete!");

  display.clearDisplay();
  printText("Connection complete!");
  display.display();

  setupMDNS();
  setupSensorsTag();
}

void setupMDNS() {
  if (MDNS.begin("esp8266")) {
    MDNS.addService("http", "tcp", 80);
  } else {
    Serial.println("Error setting up MDNS responder!");
  }
}

void loop() {
  MDNS.update();
  //wifiManager.process();
  //server.handleClient();
  Portal.handleClient();
  unsigned long currentMillis = millis();

  checkRestartButton();

  // Every minute, request the temperature
  if (currentMillis - prevTemp > intervalTemp) {
    Serial.println("Update data");
    prevTemp = currentMillis;
    readSaveAndShowSensorsData();
  }
}

void checkRestartButton() {
  if (digitalRead(TRIGGER_PIN) == LOW ) {
    Serial.println("REBOOT ESP");
    delay(1000);
    ESP.restart();
  }
}

void readSaveAndShowSensorsData() {
  Serial.println("readSaveAndShowSensorsData");
  readAndSaveSensors();
  setUpHotDevice();
  saveMaxAndMinTemp();
  updateDisplay();
  if (WiFi.status() == WL_CONNECTED) {
    writeToInfluxDB(t1, t2, serpentina);
  } else {
    Serial.println("WIFI not connected: Not sended data to influxDB");
  }
}

void setupServer() {
  Serial.println("setupServer");
  Server.on("/", handle_root);
  Server.on("/update", HTTP_POST, handleLogin);
  Server.on("/data", HTTP_GET, dataPage);
  Server.on("/data.json", dataJSONPage);

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
    String ssid = WiFi.SSID();
    //String ip = wifiManager.;
    String networkInfo;
    if (ssid != NULL) {
      printText("Connesso a: " + ssid);
    }
  } else {
    printText("CP attivo");
  }


  display.display();
}

void handleLogin() {                         // If a POST request is made to URI /login
  if ( ! Server.hasArg("t1") || ! Server.hasArg("t2") || ! Server.hasArg("reptileName") || !Server.hasArg("relay1")) { // If the POST request doesn't have username and password data
    Server.send(400, "text/plain", "400: Invalid Request");         // The request is invalid, so send HTTP status 400
    return;
  }

  String tmp1 = Server.arg("t1");
  if (tmp1 != NULL) {
    nameT1 = tmp1;
    Serial.println("Nuovo nome sensore 1: " + nameT1);
  }

  String tmp2 = Server.arg("t2");
  if (tmp2 != NULL) {

    nameT2 = tmp2;
    Serial.println("Nuovo nome sensore 2: " + nameT2);
  }

  String tmp3 = Server.arg("reptileName");
  if (tmp3 != NULL) {
    reptileName = tmp3;
    Serial.println("Nuovo nome rettile: " + reptileName);
  }


  String tmp4 = Server.arg("relay1");
  if (tmp4 != NULL) {

    relayName1 = tmp4;
    Serial.println("Nuovo nome dispositivo 1: " + relayName1);
  }

  String tmp5 = Server.arg("maxTemp");
  if (tmp5 != NULL) {
    maxTemp  = String(tmp5).toFloat();
    Serial.println("Nuovo parametro temp. max " + String(maxTemp));
  }

  String tmp6 = Server.arg("minTemp");
  if (tmp6 != NULL) {
    minTemp  = String(tmp6).toFloat();
    Serial.println("Nuovo parametro temp. min " + String(minTemp));
  }

  updateDisplay();
  Server.send(200, "text/html", "Configurazione completata");

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

  String captivePortalInfo = "<h2> Captive Portal ip: " + cpIP + "</h2>";
  String reptileNameString = "<h2>" + reptileName + "</h2>";
  String maxTempString = "<p>Temperatura massima impostata a: " + String(maxTemp) + " C</p>";
  String minTempString = "<p>Temperatura minima impostata a: " + String(minTemp) + " C</p>";
  String updateButton = "<button onClick=\"window.location.reload();\">Aggiorna</button></br></br>";
  String deleteCredentialButton = "<button onClick=\"window.location.reload();\">Aggiorna</button></br></br>";
  String maxTempInput = "<input type=\"number\" name=\"maxTemp\" maxlength=\"2\" placeholder=\"Temperatura massima\"></br>";
  String minTempInput = "<input type=\"number\" name=\"minTemp\" maxlength=\"2\" placeholder=\"Temperatura minima\"></br>";
  String form = "<form action=\"/update\" method=\"POST\"><input type=\"text\" name=\"reptileName\" maxlength=\"10\" placeholder=\"Nome rettile\"></br><input type=\"text\" name=\"t1\" maxlength=\"13\" placeholder=\"Nome sensore 1\"></br><input type=\"text\" name=\"t2\" maxlength=\"13\" placeholder=\"Nome Sensore 2\"></br><input type=\"text\" name=\"relay1\" placeholder=\"Nome Dispositivo 1\"></br>" + maxTempInput + minTempInput + "</br><input type=\"submit\" value=\"Salva\"></form>";
  String HTML = "<!DOCTYPE html><html><body><h1>Reptile Sensors</h1>" + captivePortalInfo + reptileNameString + t1Html + t2Html + maxTempString + minTempString +  hotDeviceHtml + updateButton + form + "</body></html>" ;

  Server.send(200, "text/html", HTML);
}

void dataPage() {
  if (std::isnan(t1) || std::isnan(t2)) {
    Serial.println("Failed to read from temperature sensor!");
    return;
  }

  String webString = "Humiditiy " + String((int)t1) + " C: " + String((int)t2) + " C";
  Serial.println(webString);
  Server.send(200, "text/plain", webString);
}

void dataJSONPage() {
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
  Server.send(200, "application/json", jsonString);

}

void tick() {
  int state = digitalRead(LED_BUILTIN);
  digitalWrite(LED_BUILTIN, !state);
}
