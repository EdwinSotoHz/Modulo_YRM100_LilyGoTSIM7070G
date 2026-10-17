#define RXD2 32   // Conectar al TX del YRM100
#define TXD2 33   // Conectar al RX del YRM100

// Comando de Inventario (Single Polling) para M100
const uint8_t CMD_INVENTORY[] = {0xBB, 0x00, 0x22, 0x00, 0x00, 0x22, 0x7E};

void setup() {
  Serial.begin(115200);
  Serial2.begin(115200, SERIAL_8N1, RXD2, TXD2);
  Serial.println("Lector RFID listo. Acerca etiquetas...");
}

void loop() {
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

  while (millis() < timeout && index < sizeof(buffer)) {
    if (Serial2.available()) {
      buffer[index++] = Serial2.read();
      recibido = true;
      timeout = millis() + 100; // Extender timeout si llegan más datos
    }
  }

  if (recibido) {
    // Crear un arreglo JSON dinámico
    String jsonArray = "[";
    int etiquetas_encontradas = 0;
    
    int i = 0;
    while (i < index) {
      // Buscar el inicio de una trama
      if (buffer[i] == 0xBB) {
        // Buscar el final de la trama (0x7E)
        int fin = i + 1;
        while (fin < index && buffer[fin] != 0x7E) {
          fin++;
        }
        
        // Si encontramos el final de la trama
        if (fin < index && buffer[fin] == 0x7E) {
          int longitud_trama = fin - i + 1;
          
          // Verificar si es una trama de etiqueta válida (Comando 0x22)
          if (longitud_trama >= 10 && buffer[i+1] == 0x02 && buffer[i+2] == 0x22) {
            
            // Extraer el EPC. Según el protocolo, después de BB 02 22 y la longitud,
            // vienen el PC (2 bytes) y luego el EPC (12 bytes) que empieza con E2.
            // Empezamos a extraer desde el byte 8 de la trama.
            String epc = "";
            for (int j = 8; j < (longitud_trama - 3); j++) { 
              if (buffer[i+j] < 0x10) epc += "0";
              epc += String(buffer[i+j], HEX);
            }
            epc.toUpperCase(); // Convertir a mayúsculas para estandarizar
            
            // Agregar al JSON
            if (etiquetas_encontradas > 0) jsonArray += ",";
            jsonArray += "\"" + epc + "\"";
            etiquetas_encontradas++;
          }
          
          // Saltar al final de esta trama para buscar la siguiente
          i = fin + 1;
          continue;
        }
      }
      i++;
    }
    
    jsonArray += "]";
    
    // Solo imprimir si realmente encontramos etiquetas válidas
    if (etiquetas_encontradas > 0) {
      Serial.println(jsonArray);
    }
  }

  delay(200); // Pequeña pausa entre lecturas
}