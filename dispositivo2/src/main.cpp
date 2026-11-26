
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>
#include <Mascota.h> 
#include <string.h>

#define DEVICE_ID 2
const uint8_t ID_DESTINO = 1;

#define OLED_SDA 32
#define OLED_SCL 33
#define OLED_ADDRESS 0x3C
#define MAX_TEXTO 100
#define TAM_COLA 5

Adafruit_SSD1306 pantalla(128, 64, &Wire, -1);
Mascota mascota;

enum Tipo : uint8_t { DATOS = 1, ACK = 2, NACK = 3 };

struct __attribute__((packed)) Paquete {
  uint8_t tipo;
  uint8_t origen;
  uint8_t destino;
  uint32_t id;
  uint8_t longitud;
  char texto[MAX_TEXTO + 1];
  uint16_t crc;
};

const uint8_t broadcastMac[6] = {
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

volatile bool respuestaLista = false;
volatile bool respuestaAck = false;
volatile uint32_t respuestaId = 0;
volatile uint16_t respuestaCrc = 0;

Paquete colaPaquetes[TAM_COLA];
volatile uint8_t cabezaCola = 0;
volatile uint8_t cantidadCola = 0;
volatile uint32_t paquetesDescartados = 0;

String mensaje = "";
bool pantallaMascotaActiva = false;

// --------------------------------------------------
// TECLADO FISICO: 9 BOTONES INDEPENDIENTES
// Orden fisico: desde la OLED hacia afuera.
// --------------------------------------------------
const uint8_t pinesBotones[9] = {13, 14, 16, 17, 18, 19, 21, 22, 23};
const char* letrasBoton[8] = {"ABC", "DEF", "GHI", "JKL", "MNO", "PQRS", "TUV", "WXYZ"};
const unsigned long DEBOUNCE_MS = 35;
const unsigned long MULTITAP_MS = 900;
const unsigned long PULSACION_LARGA_MS = 1000;

bool lecturaAnterior[9] = {true,true,true,true,true,true,true,true,true};
bool estadoEstable[9] = {true,true,true,true,true,true,true,true,true};
unsigned long cambioLectura[9] = {0};
unsigned long inicioPulsacion[9] = {0};
bool pulsacionLargaAtendida[9] = {false};
int teclaMultitapPendiente = -1;
uint8_t indiceLetraMultitap = 0;
unsigned long tiempoUltimaTecla = 0;


// --------------------------------------------------
// GENERAR ID
// --------------------------------------------------

uint32_t nuevoId() {
  Preferences p;
  p.begin("protocolo", false);

  uint32_t n = p.getUInt("ultimoId", 0);

  if (n == UINT32_MAX) {
    p.end();
    return 0;
  }

  n++;
  p.putUInt("ultimoId", n);
  p.end();

  return n;
}

// --------------------------------------------------
// CRC16
// --------------------------------------------------

uint16_t crc16(const Paquete& p) {
  uint16_t crc = 0xFFFF;

  auto byteCRC = [&crc](uint8_t b) {
    crc ^= b;

    for (uint8_t i = 0; i < 8; i++) {
      crc = (crc & 1)
        ? ((crc >> 1) ^ 0xA001)
        : (crc >> 1);
    }
  };

  byteCRC(p.tipo);
  byteCRC(p.origen);
  byteCRC(p.destino);

  for (uint8_t i = 0; i < 4; i++) {
    byteCRC((p.id >> (8 * i)) & 0xFF);
  }

  byteCRC(p.longitud);

  if (p.longitud > MAX_TEXTO) return 0;

  for (uint8_t i = 0; i < p.longitud; i++) {
    byteCRC((uint8_t)p.texto[i]);
  }

  return crc;
}

// --------------------------------------------------
// PANTALLAS
// --------------------------------------------------

void mostrar(
  const String& a,
  const String& b = "",
  const String& c = "",
  const String& d = ""
) {
  pantallaMascotaActiva = false;

  pantalla.clearDisplay();
  pantalla.setTextColor(SSD1306_WHITE);
  pantalla.setTextSize(1);

  pantalla.setCursor(0, 0);
  pantalla.println(a);
  pantalla.setCursor(0, 16);
  pantalla.println(b);
  pantalla.setCursor(0, 32);
  pantalla.println(c);
  pantalla.setCursor(0, 48);
  pantalla.println(d);

  pantalla.display();
}

void mostrarEditor() {
  pantallaMascotaActiva = false;

  pantalla.clearDisplay();
  pantalla.setTextColor(SSD1306_WHITE);
  pantalla.setTextSize(1);

  pantalla.setCursor(0, 0);
  pantalla.println("MENSAJE:");
  pantalla.setCursor(0, 16);
  pantalla.println(mensaje);

  pantalla.display();
}

void mostrarEstado(
  const String& a,
  const String& b = "",
  const String& c = "",
  const String& d = ""
) {
  if (!pantallaMascotaActiva) {
    mostrar(a, b, c, d);
  }
}

// --------------------------------------------------
// ENVIAR ACK / NACK
// --------------------------------------------------

void enviarControl(
  uint8_t tipo,
  uint32_t id,
  uint16_t crcOriginal
) {
  Paquete p = {};

  p.tipo = tipo;
  p.origen = DEVICE_ID;
  p.destino = ID_DESTINO;
  p.id = id;
  p.longitud = 2;

  p.texto[0] = (char)(crcOriginal & 0xFF);
  p.texto[1] = (char)(crcOriginal >> 8);

  p.crc = crc16(p);

  esp_now_send(
    broadcastMac,
    (const uint8_t*)&p,
    sizeof(p)
  );
}

// --------------------------------------------------
// CALLBACK DE RECEPCION
// --------------------------------------------------

void callbackRecepcion(
  const uint8_t* mac,
  const uint8_t* data,
  int len
) {
  (void)mac;

  if (len != sizeof(Paquete)) return;

  Paquete p;
  memcpy(&p, data, sizeof(p));

  if (p.origen == DEVICE_ID ||
      p.destino != DEVICE_ID ||
      p.longitud > MAX_TEXTO) {
    return;
  }

  if (p.tipo == ACK || p.tipo == NACK) {
    if (p.longitud != 2 || crc16(p) != p.crc) return;

    uint16_t crcOriginal =
      (uint8_t)p.texto[0] |
      ((uint16_t)(uint8_t)p.texto[1] << 8);

    portENTER_CRITICAL(&mux);

    respuestaAck = (p.tipo == ACK);
    respuestaId = p.id;
    respuestaCrc = crcOriginal;
    respuestaLista = true;

    portEXIT_CRITICAL(&mux);
    return;
  }

  if (p.tipo != DATOS) return;

  portENTER_CRITICAL(&mux);

  if (cantidadCola < TAM_COLA) {
    uint8_t posicion =
      (cabezaCola + cantidadCola) % TAM_COLA;

    memcpy(&colaPaquetes[posicion], &p, sizeof(Paquete));
    cantidadCola++;
  } else {
    paquetesDescartados++;
  }

  portEXIT_CRITICAL(&mux);
}

// --------------------------------------------------
// CALLBACK DE ENVIO
// --------------------------------------------------

void callbackEnvio(
  const uint8_t* mac,
  esp_now_send_status_t status
) {
  (void)mac;
  (void)status;
}

// --------------------------------------------------
// INICIAR ESP-NOW
// --------------------------------------------------

bool iniciarRadio() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  if (esp_wifi_set_channel(
    1,
    WIFI_SECOND_CHAN_NONE
  ) != ESP_OK) {
    return false;
  }

  if (esp_now_init() != ESP_OK) {
    return false;
  }

  esp_now_register_recv_cb(callbackRecepcion);
  esp_now_register_send_cb(callbackEnvio);

  esp_now_peer_info_t peer = {};

  memcpy(peer.peer_addr, broadcastMac, 6);

  peer.channel = 1;
  peer.encrypt = false;

  if (!esp_now_is_peer_exist(broadcastMac)) {
    if (esp_now_add_peer(&peer) != ESP_OK) {
      return false;
    }
  }

  return true;
}

