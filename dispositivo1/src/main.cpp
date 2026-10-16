#include <Arduino.h>
#include <string.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ======================================================
// CONFIGURACIÓN DEL DISPOSITIVO
// ======================================================

#define ID_DISPOSITIVO 1

#if ID_DISPOSITIVO == 1
const byte ID_DESTINO = 2;
const char* NOMBRE_DISPOSITIVO = "DISPOSITIVO 1";
const char* CLIENT_ID = "TFL_D1";
const char* TOPICO_RECEPCION = "tfl_walkietalkie_2026_d1";
const char* TOPICO_ENVIO = "tfl_walkietalkie_2026_d2";
const char* TOPICO_ACK_PROPIO = "tfl_walkietalkie_2026_ack_d1";
const char* TOPICO_ACK_DESTINO = "tfl_walkietalkie_2026_ack_d2";
#else
const byte ID_DESTINO = 1;
const char* NOMBRE_DISPOSITIVO = "DISPOSITIVO 2";
const char* CLIENT_ID = "TFL_D2";
const char* TOPICO_RECEPCION = "tfl_walkietalkie_2026_d2";
const char* TOPICO_ENVIO = "tfl_walkietalkie_2026_d1";
const char* TOPICO_ACK_PROPIO = "tfl_walkietalkie_2026_ack_d2";
const char* TOPICO_ACK_DESTINO = "tfl_walkietalkie_2026_ack_d1";
#endif

// ======================================================
// WIFI
// ======================================================

const char* SSID = "Wokwi-GUEST";
const char* PASSWORD = "";

// ======================================================
// MQTT
// ======================================================

const char* MQTT_BROKER = "broker.hivemq.com";
const int MQTT_PUERTO = 1883;
WiFiClient espClient;
PubSubClient mqtt(espClient);

// ======================================================
// OLED
// ======================================================

#define ANCHO_PANTALLA 128
#define ALTO_PANTALLA 64

Adafruit_SSD1306 pantalla(ANCHO_PANTALLA, ALTO_PANTALLA, &Wire, -1);

// ======================================================
// TECLADO 3x3
// ======================================================

const byte filas[3] = {13, 14, 16};
const byte columnas[3] = {25, 32, 27};

const char teclas[3][3] = {
    {'1', '2', '3'},
    {'4', '5', '6'},
    {'7', '8', '9'}
};

// ======================================================
// MULTITAP
// ======================================================

const char* caracteres[] = {
    "ABC",
    "DEF",
    "GHI",
    "JKL",
    "MNO",
    "PQRS",
    "TUV",
    "WXYZ"
};

String mensaje = "";
const int MAX_MENSAJE = 100;
int ultimaTecla = -1;
int indiceCaracter = 0;
unsigned long ultimoToque = 0;
const unsigned long TIEMPO_MULTITAP = 900;
const unsigned long TIEMPO_MANTENER = 1000;

// ======================================================
// PAQUETE
// ======================================================

struct Paquete {
    byte origen;
    byte destino;
    uint8_t longitud;
    char datos[MAX_MENSAJE + 1];
    uint16_t crc;
};

// ======================================================
// ESTADO ACK
// ======================================================

volatile bool ackRecibido = false;
volatile bool ackCorrecto = false;
uint16_t crcEsperado = 0;

// ======================================================
// DEMOSTRACIÓN DE ERROR
// ======================================================

const bool DEMOSTRAR_RETRANSMISION = false;

// ======================================================
// OLED - MOSTRAR TEXTO
// ======================================================

void mostrarTexto(String linea1, String linea2 = "", String linea3 = "", String linea4 = "") {
    pantalla.clearDisplay();
    pantalla.setTextColor(SSD1306_WHITE);
    pantalla.setTextSize(1);
    pantalla.setCursor(0, 0);
    pantalla.println(linea1);
    pantalla.setCursor(0, 16);
    pantalla.println(linea2);
    pantalla.setCursor(0, 32);
    pantalla.println(linea3);
    pantalla.setCursor(0, 48);
    pantalla.println(linea4);
    pantalla.display();
}

// ======================================================
// MOSTRAR MENSAJE ACTUAL
// ======================================================

void mostrarMensajeActual() {
    pantalla.clearDisplay();
    pantalla.setTextColor(SSD1306_WHITE);
    pantalla.setTextSize(1);
    pantalla.setCursor(0, 0);
    pantalla.println("MENSAJE:");
    String texto = mensaje;
    if (texto.length() > 84) {
        texto = texto.substring(texto.length() - 84);
    }
    for (int i = 0; i < 4; i++) {
        int inicio = i * 21;
        if (inicio >= texto.length()) {
            break;
        }
        String linea = texto.substring(inicio, min(inicio + 21, (int)texto.length()));
        pantalla.setCursor(0, 16 + i * 12);
        pantalla.println(linea);
    }
    pantalla.display();
}

