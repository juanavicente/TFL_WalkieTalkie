#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <LoRa.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// =========================
// OLED
// =========================
#define ANCHO_PANTALLA 128
#define ALTO_PANTALLA 64

Adafruit_SSD1306 pantalla(
    ANCHO_PANTALLA,
    ALTO_PANTALLA,
    &Wire,
    -1
);

// =========================
// MATRIZ 3x3
// =========================
const byte filas[3] = {13, 14, 16};
const byte columnas[3] = {25, 32, 27};

const char teclas[3][3] = {
    {'1', '2', '3'},
    {'4', '5', '6'},
    {'7', '8', '9'}
};

// =========================
// LETRAS
// =========================
const char* letras[8] = {
    "ABC",
    "DEF",
    "GHI",
    "JKL",
    "MNO",
    "PQRS",
    "TUV",
    "WXYZ"
};

// =========================
// MENSAJE
// =========================
String mensaje = "";
const int MAX_MENSAJE = 100;

// =========================
// MULTITAP
// =========================
char ultimaTecla = '\0';
int indiceLetra = 0;
unsigned long tiempoUltimaTecla = 0;
const unsigned long TIEMPO_MULTITAP = 900;

// =========================
// BOTÓN MANTENIDO
// =========================
unsigned long tiempoPresionada = 0;
const unsigned long TIEMPO_MANTENIDO = 1000;


// ======================================================
// ETAPA 3
// REPRESENTACIÓN Y PAQUETE
// ======================================================

// Identificadores de los dispositivos
const byte ID_DISPOSITIVO = 1;
const byte ID_DESTINO = 2;

// LoRa SX1278
#define LORA_SS   5
#define LORA_RST  17
#define LORA_DIO0 26
#define FRECUENCIA_LORA 433E6

void iniciarLoRa() {

    LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);

    if (!LoRa.begin(FRECUENCIA_LORA)) {

        pantalla.clearDisplay();
        pantalla.setTextColor(SSD1306_WHITE);
        pantalla.setTextSize(1);

        pantalla.setCursor(0, 0);
        pantalla.println("ERROR LORA");

        pantalla.setCursor(0, 15);
        pantalla.println("Modulo no encontrado");

        pantalla.display();

        while (true) {
            delay(100);
        }
    }

    LoRa.setTxPower(17);

    Serial.println("LoRa iniciado");
}

// -------------------------
// Estructura del paquete
// -------------------------
struct Paquete {

    byte origen;
    byte destino;
    uint8_t longitud;
    char datos[MAX_MENSAJE + 1];

    uint16_t crc;
};


// Paquete que vamos a construir
Paquete paquete;

//========================
// CRC-16
//========================
uint16_t calcularCRC16(
    const uint8_t* datos,
    size_t longitud
) {

    uint16_t crc = 0x0000;

    for (size_t i = 0; i < longitud; i++) {

        crc ^= (uint16_t)datos[i] << 8;

        for (int bit = 0; bit < 8; bit++) {

            if (crc & 0x8000) {

                crc = (crc << 1) ^ 0x8005;

            } else {

                crc <<= 1;
            }
        }
    }

    return crc;
}

// =========================
// VERIFICAR CRC
// =========================
bool verificarCRC(const Paquete& paquete) {

    uint8_t buffer[MAX_MENSAJE + 3];

    // Reconstruimos los datos
    // que fueron protegidos por el CRC

    buffer[0] = paquete.origen;
    buffer[1] = paquete.destino;
    buffer[2] = paquete.longitud;

    memcpy(
        &buffer[3],
        paquete.datos,
        paquete.longitud
    );

    // Calculamos nuevamente el CRC

    uint16_t crcCalculado = calcularCRC16(
        buffer,
        paquete.longitud + 3
    );

    // Comparamos con el CRC
    // que viene dentro del paquete

    return crcCalculado == paquete.crc;
}

