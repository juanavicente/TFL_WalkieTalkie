#include "Mascota.h"
#include <Preferences.h>

#define COLOR_FONDO SSD1306_BLACK
#define COLOR_TINTA SSD1306_WHITE

namespace {
  Preferences prefsMascota;
  const uint32_t LIMITE_MUERTE = 12;

  int limitar(int valor, int minimo, int maximo) {
    if (valor < minimo) return minimo;
    if (valor > maximo) return maximo;
    return valor;
  }

  void textoCentrado(Adafruit_SSD1306& p, int y, const String& s) {
    p.setTextSize(1);
    p.setTextColor(COLOR_TINTA);
    int16_t x1, y1; uint16_t w, h;
    p.getTextBounds(s, 0, y, &x1, &y1, &w, &h);
    int x = (128 - (int)w) / 2;
    if (x < 0) x = 0;
    p.setCursor(x, y);
    p.print(s);
  }
}

void Mascota::iniciar() {
  if (!iniciada) cargar();
  ayudaAbierta = false;
  notificacionActiva = false;
  textoNotificacion = "";
  estado = LISTO;
  detalle = "";
  ultimoFrame = millis();
  iniciada = true;
  actualizarEtapa();
}

void Mascota::cargar() {
  prefsMascota.begin("mascota", false);
  // Limpieza de una sola vez de los registros antiguos de la mascota.
  // La marca evita volver a borrar el contador en cada reinicio.
  if (!prefsMascota.getBool("reset12", false)) {
    prefsMascota.putUInt("comunicaciones", 0);
    prefsMascota.putUInt("ultimoD1", 0);
    prefsMascota.putUInt("ultimoD2", 0);
    prefsMascota.putBool("reset12", true);
  }
  contador = prefsMascota.getUInt("comunicaciones", 0);
  prefsMascota.end();
  actualizarEtapa();
}

void Mascota::guardar() {
  prefsMascota.begin("mascota", false);
  prefsMascota.putUInt("comunicaciones", contador);
  prefsMascota.end();
}

void Mascota::actualizarEtapa() {
  if (contador >= LIMITE_MUERTE) etapa = FALLECIDA;
  else if (contador >= 6) etapa = ADULTO;
  else if (contador >= 3) etapa = BEBE;
  else etapa = HUEVO;
  if (etapa == FALLECIDA) ayudaAbierta = false;
}

const char* Mascota::nombreEtapa() const {
  switch (etapa) {
    case HUEVO: return "HUEVO";
    case BEBE: return "BEBE";
    case ADULTO: return "ADULTO";
    case FALLECIDA: return "MUERTE";
    default: return "HUEVO";
  }
}

bool Mascota::estaFallecida() const { return etapa == FALLECIDA; }
uint32_t Mascota::comunicacionesExitosas() const { return contador; }

const char* Mascota::nombreEstado() const {
  switch (estado) {
    case ENVIANDO: return "ENVIANDO";
    case CONFIRMADO: return "ENTREGA OK";
    case RECIBIDO: return "RECIBIDO";
    case ERROR_COM: return "ERROR";
    case RETRANSMISION: return "REINTENTO";
    default: return "LISTO";
  }
}

void Mascota::establecerEstado(Estado e, const String& d) {
  estado = e; detalle = d; inicioEstado = millis();
}
void Mascota::notificarEnvio() { establecerEstado(ENVIANDO, "Enviando mensaje"); }
void Mascota::notificarEntregaConfirmada() { establecerEstado(CONFIRMADO, "ACK recibido"); }
void Mascota::notificarError(const String& d) { establecerEstado(ERROR_COM, d); }
void Mascota::notificarRetransmision(uint8_t n) { establecerEstado(RETRANSMISION, "Intento " + String(n)); }

bool Mascota::registrarComunicacionExitosa(byte origen, uint32_t id) {
  if ((origen != 1 && origen != 2) || id == 0 || etapa == FALLECIDA) return false;
  prefsMascota.begin("mascota", false);
  const char* clave = (origen == 1) ? "ultimoD1" : "ultimoD2";
  uint32_t ultimo = prefsMascota.getUInt(clave, 0);
  if (id <= ultimo) { prefsMascota.end(); return false; }
  prefsMascota.putUInt(clave, id);
  if (contador < LIMITE_MUERTE) contador++;
  prefsMascota.putUInt("comunicaciones", contador);
  prefsMascota.end();
  actualizarEtapa();
  return true;
}

void Mascota::notificarMensaje(byte origen, const String& texto) {
  origenNotificacion = origen;
  textoNotificacion = texto;
  inicioNotificacion = millis();
  notificacionActiva = true;
  establecerEstado(RECIBIDO, "CRC correcto");
}

void Mascota::actualizar(Adafruit_SSD1306& p, bool activo) {
  if (!iniciada) iniciar();
  unsigned long ahora = millis();
  if (ahora - ultimoFrame >= 350UL) { ultimoFrame = ahora; frame = (frame + 1) % 2; }
  if (notificacionActiva && ahora - inicioNotificacion >= 3500UL) {
    notificacionActiva = false; textoNotificacion = "";
  }
  if (estado != LISTO && ahora - inicioEstado >= 2500UL) { estado = LISTO; detalle = ""; }
  if (activo) dibujar(p);
}