// ======================================================
// CRC-16
// ======================================================

uint16_t calcularCRC(byte origen, byte destino, uint8_t longitud, const char* datos) {
    uint16_t crc = 0x0000;
    auto procesarByte = [&](uint8_t dato) {
        crc ^= ((uint16_t)dato << 8);
        for (byte i = 0; i < 8; i++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x8005;
            } else {
                crc <<= 1;
            }
        }
    };
    procesarByte(origen);
    procesarByte(destino);
    procesarByte(longitud);
    for (int i = 0; i < longitud; i++) {
        procesarByte(datos[i]);
    }
    return crc;
}

// ======================================================
// CONSTRUIR PAQUETE
// ======================================================

Paquete construirPaquete(String texto) {
    Paquete paquete;
    paquete.origen = ID_DISPOSITIVO;
    paquete.destino = ID_DESTINO;
    if (texto.length() > MAX_MENSAJE) {
        texto = texto.substring(0, MAX_MENSAJE);
    }
    paquete.longitud = texto.length();
    texto.toCharArray(paquete.datos, MAX_MENSAJE + 1);
    paquete.crc = calcularCRC(paquete.origen, paquete.destino, paquete.longitud, paquete.datos);
    return paquete;
}

// ======================================================
// SERIALIZAR PAQUETE
// ======================================================

String serializarPaquete(Paquete paquete) {
    String resultado = "";
    resultado += String(paquete.origen);
    resultado += ",";
    resultado += String(paquete.destino);
    resultado += ",";
    resultado += String(paquete.longitud);
    resultado += ",";
    resultado += String(paquete.crc);
    resultado += ",";
    resultado += String(paquete.datos);
    return resultado;
}

// ======================================================
// CORROMPER PAQUETE
// ======================================================

String corromperPaquete(String paquete) {
    int p1 = paquete.indexOf(',');
    int p2 = paquete.indexOf(',', p1 + 1);
    int p3 = paquete.indexOf(',', p2 + 1);
    int p4 = paquete.indexOf(',', p3 + 1);
    int posicionDatos = p4 + 1;
    if (p4 >= 0 && posicionDatos < paquete.length()) {
        char original = paquete.charAt(posicionDatos);
        if (original == 'X') {
            paquete.setCharAt(posicionDatos, 'Z');
        } else {
            paquete.setCharAt(posicionDatos, 'X');
        }
    }
    return paquete;
}

// ======================================================
// VERIFICAR PAQUETE RECIBIDO
// ======================================================

bool verificarPaquete(String paquete, Paquete &resultado) {
    int p1 = paquete.indexOf(',');
    if (p1 < 0) return false;
    int p2 = paquete.indexOf(',', p1 + 1);
    if (p2 < 0) return false;
    int p3 = paquete.indexOf(',', p2 + 1);
    if (p3 < 0) return false;
    int p4 = paquete.indexOf(',', p3 + 1);
    if (p4 < 0) return false;
    String textoOrigen = paquete.substring(0, p1);
    String textoDestino = paquete.substring(p1 + 1, p2);
    String textoLongitud = paquete.substring(p2 + 1, p3);
    String textoCRC = paquete.substring(p3 + 1, p4);
    if (textoOrigen.length() == 0 || textoDestino.length() == 0 ||
        textoLongitud.length() == 0 || textoCRC.length() == 0) return false;
    long origen = textoOrigen.toInt();
    long destino = textoDestino.toInt();
    long longitud = textoLongitud.toInt();
    long crcRecibido = textoCRC.toInt();
    if ((origen != 1 && origen != 2) || (destino != 1 && destino != 2) ||
        longitud < 0 || longitud > MAX_MENSAJE || crcRecibido < 0 || crcRecibido > 65535) return false;
    String datos = paquete.substring(p4 + 1);
    if (datos.length() != (unsigned int)longitud) return false;
    resultado.origen = (byte)origen;
    resultado.destino = (byte)destino;
    resultado.longitud = (uint8_t)longitud;
    resultado.crc = (uint16_t)crcRecibido;
    datos.toCharArray(resultado.datos, MAX_MENSAJE + 1);
    uint16_t crcCalculado = calcularCRC(resultado.origen, resultado.destino, resultado.longitud, resultado.datos);
    return crcCalculado == resultado.crc;
}