// =========================
// DISPOSITIVO 2 - RECEPCIÓN
// =========================
bool recibirPaquete(const Paquete& paqueteRecibido) {

    pantalla.clearDisplay();
    pantalla.setTextColor(SSD1306_WHITE);
    pantalla.setTextSize(1);

    pantalla.setCursor(0, 0);
    pantalla.println("DISPOSITIVO 2");

    pantalla.setCursor(0, 15);
    pantalla.println("Recibiendo...");

    pantalla.display();

    delay(1000);


    // ---------------------------------
    // Verificamos el CRC
    // ---------------------------------

    if (verificarCRC(paqueteRecibido)) {

        pantalla.clearDisplay();

        pantalla.setCursor(0, 0);
        pantalla.println("MENSAJE RECIBIDO");

        pantalla.setCursor(0, 15);
        pantalla.println(paqueteRecibido.datos);

        pantalla.display();

        delay(2000);

        // CRC correcto = ACK
        return true;

    } else {

        pantalla.clearDisplay();

        pantalla.setCursor(0, 0);
        pantalla.println("ERROR DE CRC");

        pantalla.setCursor(0, 15);
        pantalla.println("Sin ACK");

        pantalla.display();

        delay(2000);

        // CRC incorrecto = no ACK
        return false;
    }
}

// =========================
// TRANSMISIÓN LoRa SIMULADA
// =========================
void transmitirLoRaSimulado(const Paquete& paqueteTransmitido) {

    const int MAX_INTENTOS = 3;

    bool recibidoCorrectamente = false;


    // =================================
    // Intentos de transmisión
    // =================================

    for (int intento = 1; intento <= MAX_INTENTOS; intento++) {

        pantalla.clearDisplay();

        pantalla.setTextColor(SSD1306_WHITE);
        pantalla.setTextSize(1);

        pantalla.setCursor(0, 0);
        pantalla.println("LoRa");

        pantalla.setCursor(0, 15);
        pantalla.print("Intento ");
        pantalla.println(intento);

        pantalla.setCursor(0, 30);
        pantalla.println("Transmitiendo...");

        pantalla.display();

        delay(1000);


        // ---------------------------------
        // Copiamos el paquete transmitido
        // ---------------------------------

        Paquete paqueteRecibido = paqueteTransmitido;


        // ---------------------------------
        // Simulamos errores en los primeros
        // dos intentos
        // ---------------------------------

        if (intento < 3 && paqueteRecibido.longitud > 0) {

            paqueteRecibido.datos[0] = 'X';
        }


        // ---------------------------------
        // El dispositivo 2 recibe
        // ---------------------------------

        bool ack = recibirPaquete(paqueteRecibido);


        // ---------------------------------
        // ¿Recibimos ACK?
        // ---------------------------------

        if (ack) {

            pantalla.clearDisplay();

            pantalla.setTextColor(SSD1306_WHITE);
            pantalla.setTextSize(1);

            pantalla.setCursor(0, 0);
            pantalla.println("ACK RECIBIDO");

            pantalla.setCursor(0, 15);
            pantalla.println("Mensaje enviado");

            pantalla.display();

            delay(2000);

            recibidoCorrectamente = true;

            break;
        }


        // ---------------------------------
        // No hubo ACK
        // ---------------------------------

        pantalla.clearDisplay();

        pantalla.setTextColor(SSD1306_WHITE);
        pantalla.setTextSize(1);

        pantalla.setCursor(0, 0);
        pantalla.println("SIN ACK");

        pantalla.setCursor(0, 15);
        pantalla.println("Retransmitiendo...");

        pantalla.display();

        delay(1500);
    }


    // =================================
    // Si agotamos los intentos
    // =================================

    if (!recibidoCorrectamente) {

        pantalla.clearDisplay();

        pantalla.setTextColor(SSD1306_WHITE);
        pantalla.setTextSize(1);

        pantalla.setCursor(0, 0);
        pantalla.println("TRANSMISION");

        pantalla.setCursor(0, 15);
        pantalla.println("FALLIDA");

        pantalla.display();

        delay(3000);
    }
}

