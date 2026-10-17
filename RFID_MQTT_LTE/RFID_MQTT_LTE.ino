#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>

#define TINY_GSM_MODEM_SIM7070
#include <TinyGsmClient.h>

#include "config.h"
#include "rfid_reader.h"

// ============================================================
// OBJETOS GLOBALES DE RED
// ============================================================
HardwareSerial ModemSerial(1);
TinyGsm modem(ModemSerial);
TinyGsmClientSecure modemClient(modem);
WiFiClientSecure wifiClient;

PubSubClient* mqtt = nullptr;

ConnectionType currentConnection = CONN_NONE;
unsigned long lastSend = 0;
const unsigned long sendInterval = 2000;

int lteMqttFailCount = 0;
const int LTE_FAIL_THRESHOLD = 2;
bool modemPoweredOn = false;

// ============================================================
// FUNCIONES DE RED
// ============================================================
void powerOffModem() {
  if (modemPoweredOn) {
    Serial.println("Cerrando GPRS del módem...");
    modem.gprsDisconnect();
    modemPoweredOn = false;
  }
}

bool initModem() {
  Serial.println("Iniciando módem celular...");

  pinMode(PWR_PIN, OUTPUT);
  digitalWrite(PWR_PIN, LOW);
  delay(100);
  digitalWrite(PWR_PIN, HIGH);
  delay(1000);
  digitalWrite(PWR_PIN, LOW);
  delay(2000);

  ModemSerial.begin(UART_BAUD, SERIAL_8N1, PIN_RX, PIN_TX);
  delay(3000);

  if (!modem.restart()) {
    Serial.println("Fallo: módem no responde");
    return false;
  }

  Serial.print("Esperando red...");
  if (!modem.waitForNetwork(60000L)) {
    Serial.println(" sin cobertura");
    return false;
  }
  Serial.println(" OK");

  Serial.print("Conectando APN...");
  if (!modem.gprsConnect(apn, gprsUser, gprsPass)) {
    Serial.println(" fallo APN");
    return false;
  }
  Serial.println(" OK");
  Serial.print("IP LTE: ");
  Serial.println(modem.getLocalIP());

  modemPoweredOn = true;
  return true;
}

bool connectWiFi() {
  Serial.print("Conectando WiFi: ");
  Serial.println(ssid_wifi);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid_wifi, password_wifi);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi OK");
    Serial.print("IP WiFi: ");
    Serial.println(WiFi.localIP());
    return true;
  }
  Serial.println("\nWiFi falló");
  return false;
}

bool connectMQTT() {
  if (currentConnection == CONN_LTE) {
    mqtt = new PubSubClient(modemClient);
    Serial.println("MQTT vía LTE");
  } else if (currentConnection == CONN_WIFI) {
    wifiClient.setCACert(ca_cert);
    mqtt = new PubSubClient(wifiClient);
    Serial.println("MQTT vía WiFi");
  } else {
    return false;
  }

  mqtt->setServer(broker, port);

  String clientId = "ESP32-7070G-" + String(random(0xffff), HEX);

  if (mqtt->connect(clientId.c_str(), mqtt_user, mqtt_password)) {
    Serial.println("MQTT conectado");
    return true;
  } else {
    Serial.print("MQTT falló, rc=");
    Serial.println(mqtt->state());
    return false;
  }
}

bool establishConnection() {
  Serial.println("\n--- Intentando LTE ---");
  if (initModem()) {
    currentConnection = CONN_LTE;
    return true;
  }

  Serial.println("\n--- LTE falló, intentando WiFi ---");
  powerOffModem();
  if (connectWiFi()) {
    currentConnection = CONN_WIFI;
    return true;
  }

  Serial.println("Sin conexión disponible");
  currentConnection = CONN_NONE;
  return false;
}

bool switchToWiFi() {
  Serial.println("\n*** Conmutando de LTE a WiFi ***");

  if (mqtt) {
    mqtt->disconnect();
    delete mqtt;
    mqtt = nullptr;
  }

  powerOffModem();
  WiFi.disconnect(true);
  delay(500);

  if (connectWiFi()) {
    currentConnection = CONN_WIFI;
    lteMqttFailCount = 0;
    return connectMQTT();
  }

  Serial.println("WiFi no disponible, reintentando LTE en el próximo ciclo");
  currentConnection = CONN_NONE;
  return false;
}

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  while (!Serial);

  Serial.println("=== T-SIM7070G RFID Gateway (LTE -> WiFi) ===");

  randomSeed(analogRead(0));

  initRfidReader();

  if (establishConnection()) {
    if (!connectMQTT()) {
      if (currentConnection == CONN_LTE) {
        lteMqttFailCount++;
      }
    }
  }
}

// ============================================================
// LOOP
// ============================================================
void loop() {
  // --- Camino rápido: si MQTT está conectado, procesar y publicar ---
  if (mqtt && mqtt->connected()) {
    mqtt->loop();
    lteMqttFailCount = 0;

    if (millis() - lastSend > sendInterval) {
      lastSend = millis();

      // Leer etiquetas RFID (devuelve JSON array, o "" si no hay)
      String rfidJson = readRfidTags();

      if (rfidJson.length() > 0) {
        Serial.println("Publicando RFID: " + rfidJson);
        if (!mqtt->publish(topicPublish, rfidJson.c_str())) {
          Serial.println("Fallo al publicar (posible caída de enlace)");
          if (currentConnection == CONN_LTE) {
            lteMqttFailCount++;
          }
          mqtt->disconnect();
        }
      } else {
        Serial.println("Sin etiquetas RFID en este ciclo");
      }
    }
    return;
  }

  // --- Camino lento: reconexión ---
  static unsigned long lastRetry = 0;
  if (millis() - lastRetry < 30000) {
    return;
  }
  lastRetry = millis();

  Serial.println("Reintentando conexión...");

  if (mqtt) {
    delete mqtt;
    mqtt = nullptr;
  }

  if (currentConnection == CONN_LTE && lteMqttFailCount >= LTE_FAIL_THRESHOLD) {
    Serial.print("LTE acumuló ");
    Serial.print(lteMqttFailCount);
    Serial.println(" fallos MQTT -> cambiando a WiFi");

    if (!switchToWiFi()) {
      lteMqttFailCount = 0;
    }
    return;
  }

  bool ok = false;
  if (currentConnection == CONN_NONE) {
    if (establishConnection()) {
      ok = connectMQTT();
    }
  } else {
    ok = connectMQTT();
  }

  if (!ok && currentConnection == CONN_LTE) {
    lteMqttFailCount++;
    Serial.print("Fallos MQTT LTE acumulados: ");
    Serial.println(lteMqttFailCount);
  }
}