// ======================================================
// RECIBIR PAQUETE MQTT
// ======================================================

void procesarPaqueteRecibido(String contenido) {
    Paquete paquete;
    bool crcCorrecto = verificarPaquete(contenido, paquete);
    if (!crcCorrecto) {
        mostrarTexto("ERROR CRC", "Paquete rechazado", "Enviando NACK");
        int p3 = contenido.indexOf(',', contenido.indexOf(',') + 1);
        p3 = contenido.indexOf(',', p3 + 1);
        int p4 = contenido.indexOf(',', p3 + 1);
        if (p3 >= 0 && p4 >= 0) {
            String crc = contenido.substring(p3 + 1, p4);
            String respuesta = "NACK:" + crc;
            mqtt.publish(TOPICO_ACK_DESTINO, respuesta.c_str());
        }
        return;
    }
    if (paquete.destino != ID_DISPOSITIVO) {
        return;
    }
    mostrarTexto("PAQUETE RECIBIDO", "Desde D" + String(paquete.origen), paquete.datos, "CRC OK");
    String respuesta = "ACK:" + String(paquete.crc);
    mqtt.publish(TOPICO_ACK_DESTINO, respuesta.c_str());
}

// ======================================================
// CALLBACK MQTT
// ======================================================

void callbackMQTT(char* topic, byte* payload, unsigned int length) {
    String contenido = "";
    for (unsigned int i = 0; i < length; i++) {
        contenido += (char)payload[i];
    }
    String topicString = String(topic);
    if (topicString == TOPICO_ACK_PROPIO) {
        if (contenido.startsWith("ACK:")) {
            uint16_t crc = contenido.substring(4).toInt();
            if (crc == crcEsperado) {
                ackCorrecto = true;
                ackRecibido = true;
            }
        } else if (contenido.startsWith("NACK:")) {
            uint16_t crc = contenido.substring(5).toInt();
            if (crc == crcEsperado) {
                ackCorrecto = false;
                ackRecibido = true;
            }
        }
        return;
    }
    if (topicString == TOPICO_RECEPCION) {
        procesarPaqueteRecibido(contenido);
    }
}

// ======================================================
// WIFI
// ======================================================

void conectarWiFi() {
    mostrarTexto(NOMBRE_DISPOSITIVO, "Conectando WiFi...");
    WiFi.begin(SSID, PASSWORD, 6);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
    }
}

// ======================================================
// MQTT
// ======================================================

void conectarMQTT() {
    while (!mqtt.connected()) {
        mostrarTexto(NOMBRE_DISPOSITIVO, "Conectando MQTT...");
        if (mqtt.connect(CLIENT_ID)) {
            mqtt.subscribe(TOPICO_RECEPCION);
            mqtt.subscribe(TOPICO_ACK_PROPIO);
            mostrarTexto(NOMBRE_DISPOSITIVO, "SISTEMA LISTO", "Esperando mensaje...");
            delay(1000);
        } else {
            delay(1000);
        }
    }
}

// ======================================================
// TRANSMISIÓN
// ======================================================

bool transmitirPaquete(Paquete paquete) {
    String paqueteOriginal = serializarPaquete(paquete);
    crcEsperado = paquete.crc;
    const int MAX_INTENTOS = 3;
    for (int intento = 1; intento <= MAX_INTENTOS; intento++) {
        ackRecibido = false;
        ackCorrecto = false;
        mostrarTexto("TRANSMITIENDO", "Intento " + String(intento), "Enviando paquete...");
        delay(1500);
        String paqueteAEnviar = paqueteOriginal;
        if (DEMOSTRAR_RETRANSMISION && intento < 3) {
            paqueteAEnviar = corromperPaquete(paqueteOriginal);
        }
        mqtt.publish(TOPICO_ENVIO, paqueteAEnviar.c_str());
        unsigned long inicio = millis();
        while (!ackRecibido && millis() - inicio < 2500) {
            mqtt.loop();
            delay(10);
        }
        if (ackRecibido && ackCorrecto) {
            mostrarTexto("TRANSMISION OK", "ACK RECIBIDO", "Intento " + String(intento));
            delay(1500);
            return true;
        }
        if (ackRecibido && !ackCorrecto) {
            mostrarTexto("ERROR DETECTADO", "CRC incorrecto", "Retransmitiendo...");
            delay(1500);
            continue;
        }
        mostrarTexto("SIN RESPUESTA", "Timeout", "Retransmitiendo...");
        delay(1500);
    }
    mostrarTexto("TRANSMISION", "FALLIDA", "3 intentos");
    delay(1500);
    return false;
}

// ======================================================
// ENVIAR MENSAJE
// ======================================================

