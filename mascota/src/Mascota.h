#ifndef MASCOTA_TFL_H
#define MASCOTA_TFL_H

#include <Arduino.h>
#include <Adafruit_SSD1306.h>

class Mascota {
public:
  void iniciar();
  void actualizar(Adafruit_SSD1306& pantalla, bool modoActivo);
  void dibujar(Adafruit_SSD1306& pantalla);
  void realizarAccion(int tecla, Adafruit_SSD1306& pantalla);

  void notificarMensaje(byte origen, const String& texto);
  void notificarEnvio();
  void notificarEntregaConfirmada();
  void notificarError(const String& detalle);
  void notificarRetransmision(uint8_t intento);

  bool registrarComunicacionExitosa(byte origen, uint32_t idMensaje);
  bool estaFallecida() const;
  const char* nombreEtapa() const;
  const char* nombreEstado() const;
  uint32_t comunicacionesExitosas() const;

private:
  enum Etapa { HUEVO, BEBE, ADULTO, FALLECIDA };
  enum Estado { LISTO, ENVIANDO, CONFIRMADO, RECIBIDO, ERROR_COM, RETRANSMISION };

  Etapa etapa = HUEVO;
  Estado estado = LISTO;
  uint32_t contador = 0;

  bool iniciada = false;
  bool ayudaAbierta = false;
  bool notificacionActiva = false;
  byte origenNotificacion = 0;

  int hambre = 25;
  int felicidad = 75;
  int energia = 80;
  int frame = 0;

  String textoNotificacion;
  String detalle;
  unsigned long ultimoFrame = 0;
  unsigned long inicioNotificacion = 0;
  unsigned long inicioEstado = 0;

  void cargar();
  void guardar();
  void actualizarEtapa();
  void establecerEstado(Estado e, const String& d = "");
  void dibujarPersonaje(Adafruit_SSD1306& pantalla, int x, int y);
  void dibujarAyuda(Adafruit_SSD1306& pantalla);
  void dibujarNotificacion(Adafruit_SSD1306& pantalla);
};

#endif