// -------------------------
// Construir paquete
// -------------------------
void construirPaquete() {

    // -------------------------
    // Datos del paquete
    // -------------------------
    paquete.origen = ID_DISPOSITIVO;

    paquete.destino = ID_DESTINO;

    paquete.longitud = mensaje.length();

    mensaje.toCharArray(
        paquete.datos,
        MAX_MENSAJE + 1
    );


    // -------------------------
    // Construimos los datos
    // que serán protegidos por CRC
    // -------------------------
    uint8_t buffer[MAX_MENSAJE + 3];

    buffer[0] = paquete.origen;

    buffer[1] = paquete.destino;

    buffer[2] = paquete.longitud;


    // Copiamos el mensaje
    memcpy(
        &buffer[3],
        paquete.datos,
        paquete.longitud
    );


    // -------------------------
    // Calculamos UN solo CRC
    // sobre todo el contenido
    // -------------------------
    paquete.crc = calcularCRC16(
        buffer,
        paquete.longitud + 3
    );
}

// ======================================================
// FIN ETAPA 3
// ======================================================

// =========================
// LEER MATRIZ
// =========================
char leerTecla() {

    for (int fila = 0; fila < 3; fila++) {

        digitalWrite(filas[fila], LOW);

        for (int columna = 0; columna < 3; columna++) {

            if (digitalRead(columnas[columna]) == LOW) {

                digitalWrite(filas[fila], HIGH);

                return teclas[fila][columna];
            }
        }

        digitalWrite(filas[fila], HIGH);
    }

    return '\0';
}

// =========================
// MOSTRAR MENSAJE
// =========================
void mostrarMensaje() {

    pantalla.clearDisplay();

    pantalla.setTextColor(SSD1306_WHITE);
    pantalla.setTextSize(1);

    pantalla.setCursor(0, 0);
    pantalla.println("Mensaje:");

    pantalla.setCursor(0, 12);

    if (mensaje.length() <= 80) {

        pantalla.println(mensaje);

    } else {

        pantalla.println(
            mensaje.substring(
                mensaje.length() - 80
            )
        );
    }

    pantalla.display();
}

// =========================
// BORRAR
// =========================
void borrarUltimoCaracter() {

    if (mensaje.length() > 0) {

        mensaje.remove(
            mensaje.length() - 1
        );
    }

    ultimaTecla = '\0';

    mostrarMensaje();
}

// =========================
// MOSTRAR PAQUETE
// =========================
void mostrarPaquete() {

    Serial.println();
    Serial.println("========== PAQUETE ==========");

    Serial.print("SRC  : ");
    Serial.println(paquete.origen);

    Serial.print("DEST : ");
    Serial.println(paquete.destino);

    Serial.print("LEN  : ");
    Serial.println(paquete.longitud);

    Serial.print("DATA : ");
    Serial.println(paquete.datos);

    Serial.print("CRC  : 0x");

    if (paquete.crc < 0x1000) Serial.print("0");
    if (paquete.crc < 0x0100) Serial.print("0");
    if (paquete.crc < 0x0010) Serial.print("0");

    Serial.println(paquete.crc, HEX);

    Serial.println("=============================");
    Serial.println();
}

// =========================
// PRUEBA DEL CRC
// =========================
void probarCRC() {

    // -------------------------
    // Verificación original
    // -------------------------

    construirPaquete();

    bool crcOriginalCorrecto =
        verificarCRC(paquete);


    // -------------------------
    // Simulamos un error
    // -------------------------

    char datoOriginal = paquete.datos[0];

    paquete.datos[0] = 'X';

    bool errorDetectado =
        !verificarCRC(paquete);


    // Restauramos el dato
    paquete.datos[0] = datoOriginal;


    // -------------------------
    // Mostrar resultado
    // -------------------------

    pantalla.clearDisplay();

    pantalla.setTextColor(SSD1306_WHITE);
    pantalla.setTextSize(1);

    pantalla.setCursor(0, 0);
    pantalla.println("PRUEBA CRC");

    pantalla.setCursor(0, 15);

    if (crcOriginalCorrecto) {

        pantalla.println("Original: OK");

    } else {

        pantalla.println("Original: ERROR");
    }

    pantalla.setCursor(0, 30);

    if (errorDetectado) {

        pantalla.println("Alterado: ERROR");

    } else {

        pantalla.println("Alterado: NO");
    }

    pantalla.display();

    delay(3000);

    mostrarMensaje();
}