// --------------------------------------------------
// PROCESAR PAQUETES RECIBIDOS
// --------------------------------------------------

void procesarPaqueteRecibido() {
  Paquete p;
  bool hayPaquete = false;
  uint32_t descartados = 0;

  portENTER_CRITICAL(&mux);

  if (cantidadCola > 0) {
    memcpy(
      &p,
      &colaPaquetes[cabezaCola],
      sizeof(Paquete)
    );

    cabezaCola = (cabezaCola + 1) % TAM_COLA;
    cantidadCola--;

    hayPaquete = true;
  }

  descartados = paquetesDescartados;
  paquetesDescartados = 0;

  portEXIT_CRITICAL(&mux);

  if (descartados > 0) {
    Serial.print("PAQUETES DESCARTADOS: ");
    Serial.println(descartados);
  }

  if (!hayPaquete) return;

  if (crc16(p) != p.crc) {
    Serial.println("CRC INCORRECTO");

    enviarControl(NACK, p.id, p.crc);
    mascota.notificarError("CRC incorrecto");

    return;
  }

  if (p.tipo != DATOS) return;

  p.texto[p.longitud] = '\0';

  bool nuevo = mascota.registrarComunicacionExitosa(
    p.origen,
    p.id
  );

  enviarControl(ACK, p.id, p.crc);

  Serial.println("MENSAJE RECIBIDO");
  Serial.print("DESDE: D");
  Serial.println(p.origen);
  Serial.print("TEXTO: ");
  Serial.println(p.texto);
  Serial.println("CRC: OK");
  Serial.println("ACK ENVIADO");

  if (nuevo) {
    mascota.notificarMensaje(p.origen, String(p.texto));

    mostrarEstado(
      "MENSAJE DE D" + String(p.origen),
      String(p.texto),
      "CRC: OK",
      "ACK ENVIADO"
    );
  }
}

