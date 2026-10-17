#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>

// ============================================================
// LIBRERÍAS TINYGSM
// ============================================================
#define TINY_GSM_MODEM_SIM7070

#include <TinyGsmClient.h>

// ============================================================
// 1. CONFIGURACIÓN DE HARDWARE (LilyGO T-SIM7070G)
// ============================================================
#define UART_BAUD   115200
#define PIN_TX      27
#define PIN_RX      26
#define PWR_PIN     4

// ============================================================
// 2. CONFIGURACIÓN DE RED Y MQTT
// ============================================================
const char apn[]      = "internet.itelcel.com";
const char gprsUser[] = "";
const char gprsPass[] = "";

const char* broker = "dd2e07c1.ala.us-east-1.emqxsl.com";
const char* topicPublish = "test01";
const int   port = 8883;
const char* mqtt_user = "edpi";
const char* mqtt_password = "edpi";

// Certificado CA
const char* ca_cert = \
"-----BEGIN CERTIFICATE-----\n" \
"MIIDjjCCAnagAwIBAgIQAzrx5qcRqaC7KGSxHQn65TANBgkqhkiG9w0BAQsFADBh\n" \
"MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3\n" \
"d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH\n" \
"MjAeFw0xMzA4MDExMjAwMDBaFw0zODAxMTUxMjAwMDBaMGExCzAJBgNVBAYTAlVT\n" \
"MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j\n" \
"b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IEcyMIIBIjANBgkqhkiG\n" \
"9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuzfNNNx7a8myaJCtSnX/RrohCgiN9RlUyfuI\n" \
"2/Ou8jqJkTx65qsGGmvPrC3oXgkkRLpimn7Wo6h+4FR1IAWsULecYxpsMNzaHxmx\n" \
"1x7e/dfgy5SDN67sH0NO3Xss0r0upS/kqbitOtSZpLYl6ZtrAGCSYP9PIUkY92eQ\n" \
"q2EGnI/yuum06ZIya7XzV+hdG82MHauVBJVJ8zUtluNJbd134/tJS7SsVQepj5Wz\n" \
"tCO7TG1F8PapspUwtP1MVYwnSlcUfIKdzXOS0xZKBgyMUNGPHgm+F6HmIcr9g+UQ\n" \
"vIOlCsRnKPZzFBQ9RnbDhxSJITRNrw9FDKZJobq7nMWxM4MphQIDAQABo0IwQDAP\n" \
"BgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBhjAdBgNVHQ4EFgQUTiJUIBiV\n" \
"5uNu5g/6+rkS7QYXjzkwDQYJKoZIhvcNAQELBQADggEBAGBnKJRvDkhj6zHd6mcY\n" \
"1Yl9PMWLSn/pvtsrF9+wX3N3KjITOYFnQoQj8kVnNeyIv/iPsGEMNKSuIEyExtv4\n" \
"NeF22d+mQrvHRAiGfzZ0JFrabA0UWTW98kndth/Jsw1HKj2ZL7tcu7XUIOGZX1NG\n" \
"Fdtom/DzMNU+MeKNhJ7jitralj41E6Vf8PlwUHBHQRFXGU7Aj64GxJUTFy8bJZ91\n" \
"8rGOmaFvE7FBcf6IKshPECBV1/MUReXgRPTqh5Uykw7+U0b6LJ3/iyK5S9kJRaTe\n" \
"pLiaWN0bfVKfjllDiIGknibVb63dDcY3fe0Dkhvld1927jyNxF1WW6LZZm6zNTfl\n" \
"MrY=\n" \
"-----END CERTIFICATE-----\n";

const char* ssid_wifi = "AsusE";
const char* password_wifi = "23011edpi";

// ============================================================
// 3. OBJETOS Y VARIABLES GLOBALES
// ============================================================
HardwareSerial ModemSerial(1);
TinyGsm modem(ModemSerial);
TinyGsmClientSecure modemClient(modem);  // Para LTE (TLS)
WiFiClientSecure wifiClient;             // Para WiFi (TLS)

PubSubClient* mqtt = nullptr;

enum ConnectionType { CONN_NONE, CONN_LTE, CONN_WIFI };
ConnectionType currentConnection = CONN_NONE;

unsigned long lastSend = 0;
const unsigned long sendInterval = 2000;

// --- Control de conmutación LTE -> WiFi ---
int lteMqttFailCount = 0;                 // Fallos MQTT consecutivos en LTE
const int LTE_FAIL_THRESHOLD = 2;         // Cambiar a WiFi tras N fallos
bool modemPoweredOn = false;              // Evita re-inicializar el módem sin necesidad

// ============================================================
// 4. FUNCIONES
// ============================================================

// Apaga el módem de forma segura (cierra GPRS y libera recursos)
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

// Devuelve true si MQTT quedó conectado
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

// Cambia activamente de LTE a WiFi (cierra GPRS, libera cliente MQTT)
bool switchToWiFi() {
  Serial.println("\n*** Conmutando de LTE a WiFi ***");

  if (mqtt) {
    mqtt->disconnect();
    delete mqtt;
    mqtt = nullptr;
  }

  powerOffModem();       // Cierra GPRS para liberar el módem
  WiFi.disconnect(true); // Por si acaso estaba a medias
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
// 5. SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  while (!Serial);

  Serial.println("=== T-SIM7070G Gateway (LTE -> WiFi) ===");

  randomSeed(analogRead(0));

  if (establishConnection()) {
    if (!connectMQTT()) {
      // Si el primer intento de MQTT falla y estamos en LTE, contabilizarlo
      if (currentConnection == CONN_LTE) {
        lteMqttFailCount++;
      }
    }
  }
}

// ============================================================
// 6. LOOP
// ============================================================
void loop() {
  // --- Camino rápido: si MQTT está conectado, procesar y publicar ---
  if (mqtt && mqtt->connected()) {
    mqtt->loop();
    lteMqttFailCount = 0;

    if (millis() - lastSend > sendInterval) {
      lastSend = millis();

      int randomRaw = random(0, 4096);
      float randomVoltage = random(0, 330) / 100.0;

      String payload = "{";
      payload += "\"dispositivo\":\"T-SIM7070G\",";
      payload += "\"sensor_raw\":" + String(randomRaw) + ",";
      payload += "\"voltaje\":" + String(randomVoltage, 2) + ",";
      payload += "\"red\":\"" + String(currentConnection == CONN_LTE ? "LTE" : "WiFi") + "\",";
      payload += "\"latitud\":null,";
      payload += "\"longitud\":null,";
      payload += "\"precision_mts\":null";
      payload += "}";

      Serial.println("Publicando: " + payload);
      if (!mqtt->publish(topicPublish, payload.c_str())) {
        Serial.println("Fallo al publicar (posible caída de enlace)");
        // Si falla el publish en LTE, cuenta como fallo para forzar conmutación
        if (currentConnection == CONN_LTE) {
          lteMqttFailCount++;
        }
        mqtt->disconnect();
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

  // Si estamos en LTE y ya acumulamos suficientes fallos, cambiar a WiFi
  if (currentConnection == CONN_LTE && lteMqttFailCount >= LTE_FAIL_THRESHOLD) {
    Serial.print("LTE acumuló ");
    Serial.print(lteMqttFailCount);
    Serial.println(" fallos MQTT -> cambiando a WiFi");

    if (!switchToWiFi()) {
      // Si WiFi falla, en el próximo ciclo se intentará LTE de nuevo
      lteMqttFailCount = 0;
    }
    return;
  }

  // Caso normal de reconexión
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