// =========================
// ENVIAR
// =========================
void enviarMensaje() {
    construirPaquete();

    probarCRC();

    mostrarPaquete();

    transmitirLoRaSimulado(paquete);
}

// =========================
// PROCESAR TECLA
// =========================
void procesarTecla(char tecla) {

    // -------------------------
    // ESPACIO
    // -------------------------
    if (tecla == '9') {

        if (mensaje.length() < MAX_MENSAJE) {

            mensaje += ' ';
        }

        ultimaTecla = '\0';
        mostrarMensaje();

        return;
    }


    // -------------------------
    // LETRAS
    // -------------------------
    int numero = tecla - '1';

    const char* grupo = letras[numero];
    int cantidad = strlen(grupo);
    if (
        tecla == ultimaTecla &&
        millis() - tiempoUltimaTecla < TIEMPO_MULTITAP
    ) {

        indiceLetra++;

        if (indiceLetra >= cantidad) {
            indiceLetra = 0;
        }

        if (mensaje.length() > 0) {
            mensaje.setCharAt(
                mensaje.length() - 1,
                grupo[indiceLetra]
            );
        }

    } else {
        indiceLetra = 0;
        if (mensaje.length() < MAX_MENSAJE) {
            mensaje += grupo[indiceLetra];
        }
    }


    ultimaTecla = tecla;
    tiempoUltimaTecla = millis();
    mostrarMensaje();
}

// =========================
// SETUP
// =========================
void setup() {

    // -------------------------
    // Serial
    // -------------------------
    Serial.begin(115200);

    Serial.println(
        "Sistema iniciado"
    );


    // -------------------------
    // OLED
    // -------------------------
    Wire.begin(21, 22);

    pantalla.begin(
        SSD1306_SWITCHCAPVCC,
        0x3C
    );

    pantalla.clearDisplay();

    pantalla.setTextColor(SSD1306_WHITE);

    pantalla.setTextSize(2);

    pantalla.setCursor(0, 0);
    pantalla.println("TFL");

    pantalla.setTextSize(1);

    pantalla.setCursor(0, 25);
    pantalla.println("Escriba mensaje");

    pantalla.display();


    // -------------------------
    // Filas
    // -------------------------
    for (int i = 0; i < 3; i++) {

        pinMode(
            filas[i],
            OUTPUT
        );

        digitalWrite(
            filas[i],
            HIGH
        );
    }


    // -------------------------
    // Columnas
    // -------------------------

    for (int i = 0; i < 3; i++) {

        pinMode(
            columnas[i],
            INPUT_PULLUP
        );
    }
}

// =========================
// LOOP
// =========================
void loop() {

    char tecla = leerTecla();

    static char teclaAnterior = '\0';


    // -------------------------
    // Nueva pulsación
    // -------------------------
    if (
        tecla != '\0' &&
        teclaAnterior == '\0'
    ) {

        tiempoPresionada = millis();

        procesarTecla(tecla);
    }


    // -------------------------
    // Botón mantenido
    // -------------------------
    if (
        tecla != '\0' &&
        tecla == teclaAnterior &&
        millis() - tiempoPresionada >
        TIEMPO_MANTENIDO
    ) {

        if (tecla == '8') {

            borrarUltimoCaracter();

            tiempoPresionada =
                millis() + 100000;
        }

        else if (tecla == '9') {

            enviarMensaje();

            tiempoPresionada =
                millis() + 100000;
        }
    }


    // -------------------------
    // Botón soltado
    // -------------------------
    if (tecla == '\0') {

        tiempoPresionada = 0;
    }


    teclaAnterior = tecla;

    delay(20);
}