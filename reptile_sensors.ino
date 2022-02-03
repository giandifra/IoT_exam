#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ArduinoJson.h>

#include "display_helper.h"
#include "influxdb_helper.h"

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <AutoConnect.h>
#include <ESP8266mDNS.h>

#define ONE_WIRE_BUS_PIN D4
#define RELAY_TEMP D3
#define BUZZER D7
#define CP_SSID "Reptile-Sensors-Portal"
#define CP_PASSWORD "iot_2021"
#define SW_VERSION "GM Di Francesco 1.0.0"

OneWire oneWireBus(ONE_WIRE_BUS_PIN);

DallasTemperature sensors(&oneWireBus);

ESP8266WebServer Server;
AutoConnect      Portal(Server);
AutoConnectConfig Config;

bool heating_mat_status = false;
float maxTempTerra = -127;
float minTempTerra = 127;
float maxTemp = 32;
float minTemp = 31;

String nameT1 = "Zona calda";
String nameT2 = "Zona fredda";
String reptileName = "Reptile";
String relayName1 = "Relay tappetino";

const unsigned long thirtySeconds = 30000;   // Do a temperature measurement every 30s
const unsigned long fiveMinutes = 5 * 60000;   // Do a temperature measurement every 30s
unsigned long lastReadTime = 0;

int MAX_TEMP_OVERLOAD = 2;

float t1;
float t2;
String cpIP;

void setup() {
  delay(1000);
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println(SW_VERSION);

  setupPin();
  setupDisplay();

  printText("Provo a connettermi ad una rete conosciuta ");
  display.display();

  setupAutoConnect();
  startSensors();
  readTemperatures();
  display.clearDisplay();
}


void setupPin() {
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(RELAY_TEMP, OUTPUT);
  pinMode (BUZZER, OUTPUT);
}

void startSensors() {
  Serial.println("startSensors");
  sensors.begin();
}

bool whileCP(void) {
  return false;
}

void setupAutoConnect() {
  Serial.println("setupAutoConnect");

  Config.apid = CP_SSID;
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
  if (MDNS.begin("reptile-sensors")) {
    MDNS.addService("http", "tcp", 80);
  } else {
    Serial.println("Error setting up MDNS responder!");
  }
}

void loop() {
  MDNS.update();
  Portal.handleClient();
  unsigned long currentMillis = millis();

  // Every 30 seconds, request the temperature
  if (currentMillis - lastReadTime > thirtySeconds) {
    Serial.println("Update data");
    lastReadTime = currentMillis;
    readSaveAndShowSensorsData();
  }
}

void readSaveAndShowSensorsData() {
  Serial.println("readSaveAndShowSensorsData");
  readTemperatures();
  setUpHeatMat();
  saveMaxAndMinTemp();
  updateDisplay();
  sendDataToInfluxDB();
}

void sendDataToInfluxDB() {
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WIFI Connected");
    if (t1 == -127.0 || t2 == -127.0 ) {
      Serial.println("Data is not sent to influxDB: t1 OR t2 invalid value");
    } else {
      Serial.println("Writing to influxDB...");
      writeToInfluxDB(t1, t2, heating_mat_status);
    }
  } else {
    Serial.println("WIFI not connected: Not sended data to influxDB");
  }
}

void setupServer() {
  Serial.println("setupServer");
  Server.on("/", handleRoot);
  Server.on("/update", HTTP_POST, setupConfigParams);
  Server.on("/data", HTTP_GET, dataPage);
  Server.on("/data.json", dataJSONPage);

}

void saveMaxAndMinTemp() {
  Serial.println("saveMaxAndMinTemp");
  if (t1 > maxTempTerra) {
    maxTempTerra = t1;
  }

  if (t1 < minTempTerra) {
    minTempTerra = t1;
  }
}

void setUpHeatMat() {
  Serial.println("setUpHeatMat");
  unsigned long currentMillis = millis();
  if (t1 > maxTemp) {
    Serial.println("Turn OFF heat mat");
    heating_mat_status = false;
    digitalWrite(RELAY_TEMP, 0);

    securityCheck(t1 - maxTemp, currentMillis);

  } else if (t1 < minTemp) {
    Serial.println("Turn ON heat mat");
    heating_mat_status = true;
    digitalWrite(RELAY_TEMP, 1);

    securityCheck(minTemp - t1, currentMillis);

  } else {
    digitalWrite (BUZZER, LOW); //turn buzzer off
  }
}

//Check temperature under/overload
//If the temperature is great then maxTemp + MAX_TEMP_OVERLOAD or min
void securityCheck(float differenceTemp, unsigned long currentMillis) {
  if (differenceTemp  > MAX_TEMP_OVERLOAD) {
    Serial.println("Turn ON BUZZER");
    digitalWrite (BUZZER, HIGH); //turn buzzer on
  } else {
    Serial.println("Turn OFF BUZZER");
    digitalWrite (BUZZER, LOW); //turn buzzer off
  }

}

void readTemperatures() {
  Serial.println("read And Save Sensors");
  sensors.requestTemperatures();
  t1 = sensors.getTempCByIndex(0);
  t2 = sensors.getTempCByIndex(1);
  Serial.println(t1);
  Serial.println(t2);
}

void updateDisplay() {
  String _t1 = "--";
  String _t2 = "--";
  if (t1 != -127) {
    _t1 = String(t1);
  }
  if (t2 != -127) {
    _t2 = String(t2);
  }
  String st1 = nameT1 + ": " + _t1 + " C";
  String st2 = nameT2 + ": " + _t2 + " C";
  Serial.println(st1);
  Serial.println(st2);
  printTitle(reptileName);
  printText(st1 + String("\n") + st2);
  printText(String("min ") + minTempTerra + String("max ") + maxTempTerra + String(" C"));

  String serpentinaStatus;
  if (heating_mat_status) {
    serpentinaStatus = relayName1 + ": ON";
  } else {
    serpentinaStatus = relayName1 + ": OFF";
  }
  printText(serpentinaStatus);
  if (WiFi.status() == WL_CONNECTED) {
    String ssid = WiFi.SSID();
    String ip = WiFi.localIP().toString();
    String networkInfo = ip;
    if (ssid != NULL) {
      networkInfo = ssid + ": " + ip;
    }
    printText(networkInfo);
  } else {
    printText("CP attivo");
  }

  display.display();
}

void setupConfigParams() {
  if ( ! Server.hasArg("t1") || ! Server.hasArg("t2") || ! Server.hasArg("reptileName") || !Server.hasArg("relay1")) { // If the POST request doesn't have username and password data
    Server.send(400, "text/plain", "400: Invalid Request");
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

void handleRoot() {
  String t1Html = "<p>" + nameT1 + ": " + String(t1) + " C</p>";
  String t2Html = "<p>" + nameT2 + ": " + String(t2) + " C</p>";
  String hotDeviceHtml;
  if (heating_mat_status) {
    hotDeviceHtml = "<p>" + relayName1 + ": ATTIVO</p>";
  } else {
    hotDeviceHtml = "<p>" + relayName1 + ": SPENTO</p>";
  }

  String captivePortalInfo = "";
  if (!cpIP.isEmpty()) {
    captivePortalInfo = "<p> Captive Portal ip: " + cpIP + "</p>";
  }
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

  String webString = "Temperatures: " + String((int)t1) + " C: " + String((int)t2) + " C";
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
