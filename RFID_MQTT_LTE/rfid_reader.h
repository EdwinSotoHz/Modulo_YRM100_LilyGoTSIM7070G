#ifndef RFID_READER_H
#define RFID_READER_H

#include <Arduino.h>
#include "config.h"

// Comando de Inventario (Single Polling) para M100
static const uint8_t CMD_INVENTORY[] = {0xBB, 0x00, 0x22, 0x00, 0x00, 0x22, 0x7E};

inline void initRfidReader() {
  Serial2.begin(115200, SERIAL_8N1, RFID_RXD2, RFID_TXD2);
  Serial.println("Lector RFID listo.");
}

// Devuelve un JSON array con los EPCs encontrados, o "" si no hubo etiquetas.
inline String readRfidTags() {
  // Limpiar buffer antes de enviar
  while (Serial2.available()) Serial2.read();

  // Enviar comando de inventario
  Serial2.write(CMD_INVENTORY, sizeof(CMD_INVENTORY));
  Serial2.flush();

  // Esperar respuesta
  unsigned long timeout = millis() + 500;
  uint8_t buffer[128];
  int index = 0;
  bool recibido = false;

  while (millis() < timeout && index < (int)sizeof(buffer)) {
    if (Serial2.available()) {
      buffer[index++] = Serial2.read();
      recibido = true;
      timeout = millis() + 100;
    }
  }

  if (!recibido) {
    return String("");
  }

  String jsonArray = "[";
  int etiquetas_encontradas = 0;

  int i = 0;
  while (i < index) {
    if (buffer[i] == 0xBB) {
      int fin = i + 1;
      while (fin < index && buffer[fin] != 0x7E) {
        fin++;
      }

      if (fin < index && buffer[fin] == 0x7E) {
        int longitud_trama = fin - i + 1;

        if (longitud_trama >= 10 && buffer[i + 1] == 0x02 && buffer[i + 2] == 0x22) {
          String epc = "";
          for (int j = 8; j < (longitud_trama - 3); j++) {
            if (buffer[i + j] < 0x10) epc += "0";
            epc += String(buffer[i + j], HEX);
          }
          epc.toUpperCase();

          if (etiquetas_encontradas > 0) jsonArray += ",";
          jsonArray += "\"" + epc + "\"";
          etiquetas_encontradas++;
        }

        i = fin + 1;
        continue;
      }
    }
    i++;
  }

  jsonArray += "]";

  if (etiquetas_encontradas > 0) {
    return jsonArray;
  }
  return String("");
}

#endif