// --------------------------------------------------
// ENVIAR MENSAJE
// --------------------------------------------------

bool enviarMensaje() {
  if (mensaje.isEmpty()) {
    mostrarEstado("MENSAJE VACIO");
    return false;
  }

  Paquete p = {};

  p.tipo = DATOS;
  p.origen = DEVICE_ID;
  p.destino = ID_DESTINO;
  p.id = nuevoId();

  if (!p.id) return false;

  String s = mensaje.substring(0, MAX_TEXTO);

  p.longitud = s.length();
  s.toCharArray(p.texto, sizeof(p.texto));

  p.crc = crc16(p);

  Serial.println("ENVIANDO MENSAJE");
  Serial.println(s);
  Serial.println("CRC CALCULADO");
  Serial.println("ESPERANDO ACK");

  for (uint8_t intento = 1; intento <= 3; intento++) {
    portENTER_CRITICAL(&mux);

    respuestaLista = false;
    respuestaAck = false;
    respuestaId = 0;
    respuestaCrc = 0;

    portEXIT_CRITICAL(&mux);

    mascota.notificarEnvio();

    mostrarEstado(
      "TRANSMITIENDO",
      "ID " + String(p.id),
      "Intento " + String(intento),
      "Esperando ACK"
    );

    esp_err_t e = esp_now_send(
      broadcastMac,
      (const uint8_t*)&p,
      sizeof(p)
    );

    if (e == ESP_OK) {
      unsigned long inicio = millis();

      while (millis() - inicio < 1800) {
        bool lista, ack;
        uint32_t id;
        uint16_t crc;

        portENTER_CRITICAL(&mux);

        lista = respuestaLista;
        ack = respuestaAck;
        id = respuestaId;
        crc = respuestaCrc;

        portEXIT_CRITICAL(&mux);

        if (lista && id == p.id && crc == p.crc) {
          if (ack) {
            mascota.registrarComunicacionExitosa(
              DEVICE_ID,
              p.id
            );

            mascota.notificarEntregaConfirmada();

            Serial.println("ACK RECIBIDO");
            Serial.println("TRANSMISION EXITOSA");

            mostrarEstado(
              "TRANSMISION OK",
              "ACK RECIBIDO"
            );

            mensaje = "";

            delay(900);

            if (!pantallaMascotaActiva) {
              mostrarEditor();
            }

            return true;
          }

          Serial.println("NACK RECIBIDO");
          mascota.notificarError("NACK recibido");

          break;
        }

        delay(10);
      }
    }

    if (intento < 3) {
      Serial.println("SIN CONFIRMACION. RETRANSMITIENDO");

      mascota.notificarRetransmision(intento + 1);

      mostrarEstado(
        "REINTENTANDO",
        "Intento " + String(intento + 1)
      );

      delay(500);
    }
  }

  mascota.notificarError("Entrega fallida");

  Serial.println("TRANSMISION FALLIDA");
  Serial.println("NO LLEGO ACK");

  mostrarEstado(
    "TRANSMISION FALLIDA",
    "No llego ACK"
  );

  delay(1000);

  if (!pantallaMascotaActiva) {
    mostrarEditor();
  }

  return false;
}