void Mascota::dibujar(Adafruit_SSD1306& p) {
  p.clearDisplay(); p.setTextColor(COLOR_TINTA); p.setTextSize(1);
  if (ayudaAbierta && etapa != FALLECIDA) { dibujarAyuda(p); p.display(); return; }
  p.setCursor(2, 2); p.print(nombreEtapa());
  p.setCursor(78, 2); p.print("OK:"); p.print(contador); p.print("/12");
  p.drawLine(0, 12, 127, 12, COLOR_TINTA);
  if (etapa == FALLECIDA) {
    textoCentrado(p, 17, "LLEGO A 12 OK");
    dibujarPersonaje(p, 64, 37);
    textoCentrado(p, 52, "9: RENACER");
  } else if (notificacionActiva) dibujarNotificacion(p);
  else if (estado != LISTO) {
    textoCentrado(p, 21, nombreEstado());
    textoCentrado(p, 34, detalle.substring(0, 20));
  } else dibujarPersonaje(p, 64, 30);
  p.drawLine(0, 49, 127, 49, COLOR_TINTA);
  p.setCursor(2, 54); p.print("H:"); p.print(hambre);
  p.setCursor(43, 54); p.print("F:"); p.print(felicidad);
  p.setCursor(84, 54); p.print("E:"); p.print(energia);
  p.display();
}

void Mascota::dibujarPersonaje(Adafruit_SSD1306& p, int x, int y) {
  if (etapa == HUEVO) {
    p.drawRoundRect(x-13, y-13, 26, 30, 12, COLOR_TINTA);
    p.drawPixel(x-5, y-4, COLOR_TINTA); p.drawPixel(x+4, y+3, COLOR_TINTA);
    p.drawPixel(x-2, y+9, COLOR_TINTA); p.drawLine(x-7, y+13, x+7, y+13, COLOR_TINTA); return;
  }
  if (etapa == FALLECIDA) {
    p.drawRoundRect(x-18, y-14, 36, 28, 10, COLOR_TINTA);
    p.drawLine(x-10, y-5, x-5, y, COLOR_TINTA); p.drawLine(x-5, y, x, y-5, COLOR_TINTA);
    p.drawLine(x+4, y-5, x+9, y, COLOR_TINTA); p.drawLine(x+9, y, x+14, y-5, COLOR_TINTA);
    p.drawRoundRect(x-5, y+4, 10, 6, 3, COLOR_TINTA); return;
  }
  int cy = y + (frame ? -2 : 0);
  p.drawLine(x-8, cy+15, x-13, cy+18, COLOR_TINTA); p.drawLine(x-13, cy+18, x-5, cy+18, COLOR_TINTA);
  p.drawLine(x+8, cy+15, x+13, cy+18, COLOR_TINTA); p.drawLine(x+13, cy+18, x+5, cy+18, COLOR_TINTA);
  p.fillRoundRect(x-17, cy-17, 34, 33, 15, COLOR_TINTA);
  p.fillRoundRect(x-10, cy+1, 20, 12, 7, COLOR_FONDO); p.drawRoundRect(x-10, cy+1, 20, 12, 7, COLOR_TINTA);
  p.fillCircle(x-6, cy-7, 2, COLOR_FONDO); p.fillCircle(x+6, cy-7, 2, COLOR_FONDO);
  p.fillTriangle(x-3, cy-2, x+3, cy-2, x, cy+2, COLOR_FONDO);
  if (etapa == BEBE) p.drawPixel(x, cy-20, COLOR_TINTA);
  else { p.drawLine(x-5, cy-20, x, cy-24, COLOR_TINTA); p.drawLine(x, cy-24, x+5, cy-20, COLOR_TINTA); }
}

void Mascota::dibujarAyuda(Adafruit_SSD1306& p) {
  textoCentrado(p, 1, "AYUDA MASCOTA"); p.drawLine(0, 11, 127, 11, COLOR_TINTA);
  p.setCursor(3, 15); p.print("1 Comer"); p.setCursor(68, 15); p.print("2 Jugar");
  p.setCursor(3, 27); p.print("3 Dormir"); p.setCursor(68, 27); p.print("4 Acariciar");
  p.setCursor(3, 39); p.print("8 Volver al teclado");
  p.setCursor(3, 51); p.print("5 Cerrar ayuda");
}

void Mascota::dibujarNotificacion(Adafruit_SSD1306& p) {
  p.drawRoundRect(3, 16, 122, 29, 4, COLOR_TINTA);
  p.setCursor(8, 20); p.print("MENSAJE DE D"); p.print(origenNotificacion);
  String s = textoNotificacion; if (s.length() > 19) s = s.substring(0, 19);
  p.setCursor(8, 33); p.print(s);
}

void Mascota::realizarAccion(int tecla, Adafruit_SSD1306& p) {
  if (!iniciada) iniciar();
  if (tecla == 5 && etapa != FALLECIDA) { ayudaAbierta = !ayudaAbierta; dibujar(p); return; }
  if (tecla == 8) { ayudaAbierta = false; dibujar(p); return; }
  if (tecla == 9 && etapa == FALLECIDA) {
    // Renacer: reinicia edad/necesidades y contador de vida, conserva IDs anti-duplicados.
    contador = 0; guardar(); etapa = HUEVO;
    hambre = 25; felicidad = 75; energia = 80;
    ayudaAbierta = false; notificacionActiva = false; estado = LISTO;
    detalle = ""; dibujar(p); return;
  }
  if (etapa == FALLECIDA || ayudaAbierta) { dibujar(p); return; }
  switch (tecla) {
    case 1: hambre = limitar(hambre - 30, 0, 100); felicidad = limitar(felicidad + 2, 0, 100); break;
    case 2: felicidad = limitar(felicidad + 20, 0, 100); energia = limitar(energia - 10, 0, 100); hambre = limitar(hambre + 5, 0, 100); break;
    case 3: energia = limitar(energia + 30, 0, 100); hambre = limitar(hambre + 3, 0, 100); break;
    case 4: felicidad = limitar(felicidad + 12, 0, 100); break;
    default: break;
  }
  dibujar(p);
}