void enviarMensaje() {
    if (mensaje.length() == 0) {
        mostrarTexto("MENSAJE VACIO", "Escriba un mensaje");
        delay(1000);
        return;
    }
    Paquete paquete = construirPaquete(mensaje);
    bool enviado = transmitirPaquete(paquete);
    if (enviado) {
        mensaje = "";
        ultimaTecla = -1;
        indiceCaracter = 0;
    }
    mostrarMensajeActual();
}

// ======================================================
// BORRAR MENSAJE
// ======================================================

void borrarMensaje() {
    if (mensaje.length() > 0) {
        mensaje.remove(mensaje.length() - 1);
    }
    ultimaTecla = -1;
    indiceCaracter = 0;
    mostrarMensajeActual();
}

// ======================================================
// MULTITAP
// ======================================================

void procesarTeclaMultitap(int numero) {
    int indice = numero - 1;
    if (indice < 0 || indice > 7) {
        return;
    }
    const char* grupo = caracteres[indice];
    int cantidad = strlen(grupo);
    unsigned long ahora = millis();
    if (ultimaTecla == numero && ahora - ultimoToque < TIEMPO_MULTITAP && mensaje.length() > 0) {
        indiceCaracter++;
        if (indiceCaracter >= cantidad) {
            indiceCaracter = 0;
        }
        mensaje.setCharAt(mensaje.length() - 1, grupo[indiceCaracter]);
    } else {
        indiceCaracter = 0;
        if (mensaje.length() < MAX_MENSAJE) {
            mensaje += grupo[0];
        }
    }
    ultimaTecla = numero;
    ultimoToque = ahora;
    mostrarMensajeActual();
}

// ======================================================
// PROCESAR TECLA
// ======================================================

void procesarTecla(int numero, unsigned long duracion) {
    if (numero == 8) {
        if (duracion >= TIEMPO_MANTENER) {
            borrarMensaje();
        } else {
            procesarTeclaMultitap(8);
        }
        return;
    }
    if (numero == 9) {
        if (duracion >= TIEMPO_MANTENER) {
            enviarMensaje();
        } else {
            if (mensaje.length() < MAX_MENSAJE) {
                mensaje += " ";
            }
            ultimaTecla = -1;
            indiceCaracter = 0;
            mostrarMensajeActual();
        }
        return;
    }
    procesarTeclaMultitap(numero);
}

// ======================================================
// ESCANEAR TECLADO
// ======================================================

int leerTecla() {
    for (int fila = 0; fila < 3; fila++) {
        for (int i = 0; i < 3; i++) {
            digitalWrite(filas[i], HIGH);
        }
        digitalWrite(filas[fila], LOW);
        delayMicroseconds(50);
        for (int columna = 0; columna < 3; columna++) {
            if (digitalRead(columnas[columna]) == LOW) {
                return fila * 3 + columna + 1;
            }
        }
    }
    return 0;
}

// ======================================================
// SETUP
// ======================================================

void setup() {
    Wire.begin(21, 22);
    if (!pantalla.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        while (true);
    }
    for (int i = 0; i < 3; i++) {
        pinMode(filas[i], OUTPUT);
        digitalWrite(filas[i], HIGH);
        pinMode(columnas[i], INPUT_PULLUP);
    }
    mostrarTexto(NOMBRE_DISPOSITIVO, "Iniciando...");
    delay(1000);
    conectarWiFi();
    mqtt.setServer(MQTT_BROKER, MQTT_PUERTO);
    mqtt.setCallback(callbackMQTT);
    conectarMQTT();
    delay(1000);
    mostrarMensajeActual();
}

// ======================================================
// LOOP
// ======================================================

void loop() {
    if (WiFi.status() != WL_CONNECTED) {
        conectarWiFi();
    }
    if (!mqtt.connected()) {
        conectarMQTT();
    }
    mqtt.loop();
    static int teclaActual = 0;
    static int ultimaTeclaLeida = 0;
    static unsigned long inicioTecla = 0;
    int tecla = leerTecla();
    if (tecla != 0 && ultimaTeclaLeida == 0) {
        teclaActual = tecla;
        inicioTecla = millis();
        if (tecla <= 7) {
            procesarTecla(tecla, 0);
        }
    }
    if (tecla == 0 && ultimaTeclaLeida != 0) {
        unsigned long duracion = millis() - inicioTecla;
        if (teclaActual == 8 || teclaActual == 9) {
            procesarTecla(teclaActual, duracion);
        }
        teclaActual = 0;
    }
    ultimaTeclaLeida = tecla;
    delay(30);
}