// --------------------------------------------------
// FUNCIONES DEL TECLADO
// --------------------------------------------------
void finalizarMultitap() {
  teclaMultitapPendiente = -1;
  indiceLetraMultitap = 0;
  tiempoUltimaTecla = 0;
}

void actualizarEditor() {
  if (!pantallaMascotaActiva) mostrarEditor();
}

void agregarCaracterMultitap(uint8_t numeroBoton) {
  // numeroBoton: 0 a 7, correspondiente a botones fisicos 1 a 8.
  unsigned long ahora = millis();
  const char* grupo = letrasBoton[numeroBoton];
  uint8_t cantidad = strlen(grupo);

  if (teclaMultitapPendiente == numeroBoton &&
      (ahora - tiempoUltimaTecla) <= MULTITAP_MS &&
      !mensaje.isEmpty()) {
    indiceLetraMultitap = (indiceLetraMultitap + 1) % cantidad;
    mensaje.setCharAt(mensaje.length() - 1, grupo[indiceLetraMultitap]);
  } else {
    finalizarMultitap();
    if (mensaje.length() < MAX_TEXTO) {
      mensaje += grupo[0];
      teclaMultitapPendiente = numeroBoton;
      indiceLetraMultitap = 0;
    }
  }

  tiempoUltimaTecla = ahora;
  actualizarEditor();
}

void procesarPulsacionCorta(uint8_t numeroBoton) {
  if (pantallaMascotaActiva) {
    // Dentro de la mascota, los botones tienen funciones de cuidado.
    if (numeroBoton >= 1 && numeroBoton <= 5) {
      mascota.realizarAccion(numeroBoton, pantalla);
    } else if (numeroBoton == 9 && mascota.estaFallecida()) {
      mascota.realizarAccion(9, pantalla);
    }
    return;
  }

  if (numeroBoton >= 1 && numeroBoton <= 8) {
    agregarCaracterMultitap(numeroBoton - 1);
  } else if (numeroBoton == 9) {
    finalizarMultitap();
    if (mensaje.length() < MAX_TEXTO) mensaje += ' ';
    actualizarEditor();
  }
}

