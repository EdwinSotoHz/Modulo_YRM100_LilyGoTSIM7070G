# Uso del módulo YRM100 con una LilyGoT-SIM7070G (esp32)

### Prueba del broker
<pre>
mosquitto_sub -h dd2e07c1.ala.us-east-1.emqxsl.com -p 8883 -t test01 -u "edpi" -P "edpi" --cafile "C:\Users\edwin\OneDrive\Documentos\para-repo\Modulo_YRM100_LilyGoTSIM7070G\emqxsl-ca.crt"
</pre>

<pre>
mosquitto_pub -h dd2e07c1.ala.us-east-1.emqxsl.com -p 8883 -t test01 -m "Hola EMQX" -u "edpi" -P "edpi" --cafile "C:\Users\edwin\OneDrive\Documentos\para-repo\Modulo_YRM100_LilyGoTSIM7070G\emqxsl-ca.crt"
</pre>


### Valores de tramas de tarjetas incluidas
- **Nada proximo:** BB 01 FF 00 01 15 16 7E 
- **Tarjeta:** BB 02 22 00 11 D1 34 00 E2 00 47 12 C1 A0 60 26 51 32 01 0B 2D 6E 86 7E
- **Pegatina 1:** BB 02 22 00 11 C7 30 00 E2 80 69 15 00 00 50 2D 9D F8 0D 65 77 68 6F 7E 
- **Pegatina 2:** BB 02 22 00 11 C8 34 00 E2 82 78 02 00 00 00 00 0D FF C5 F9 F4 65 32 7E

### Conexiones
| YRM100 | LilyGo |
| :--- | :--- |
| **GND** | **GND** |
| **EN** | **3.3V** |
| **RXD** | **TXD** |
| **TXD** | **RXD** |
| **VCC** | **3.3V** |