void procesarPulsacionLarga(uint8_t numeroBoton) {
  if (pantallaMascotaActiva) {
    if (numeroBoton == 8) {
      // Mantener 8 dentro de la mascota vuelve al editor.
      pantallaMascotaActiva = false;
      finalizarMultitap();
      mostrarEditor();
      Serial.println("MASCOTA: regreso al teclado");
    }
    return;
  }

  if (numeroBoton == 8) {
    // Con el mensaje vacio, mantener 8 entra a la mascota.
    finalizarMultitap();
    if (mensaje.isEmpty()) {
      pantallaMascotaActiva = true;
      mascota.dibujar(pantalla);
      Serial.println("MASCOTA: ingreso desde boton 8");
    } else {
      mensaje.remove(mensaje.length() - 1);
      actualizarEditor();
      Serial.println("TECLADO: ultimo caracter borrado");
    }
  } else if (numeroBoton == 9) {
    // Mantener 9 envia el mensaje.
    finalizarMultitap();
    Serial.println("TECLADO: enviar mensaje");
    enviarMensaje();
  }
}

void leerBotones() {
  unsigned long ahora = millis();

  // Si vencio la ventana multitap, se confirma la letra actual.
  if (teclaMultitapPendiente >= 0 &&
      ahora - tiempoUltimaTecla > MULTITAP_MS) {
    finalizarMultitap();
  }

  for (uint8_t i = 0; i < 9; i++) {
    bool lectura = digitalRead(pinesBotones[i]); // HIGH libre, LOW presionado

    if (lectura != lecturaAnterior[i]) {
      lecturaAnterior[i] = lectura;
      cambioLectura[i] = ahora;
    }

    if ((ahora - cambioLectura[i]) >= DEBOUNCE_MS &&
        lectura != estadoEstable[i]) {
      estadoEstable[i] = lectura;

      if (estadoEstable[i] == LOW) {
        inicioPulsacion[i] = ahora;
        pulsacionLargaAtendida[i] = false;
      } else {
        unsigned long duracion = ahora - inicioPulsacion[i];
        if (duracion >= PULSACION_LARGA_MS) {
          procesarPulsacionLarga(i + 1);
        } else {
          procesarPulsacionCorta(i + 1);
        }
      }
    }
  }
}

// --------------------------------------------------
// SETUP
// --------------------------------------------------

void setup() {
  Serial.begin(115200);

  for (uint8_t i = 0; i < 9; i++) {
    pinMode(pinesBotones[i], INPUT_PULLUP);
    lecturaAnterior[i] = digitalRead(pinesBotones[i]);
    estadoEstable[i] = lecturaAnterior[i];
  }

  Wire.begin(OLED_SDA, OLED_SCL);
  Wire.setClock(100000);

  if (!pantalla.begin(
    SSD1306_SWITCHCAPVCC,
    OLED_ADDRESS
  )) {
    Serial.println("ERROR OLED");
    while (true) delay(1000);
  }

  mascota.iniciar();

  mostrar(
    "TFL WALKIE TALKIE",
    "DISPOSITIVO 2",
    "Iniciando ESP-NOW"
  );

  if (!iniciarRadio()) {
    mostrar("ERROR ESP-NOW");
    Serial.println("ERROR ESP-NOW");
    while (true) delay(1000);
  }

  Serial.println("DISPOSITIVO 2 LISTO");

  mostrar(
    "TFL WALKIE TALKIE",
    "DISPOSITIVO 2",
    "ESP-NOW LISTO",
    "Canal 1"
  );

  delay(1000);
  mostrarEditor();
}

// --------------------------------------------------
// LOOP
// --------------------------------------------------

void loop() {
  procesarPaqueteRecibido();
  leerBotones();

  if (pantallaMascotaActiva) {
    mascota.actualizar(pantalla, true);
  }

  if (Serial.available()) {
    char c = Serial.read();

    if (c == 'X' || c == 'x') {
      mensaje = "PRUEBA DESDE D2";

      // Conserva el modo mascota durante el envío.
      enviarMensaje();
    }

    if (c == 'M' || c == 'm') {
      finalizarMultitap();
      pantallaMascotaActiva = true;
      mascota.dibujar(pantalla);
    }

    if (c == 'T' || c == 't') {
      pantallaMascotaActiva = false;
      mostrarEditor();
    }
  }

  delay(10);
}
