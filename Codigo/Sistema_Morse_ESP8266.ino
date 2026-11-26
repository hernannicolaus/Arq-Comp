// Bibliotecas para Wi-Fi, servidor web, audio y LCD.
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <math.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
// Pines: pulsador D6, buzzer D5, microfono A0 y LED integrado del ESP8266 (D4).
#define BOTON 12
#define LED 2
#define BUZZER 14
#define MIC A0
// LCD I2C: SDA en D2 y SCL en D1.
#define LCD_SDA 4
#define LCD_SCL 5
const uint8_t LCD_DIRECCION = 0x27;
LiquidCrystal_I2C lcd(LCD_DIRECCION, 16, 2);
// Configuracion Wi-Fi en archivo local no publicado.
#include "secrets.h"
const unsigned long WIFI_REINTENTO_MS = 15000;
const unsigned long WIFI_ESPERA_INICIAL_MS = 12000;
unsigned long ultimoIntentoWiFi = 0;
bool wifiEstabaConectado = false;
// Frecuencias y muestreo para recepcion y transmision Morse.
const int FREQ_MIC = 1500, FREQ_BUZZER = 2500, N = 80; const float FS = 4000.0f;
// Duraciones de puntos, rayas y pausas entre letras y palabras.
const unsigned long UNIDAD = 150; const unsigned long RAYA_BOTON = 300; const unsigned long RAYA_MIC = 300;
const unsigned long LETRA_BOTON = 550; const unsigned long PALABRA_BOTON = 1400;
const unsigned long LETRA_MIC = 460; const unsigned long PALABRA_MIC = 2100;
// Umbrales y filtros para identificar tonos validos.
const unsigned long SILENCIO_BUZZER = 300;
const bool DIAGNOSTICO_DETALLADO = false;
ESP8266WebServer server(80); float energia = 0, umbral = 8.0f; const float UMBRAL_APAGADO = 3.5f;
const unsigned long CONFIRMAR_INICIO_MS = 25; const unsigned long CONFIRMAR_SILENCIO_MS = 45;
const unsigned long PULSO_MIN_MS = 70; const unsigned long PULSO_MAX_MS = 650; float picoPulso = 0;
unsigned long inicioCandidato = 0, silencioCandidato = 0; unsigned long ultimoCambioMuestra = 0;
unsigned int pulsosDescartados = 0; int adcMin = 1023, adcMax = 0, adcMedio = 0, amplitud = 0;
// Estados de entrada, recepcion y transmision.
bool microON = false, tono = false, botonON = false;
bool botonAnterior = false, txON = false, txSonando = false; bool espacioB = true, espacioM = true;
// Pausa de transmision: conserva la fase del simbolo y el tiempo restante.
bool txPausado = false, txPausaSonando = false;
unsigned long txPausaInicio = 0, txRestante = 0;
// Contadores y tiempos de los eventos del sistema.
int positivos = 0, negativos = 0; unsigned long inicioTono = 0, ultimoTono = 0, inicioBoton = 0;
unsigned long ultimoBoton = 0, cambioBoton = 0, duracion = 0; unsigned long siguienteTX = 0, ultimaMuestra = 0;
unsigned long duracionMicro = 0; unsigned long ultimoBuzzer = 0; bool bloqueoBotonTX = false;
// Controles de pulsaciones largas y avisos sonoros.
const unsigned long PULSACION_LARGA_MS = 5000; const unsigned long BUZZER_MANUAL_MAX_MS = 600;
bool pulsacionLargaAplicada = false; bool buzzerManualSilenciado = false; bool avisoActivo = false;
uint8_t faseAviso = 0; unsigned long momentoAviso = 0; bool inicioListo = false; uint8_t faseArranque = 0;
unsigned long momentoArranque = 0;
// Secuencias Morse y textos de boton, microfono y transmisor.
String secB="", secM="", textoB="", textoM="", ultimoB="", ultimoM="";
String colaTX="";
// Historiales y estados de presentacion del LCD.
String lcdMorseB="", lcdMorseM="", lcdTextoB="", lcdTextoM="";
char lcdOrigen = 'B';
bool lcdHayEntrada = false, lcdDisponible = false;
unsigned long lcdUltimaActualizacion = 0, lcdMensajeHasta = 0, lcdUltimaEntrada = 0;
const unsigned long LCD_ESPERA_MS = 5000;
const unsigned long LCD_ALTERNAR_MS = 4500;
String lcdFilaAnterior0="", lcdFilaAnterior1="";

// Texto y progreso Morse de las transmisiones web en LCD.
String lcdTXTexto="", lcdTXMorse="";
bool lcdTXVisible = false;
unsigned long lcdTXInicio = 0, lcdTXHasta = 0;
const unsigned long LCD_TX_FINAL_MS = 3500;
const unsigned long LCD_SCROLL_MS = 400;

// Completa cada fila hasta 16 caracteres.
String lcdRellenar(String texto) {
  if (texto.length() > 16) texto = texto.substring(0, 16);
  while (texto.length() < 16) texto += ' ';
  return texto;
}
// Centra mensajes en el LCD.
String lcdCentrar(String texto) {
  if (texto.length() > 16) texto = texto.substring(0,16);
  unsigned int izquierda = (16 - texto.length()) / 2;
  String resultado = "";
  while (resultado.length() < izquierda) resultado += ' ';
  return resultado + texto;
}
// Actualiza las filas solo cuando cambia su contenido.
void lcdMostrarFilas(String arriba, String abajo) {
  if (!lcdDisponible) return;
  arriba = lcdRellenar(arriba); abajo = lcdRellenar(abajo);
  if (arriba != lcdFilaAnterior0) {
    lcd.setCursor(0, 0); lcd.print(arriba); lcdFilaAnterior0 = arriba;
  }
  if (abajo != lcdFilaAnterior1) {
    lcd.setCursor(0, 1); lcd.print(abajo); lcdFilaAnterior1 = abajo;
  }
}
// Muestra avisos centrados durante un tiempo determinado.
void lcdAvisoCentrado(String arriba, String abajo, unsigned long tiempo) {
  lcdMostrarFilas(lcdCentrar(arriba), lcdCentrar(abajo));
  lcdMensajeHasta = millis() + tiempo;
}
// Reinicia el historial exclusivo del LCD.
void lcdLimpiarHistorial() {
  lcdMorseB = ""; lcdMorseM = "";
  lcdTextoB = ""; lcdTextoM = "";
  lcdHayEntrada = false;
}
// Acumula los simbolos recibidos en el historial visual.
void lcdAnotar(char origen, String simbolo) {
  unsigned long ahora = millis();
  if (lcdHayEntrada && ahora - lcdUltimaEntrada >= LCD_ESPERA_MS) lcdLimpiarHistorial();
  String &historial = (origen == 'B') ? lcdMorseB : lcdMorseM;
  historial += simbolo;
  if (historial.length() > 48) historial.remove(0, historial.length() - 48);
  lcdOrigen = origen;
  lcdHayEntrada = true;
  lcdUltimaEntrada = ahora;
  lcdMensajeHasta = 0;
}
// Agrega letras decodificadas al historial visual.
void lcdAnotarTexto(char origen, char letra) {
  String &historial = (origen == 'B') ? lcdTextoB : lcdTextoM;
  historial += letra;
  if (historial.length() > 48) historial.remove(0, historial.length() - 48);
}
// Muestra el estado del microfono en pantalla.
void lcdAvisoMicro() {
  lcdAvisoCentrado("MICROFONO", microON ? "ACTIVADO" : "DESACTIVADO", 2200);
}
// Desplaza los mensajes largos de transmision.
String lcdVentanaMovil(const String &texto, unsigned long ahora) {
  if (texto.length() <= 12) return texto;
  String circular = texto + "   ";
  unsigned int comienzo = ((ahora - lcdTXInicio) / LCD_SCROLL_MS) % circular.length();
  String visible = "";
  for (unsigned int i = 0; i < 12; ++i) visible += circular[(comienzo + i) % circular.length()];
  return visible;
}
// Gestiona recepcion, transmision y pantallas de espera del LCD.
void lcdActualizar(unsigned long ahora) {
  if (!lcdDisponible || tono || avisoActivo) return;
  if (ahora - lcdUltimaActualizacion < 180) return;
  lcdUltimaActualizacion = ahora;

  if (lcdTXVisible) {
    if (txON || (long)(ahora - lcdTXHasta) < 0) {
      unsigned long ahoraVisual = txPausado ? txPausaInicio : ahora;
      String morse = lcdTXMorse;
      if (morse.length() > 12) morse = morse.substring(morse.length() - 12);
      lcdMostrarFilas("TXT:" + lcdVentanaMovil(lcdTXTexto, ahoraVisual), "MOR:" + morse);
      return;
    }
    lcdTXVisible = false;
    lcdTXTexto = ""; lcdTXMorse = "";
    lcdLimpiarHistorial();
  }

  if (lcdMensajeHasta != 0 && (long)(ahora - lcdMensajeHasta) < 0) return;
  if (lcdMensajeHasta != 0) lcdMensajeHasta = 0;
  if (lcdHayEntrada && ahora - lcdUltimaEntrada >= LCD_ESPERA_MS) lcdLimpiarHistorial();
  if (!lcdHayEntrada) {
    if (WiFi.status() == WL_CONNECTED && ((ahora / LCD_ALTERNAR_MS) % 2 == 1)) {
      lcdMostrarFilas(lcdCentrar("IP MORSELAB"), lcdCentrar(WiFi.localIP().toString()));
    } else {
      lcdMostrarFilas(lcdCentrar("EN ESPERA"), lcdCentrar(microON ? "MICROFONO: ON" : "MICROFONO: OFF"));
    }
    return;
  }
  String morse = lcdOrigen == 'B' ? lcdMorseB : lcdMorseM;
  String texto = lcdOrigen == 'B' ? lcdTextoB : lcdTextoM;
  if (morse.length() > 12) morse = morse.substring(morse.length() - 12);
  if (texto.length() > 12) texto = texto.substring(texto.length() - 12);
  lcdMostrarFilas("MOR:" + morse, "TXT:" + texto);
}
// Diccionario de caracteres y sus equivalentes Morse.
const char letras[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
const char* codigos[] = {
  ".-","-...","-.-.","-..",".","..-.","--.","....","..",".---",
  "-.-",".-..","--","-.","---",".--.","--.-",".-.","...","-",
  "..-","...-",".--","-..-","-.--","--..",
  "-----",".----","..---","...--","....-",".....",
  "-....","--...","---..","----."
};
// Convierte un caracter a Morse.
String codificar(char c) {
  c = toupper((unsigned char)c);
  for (int i = 0; i < 36; i++)
    if (letras[i] == c) return codigos[i];
  return "";
}
// Convierte una secuencia Morse en un caracter.
char decodificar(String s) {
  for (int i = 0; i < 36; i++)
    if (s == codigos[i]) return letras[i];
  return '?';
}
// Convierte un mensaje completo a Morse.
String convertir(String texto) {
  texto.toUpperCase();
  String salida = "";
  bool anterior = false, espacio = false;
  for (unsigned int i = 0; i < texto.length(); i++) {
    if (texto[i] == ' ') {
      if (anterior) espacio = true;
      continue;
    }
    String codigo = codificar(texto[i]);
    if (!codigo.length()) continue;
    if (anterior) salida += espacio ? "/" : " ";
    salida += codigo; anterior = true; espacio = false;
  }
  return salida;
}
// Agrega un punto o una raya recibidos.
void agregar(String &sec, bool punto) {
  if (sec.length() >= 12) sec = "";
  sec += punto ? "." : "-";
  lcdAnotar((&sec == &secB) ? 'B' : 'M', punto ? "." : "-");
  Serial.println(punto ? "[RX] PUNTO" : "[RX] RAYA");
}
// Decodifica una letra y la agrega al mensaje.
void finalizar(String &sec, String &texto, String &ultimo) {
  if (!sec.length()) return;
  ultimo = sec;
  char c = decodificar(sec); texto += c;
  char origen = (&sec == &secB) ? 'B' : 'M';
  lcdAnotar(origen, " ");
  lcdAnotarTexto(origen, c);
  if (texto.length() > 100)
    texto.remove(0, texto.length() - 100);
  Serial.println("[RX] " + sec + " -> " + String(c));
  sec = "";
}
// Separa palabras al detectar una pausa prolongada.
void pausa(String &sec, String &texto, bool &espacio,
           unsigned long ultimo, unsigned long limite,
           unsigned long ahora) {
  if (!sec.length() && !espacio && texto.length() &&
      ahora - ultimo >= limite) {
    texto += " ";
    char origen = (&sec == &secB) ? 'B' : 'M';
    lcdAnotar(origen, "/ ");
    lcdAnotarTexto(origen, ' ');
    if (texto.length() > 100) texto.remove(0, texto.length() - 100);
    espacio = true;
  }
}
// Datos del detector de frecuencia del microfono.
float ventanaGoertzel[N]; float coefGoertzel = 0;
// Prepara los coeficientes para detectar el tono.
void prepararGoertzel() {
  const float omega = 2.0f * PI * FREQ_MIC / FS;
  coefGoertzel = 2.0f * cosf(omega);
  for (int i = 0; i < N; i++) {
    ventanaGoertzel[i] = 0.5f - 0.5f * cosf(2.0f * PI * i / (N - 1));
  }
}
// Calcula la energia de la frecuencia recibida.
float goertzel() {
  float muestras[N], media = 0; int minimo = 1023, maximo = 0;
  unsigned long t = micros();
  for (int i = 0; i < N; i++) {
    while ((long)(micros() - t) < 0) yield();
    int lectura = analogRead(MIC); muestras[i] = lectura;
    if (lectura < minimo) minimo = lectura;
    if (lectura > maximo) maximo = lectura;
    media += muestras[i]; t += 250;
  }
  media /= N; adcMin = minimo; adcMax = maximo; adcMedio = (int)media; amplitud = maximo - minimo;
  float q0 = 0, q1 = 0, q2 = 0;
  for (int i = 0; i < N; i++) {
    q0 = (muestras[i] - media) * ventanaGoertzel[i] + coefGoertzel * q1 - q2; q2 = q1; q1 = q0;
  }
  float potencia = q1*q1 + q2*q2 - coefGoertzel*q1*q2;
  return 4.0f * sqrtf(fmaxf(0.0f, potencia)) / N;
}
// Reinicia el estado de recepcion del microfono.
void reiniciarMicro() {
  tono = false; positivos = negativos = 0; energia = 0; adcMin = 1023; adcMax = 0; adcMedio = 0; amplitud = 0;
  duracionMicro = 0; inicioCandidato = silencioCandidato = 0; picoPulso = 0;
  secM = "";
  digitalWrite(LED, HIGH);
}
// Reconoce tonos y silencios como puntos o rayas.
void leerMicro() {
  if (!microON || txON || botonON || bloqueoBotonTX || avisoActivo ||
      millis() - ultimoBuzzer < SILENCIO_BUZZER) return;
  if (millis() - ultimaMuestra < 25) return;
  ultimaMuestra = millis();
  energia = goertzel();
  unsigned long ahora = millis();
  if (!tono) {
    if (energia >= umbral) {
      if (inicioCandidato == 0) inicioCandidato = ahora;
      if (ahora - inicioCandidato >= CONFIRMAR_INICIO_MS) {
        tono = true; inicioTono = inicioCandidato; picoPulso = energia; silencioCandidato = 0;
        digitalWrite(LED, LOW);
      }
    } else {
      inicioCandidato = 0;
    }
    return;
  }
  if (energia > picoPulso) picoPulso = energia;
  if (energia <= UMBRAL_APAGADO) {
    if (silencioCandidato == 0) silencioCandidato = ahora;
    if (ahora - silencioCandidato < CONFIRMAR_SILENCIO_MS) return;
    tono = false;
    digitalWrite(LED, HIGH); duracionMicro = silencioCandidato - inicioTono; ultimoTono = silencioCandidato;
    inicioCandidato = 0; silencioCandidato = 0;
    if (duracionMicro >= PULSO_MIN_MS && duracionMicro <= PULSO_MAX_MS) {
      bool punto = duracionMicro < RAYA_MIC;
      agregar(secM, punto); espacioM = false;
      Serial.printf("[MIC] %lu ms -> %s | Pico %.2f\n",
                    duracionMicro, punto ? "PUNTO" : "RAYA", picoPulso);
    } else {
      pulsosDescartados++;
      Serial.printf("[MIC] DESCARTADO: %lu ms | Pico %.2f | revisar audio/cable\n",
                    duracionMicro, picoPulso);
      secM = "";
      espacioM = true;
    }
    picoPulso = 0;
  } else {
    silencioCandidato = 0;
    if (ahora - inicioTono > PULSO_MAX_MS + 120) {
      tono = false; inicioCandidato = 0; silencioCandidato = 0;
      digitalWrite(LED, HIGH); pulsosDescartados++;
      secM = "";
      espacioM = true; ultimoTono = ahora;
      Serial.printf("[MIC] DESCARTADO: tono continuo > %lu ms | Pico %.2f\n",
                    PULSO_MAX_MS, picoPulso);
      picoPulso = 0;
    }
  }
}
// Inicia los pitidos de confirmacion.
void iniciarAvisoMicro() {
  if (avisoActivo || txON || !inicioListo) return;
  avisoActivo = true; faseAviso = 0;
  momentoAviso = millis();
  tone(BUZZER, FREQ_BUZZER);
  digitalWrite(LED, LOW); ultimoBuzzer = momentoAviso;
}
// Controla los pitidos de confirmacion.
void procesarAvisoMicro(unsigned long ahora) {
  if (!avisoActivo) return;
  if (faseAviso == 0 && ahora - momentoAviso >= 110) {
    noTone(BUZZER);
    digitalWrite(LED, HIGH); ultimoBuzzer = ahora; faseAviso = 1; momentoAviso = ahora;
  } else if (faseAviso == 1 && ahora - momentoAviso >= 160) {
    tone(BUZZER, FREQ_BUZZER);
    digitalWrite(LED, LOW); ultimoBuzzer = ahora; faseAviso = 2; momentoAviso = ahora;
  } else if (faseAviso == 2 && ahora - momentoAviso >= 110) {
    noTone(BUZZER);
    digitalWrite(LED, HIGH); ultimoBuzzer = ahora; avisoActivo = false;
  }
}
// Detecta pulsaciones Morse y alterna el microfono con 5 segundos.
void leerBoton(unsigned long ahora) {
  bool lectura = digitalRead(BOTON) == LOW;
  if (txON) {
    bloqueoBotonTX = true; botonON = false; botonAnterior = lectura; cambioBoton = ahora; return;
  }
  if (bloqueoBotonTX) {
    if (!lectura) {
      bloqueoBotonTX = false; botonAnterior = false; cambioBoton = ahora;
    }
    return;
  }
  if (lectura != botonAnterior) {
    cambioBoton = ahora; botonAnterior = lectura;
  }
  if (ahora - cambioBoton >= 25 && lectura != botonON) {
    botonON = lectura;
    if (botonON) {
      reiniciarMicro(); inicioBoton = ahora; pulsacionLargaAplicada = false; buzzerManualSilenciado = false;
      tone(BUZZER, FREQ_BUZZER);
      digitalWrite(LED, LOW); ultimoBuzzer = ahora;
    } else {
      if (!avisoActivo) {
        noTone(BUZZER);
        digitalWrite(LED, HIGH); ultimoBuzzer = ahora;
      }
      duracion = ahora - inicioBoton;
      if (!pulsacionLargaAplicada && duracion >= 40) {
        agregar(secB, duracion < RAYA_BOTON); ultimoBoton = ahora; espacioB = false;
      }
      pulsacionLargaAplicada = false; buzzerManualSilenciado = false;
    }
  }
  if (!botonON) return;
  if (!buzzerManualSilenciado && ahora - inicioBoton >= BUZZER_MANUAL_MAX_MS) {
    noTone(BUZZER); ultimoBuzzer = ahora;
    digitalWrite(LED, HIGH); buzzerManualSilenciado = true;
  }
  if (!pulsacionLargaAplicada && ahora - inicioBoton >= PULSACION_LARGA_MS) {
    pulsacionLargaAplicada = true;
    noTone(BUZZER); ultimoBuzzer = ahora;
    digitalWrite(LED, HIGH); buzzerManualSilenciado = true; microON = !microON;
    reiniciarMicro(); ultimoTono = ahora; ultimaMuestra = ahora; espacioM = true;
    lcdAvisoMicro();
    iniciarAvisoMicro();
    Serial.printf("[BOTON] Pulsacion larga 5 s: microfono %s\n",
                  microON ? "ACTIVADO" : "DESACTIVADO");
  }
}
// Prepara la transmision del mensaje enviado desde la web.
void iniciarTX(String texto) {
  texto.trim();
  if (texto.length() > 60) texto = texto.substring(0, 60);
  colaTX = convertir(texto); txON = colaTX.length() > 0; txSonando = false;
  txPausado = false; txPausaSonando = false; txRestante = 0;
  if (txON) {
    lcdTXTexto = texto;
    lcdTXTexto.toUpperCase();
    lcdTXMorse = "";
    lcdTXVisible = true;
    lcdTXInicio = millis();
    lcdTXHasta = lcdTXInicio;
    lcdMensajeHasta = 0;
  }
  siguienteTX = millis();
  reiniciarMicro();
  ultimoBuzzer = millis();
}
// Pausa el buzzer y conserva el tiempo pendiente del simbolo o silencio.
void pausarTX(unsigned long ahora) {
  if (!txON || txPausado) return;
  txRestante = (long)(siguienteTX - ahora) > 0 ? siguienteTX - ahora : 0;
  txPausaSonando = txSonando;
  txPausado = true;
  txPausaInicio = ahora;
  noTone(BUZZER);
  digitalWrite(LED, HIGH);
  ultimoBuzzer = ahora;
}
// Reanuda la transmision desde el instante exacto en que se pauso.
void reanudarTX(unsigned long ahora) {
  if (!txON || !txPausado) return;
  lcdTXInicio += ahora - txPausaInicio; // Congela tambien el desplazamiento del LCD.
  siguienteTX = ahora + txRestante;
  if (txPausaSonando && txRestante > 0) {
    tone(BUZZER, FREQ_BUZZER);
    digitalWrite(LED, LOW);
    ultimoBuzzer = ahora;
  }
  txPausado = false;
}
// Emite los simbolos Morse y actualiza su progreso en LCD.
void procesarTX(unsigned long ahora) {
  if (!txON || txPausado || (long)(ahora - siguienteTX) < 0) return;
  if (txSonando) {
    noTone(BUZZER); ultimoBuzzer = ahora;
    digitalWrite(LED, HIGH); txSonando = false; siguienteTX = ahora + UNIDAD; return;
  }
  if (!colaTX.length()) {
    txON = false;
    lcdTXHasta = ahora + LCD_TX_FINAL_MS;
    noTone(BUZZER); ultimoBuzzer = ahora; ultimoTono = ahora; return;
  }
  char c = colaTX[0];
  colaTX.remove(0, 1);
  if (lcdTXVisible) {
    lcdTXMorse += c;
    if (lcdTXMorse.length() > 120) lcdTXMorse.remove(0, lcdTXMorse.length() - 120);
  }
  if (c == '.' || c == '-') {
    unsigned long tiempo = c == '.' ? UNIDAD : 3 * UNIDAD;
    tone(BUZZER, FREQ_BUZZER); ultimoBuzzer = ahora;
    digitalWrite(LED, LOW); txSonando = true; siguienteTX = ahora + tiempo;
  } else {
    siguienteTX = ahora + (c == '/' ? 6 : 2) * UNIDAD;
  }
}
// Interfaz web integrada: HTML, CSS y JavaScript.
const char WEB[] PROGMEM = R"HTML(
<!doctype html><html lang="es"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>MorseLab ESP8266</title>
/* Estilos de la interfaz web. */
<style>
*{box-sizing:border-box}body{margin:0;background:#f1f5fa;color:#17253c;font:13px Arial,sans-serif}
header{background:#fff;border-bottom:1px solid #dce4ee}nav{max-width:1370px;margin:auto;min-height:58px;padding:9px 17px;display:flex;align-items:center;justify-content:space-between;gap:14px}
.logo{font-size:22px;font-weight:800}.logo span{color:#2563eb}.right{display:flex;align-items:center;gap:9px;position:relative}
.status{font-size:12px;background:#dcfce7;color:#166534;padding:7px 11px;border-radius:20px}
.info-button{width:30px;height:30px;border-radius:50%;border:1px solid #94a3b8;background:white;color:#2563eb;font-size:17px;font-weight:700;padding:0;cursor:pointer}
.popover{position:absolute;right:0;top:43px;width:260px;max-width:calc(100vw - 24px);background:white;border:1px solid #cbd5e1;border-radius:12px;box-shadow:0 10px 30px #17253c25;padding:12px 14px;z-index:5}
.popover[hidden]{display:none}.pop-title{font-weight:bold;margin:0 0 7px}.pop-row{display:flex;justify-content:space-between;padding:7px 0;gap:10px;border-bottom:1px solid #edf1f6}.pop-row:last-child{border:0}
main{max-width:1370px;margin:16px auto;padding:0 14px}.grid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:14px;align-items:stretch}
.card{background:#fff;border:1px solid #e2e8f0;border-radius:13px;padding:15px;min-width:0;display:flex;flex-direction:column;min-height:318px}
h2{font-size:17px;margin:0}.card-head{display:flex;justify-content:space-between;align-items:center;gap:8px}
p{color:#64748b;line-height:1.4;margin:9px 0}.label{font-size:10px;color:#64748b;letter-spacing:.5px;font-weight:700}
.result{background:#f8fafc;border:1px solid #e2e8f0;border-radius:9px;padding:12px;min-height:107px;margin:12px 0}
.msg{font-size:22px;line-height:1.3;font-weight:750;overflow-wrap:anywhere;margin:8px 0}.code{font:16px Consolas,monospace;color:#2563eb;overflow-wrap:anywhere}
.row{display:flex;align-items:center;justify-content:space-between;border-bottom:1px solid #edf1f6;padding:10px 0;gap:10px}.row:last-child{border:0}
button{border:0;border-radius:8px;background:#2563eb;color:#fff;padding:11px 12px;font-size:12px;font-weight:700;cursor:pointer}button:disabled{opacity:.5;cursor:default}
.secondary{background:#e2e8f0;color:#17253c}.small-btn{padding:7px 10px} .actions{display:flex;gap:8px;margin-top:12px}.actions button{flex:1}
textarea{width:100%;min-height:105px;resize:vertical;padding:11px;margin:11px 0;border:1px solid #cbd5e1;border-radius:8px;font:14px Arial}
.preview{min-height:42px;background:#f8fafc;color:#2563eb;font:14px Consolas,monospace;padding:11px;border:1px dashed #cbd5e1;border-radius:8px;overflow-wrap:anywhere}
.bar{height:7px;background:#e2e8f0;border-radius:8px;overflow:hidden;margin:12px 0}.bar div{height:100%;width:0;background:#2563eb;transition:width .15s}
.flow-panel{margin-top:14px;background:#fff;border:1px solid #e2e8f0;border-radius:13px;padding:14px 16px}
.flow-title{font-size:14px;font-weight:700;margin-bottom:12px}
.flow-grid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:12px}
.flow-line{display:flex;align-items:center;flex-wrap:wrap;gap:6px;font-size:12px}
.flow-tag{background:#f1f5f9;color:#475569;border:1px solid #e2e8f0;border-radius:6px;padding:6px 8px;font-weight:700}
.flow-line.activo .flow-tag{border-color:#93c5fd;background:#dbeafe;color:#1d4ed8}
.flow-arrow{color:#94a3b8}
.flow-sub{font-size:11px;color:#64748b;margin:9px 0 0}
.helper{margin-top:auto;font-size:11px;color:#64748b;padding-top:14px}
footer{text-align:center;color:#94a3b8;padding:15px;font-size:11px}
/* Ajustes para pantallas pequenas. */
@media(max-width:900px){.grid{grid-template-columns:repeat(2,minmax(0,1fr))}.flow-grid{grid-template-columns:1fr}}
@media(max-width:620px){.grid{grid-template-columns:1fr}main{margin:10px auto}.card{padding:14px}}
</style></head><body>
<!-- Cabecera e informacion del sistema. -->
<header><nav><div class="logo">Morse<span>Lab</span></div><div class="right">
<span id="conexion" class="status">Conectando...</span>
<button id="infoBtn" type="button" class="info-button" aria-label="Informacion del sistema" aria-expanded="false" onclick="toggleInfo()">i</button>
<div class="popover" id="infoPanel" hidden>
<div class="pop-title">Informacion del sistema</div>
<div class="pop-row"><span>IP</span><b id="ip">—</b></div>
<div class="pop-row"><span>Microfono</span><b id="microStat">—</b></div>
<div class="pop-row"><span>Frecuencia RX</span><b>1500 Hz</b></div>
<div class="pop-row"><span>Buzzer</span><b>2500 Hz</b></div>
<div class="pop-row"><span>Velocidad</span><b>8 WPM</b></div>
<div class="pop-row"><span>Red Wi-Fi</span><b id="redWifi">—</b></div>
<div class="pop-row"><span>Transmision</span><b id="txEstado">Listo</b></div>
<div class="pop-row"><span>Umbral</span><b id="umbral">8</b></div>
</div></div></nav></header>
<!-- Paneles de boton, microfono y transmisor. -->
<main><div class="grid">
<section class="card">
<div class="card-head"><h2>Boton manual</h2><button class="secondary small-btn" onclick="limpiar('boton')">Limpiar</button></div>
<p>Ingreso de codigo Morse mediante pulsador.</p>
<div class="result"><div class="label">MENSAJE DECODIFICADO</div><div class="msg" id="textoBoton">—</div><div class="code" id="morseBoton">—</div></div>
<div class="row"><span>Ultimo codigo</span><b id="ultimoBoton">—</b></div>
<div class="row"><span>Boton</span><b id="botonEstado">Libre</b></div>
<div class="row"><span>Ultimo pulso</span><b id="duracion">0 ms</b></div>
<div class="helper">Mantené presionado 5 segundos para activar o desactivar el micrófono.</div>
</section>
<section class="card">
<div class="card-head"><h2>Microfono</h2><button class="secondary small-btn" onclick="limpiar('micro')">Limpiar</button></div>
<p>Recepcion acustica a 1500 Hz.</p>
<div class="result"><div class="label">MENSAJE RECIBIDO</div><div class="msg" id="textoSonido">—</div><div class="code" id="morseSonido">—</div></div>
<div class="row"><span>Estado</span><b id="senal">Iniciando</b></div>
<div class="row"><span>Ultimo codigo</span><b id="ultimoSonido">—</b></div>
<div class="bar"><div id="barra"></div></div>
<div class="actions"><button id="microBtn" onclick="toggleMicro()" disabled>Desactivar microfono</button></div>
</section>
<section class="card">
<div class="card-head"><h2>Transmisor Morse</h2></div>
<p>Escribi un mensaje para reproducirlo con el buzzer.</p>
<textarea id="mensaje" maxlength="60" placeholder="HOLA MUNDO" oninput="preview()"></textarea>
<div class="preview" id="prev">—</div>
<div class="actions"><button id="enviarBtn" onclick="accionTransmisor()" disabled>Transmitir</button><button class="secondary" onclick="limpiarEntrada()">Limpiar</button></div>
<p id="estadoEnvio">Transmisor listo.</p>
</section>
</div>
<!-- Indicadores del flujo del sistema. -->
<section class="flow-panel" aria-label="Flujo del sistema">
<div class="flow-title">Flujo del sistema en vivo</div>
<div class="flow-grid">
<div class="flow-line" id="flujoBoton"><span class="flow-tag">Pulsador</span><span class="flow-arrow">→</span><span class="flow-tag">Morse</span><span class="flow-arrow">→</span><span class="flow-tag">Texto</span></div>
<div class="flow-line" id="flujoMicro"><span class="flow-tag">Micrófono</span><span class="flow-arrow">→</span><span class="flow-tag">Goertzel</span><span class="flow-arrow">→</span><span class="flow-tag">Morse / texto</span></div>
<div class="flow-line" id="flujoTX"><span class="flow-tag">Texto</span><span class="flow-arrow">→</span><span class="flow-tag">Morse</span><span class="flow-arrow">→</span><span class="flow-tag">Buzzer</span></div>
</div>
<p class="flow-sub">Los bloques se resaltan cuando hay una pulsación, un tono detectado o una transmisión activa.</p>
</section>
</main><footer>MorseLab - ESP8266</footer>
// JavaScript: vista previa, controles y actualizacion del estado.
<script>
const $=id=>document.getElementById(id),abc="ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
const dic=[".-","-...","-.-.","-..",".","..-.","--.","....","..",".---","-.-",".-..","--","-.","---",".--.","--.-",".-.","...","-","..-","...-",".--","-..-","-.--","--..","-----",".----","..---","...--","....-",".....","-....","--...","---..","----."];
let micro=true,ocupado=false,actualizando=false,listo=false,transmitiendo=false,txPausado=false;
function toggleInfo(){let p=$("infoPanel");p.hidden=!p.hidden;$("infoBtn").setAttribute("aria-expanded",String(!p.hidden))}
document.addEventListener("click",e=>{if(!e.target.closest(".right")&&!$("infoPanel").hidden)toggleInfo()});
document.addEventListener("keydown",e=>{if(e.key==="Escape"&&!$("infoPanel").hidden)toggleInfo()});
// Traduccion local para la vista previa Morse.
function convertir(t){return t.toUpperCase().trim().split(/\s+/).map(p=>[...p].map(c=>dic[abc.indexOf(c)]||"").filter(Boolean).join(" ")).filter(Boolean).join(" / ")}
function preview(){$("prev").textContent=convertir($("mensaje").value)||"—"}
function limpiarEntrada(){$("mensaje").value="";preview()}
// Envia, pausa o reanuda segun el estado actual de la transmision.
async function accionTransmisor(){
  if(ocupado||!listo)return;
  if(!transmitiendo){await enviar();return}
  ocupado=true;
  try{
    let r=await fetch(txPausado?"/tx/reanudar":"/tx/pausar",{method:"POST"});
    if(!r.ok)throw Error(await r.text());
    await actualizar();
  }catch(e){$("estadoEnvio").textContent="No se pudo cambiar la pausa: "+e.message}
  finally{ocupado=false}
}
// Solicitudes para transmitir y administrar el sistema.
async function enviar(){let t=$("mensaje").value.trim();if(!t||ocupado||!listo)return;ocupado=true;
try{let r=await fetch("/enviar",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body:"texto="+encodeURIComponent(t)});$("estadoEnvio").textContent=r.ok?"Transmitiendo mensaje...":await r.text();if(r.ok)actualizar()}
catch(e){$("estadoEnvio").textContent="Error de conexion"}finally{ocupado=false}}
async function toggleMicro(){if(!listo)return;try{let r=await fetch("/micro?on="+(micro?0:1));if(r.ok)actualizar()}catch(e){}}
async function limpiar(tipo){try{let r=await fetch("/limpiar?tipo="+tipo,{method:"POST"});if(!r.ok)throw Error();actualizar()}catch(e){alert("No se pudo limpiar esta seccion")}}
// Consulta el estado del ESP8266 y actualiza la interfaz.
async function actualizar(){if(actualizando||document.hidden)return;actualizando=true;try{
const ctrl=new AbortController(), limite=setTimeout(()=>ctrl.abort(),1800);
let r;
try{r=await fetch("/estado",{cache:"no-store",signal:ctrl.signal});if(!r.ok)throw Error();}
finally{clearTimeout(limite)}
let d=await r.json();
ultimoEstadoActivo=!!(d.tono||d.botonActivo||d.transmitiendo||d.morseSonido||d.morseBoton);

micro=d.microfono;listo=d.inicioListo;transmitiendo=d.transmitiendo;txPausado=d.txPausado;
$("conexion").textContent="Conectado";$("ip").textContent=d.ip;
$("microStat").textContent=!listo?"Iniciando":micro?"Activo":"Apagado";
$("txEstado").textContent=d.txPausado?"En pausa":d.transmitiendo?"Transmitiendo":"Listo";
$("enviarBtn").disabled=!listo||ocupado||(!d.transmitiendo&&d.botonActivo);
$("enviarBtn").textContent=d.txPausado?"Reanudar":d.transmitiendo?"Pausar":"Transmitir";
if(d.transmitiendo)$("estadoEnvio").textContent=d.txPausado?"Transmision pausada.":"Transmitiendo mensaje...";
for(let k of ["textoSonido","textoBoton","morseSonido","morseBoton","ultimoSonido","ultimoBoton"])$(k).textContent=d[k]||"—";
$("umbral").textContent=d.umbral;$("redWifi").textContent=d.red||"—";
$("barra").style.width=Math.min(100,d.nivel/Math.max(d.umbral*3,1)*100)+"%";
$("senal").textContent=!listo?"Iniciando":!micro?"Desactivado":d.tono?"Tono detectado":"Silencio";
$("botonEstado").textContent=d.botonActivo?"Presionado":"Libre";
$("flujoBoton").classList.toggle("activo",d.botonActivo);
$("flujoMicro").classList.toggle("activo",d.tono && micro);
$("flujoTX").classList.toggle("activo",d.transmitiendo);
$("duracion").textContent=d.duracion+" ms";
$("microBtn").disabled=!listo;$("microBtn").textContent=micro?"Desactivar microfono":"Activar microfono";
}catch(e){$("conexion").textContent="Sin conexion"}
finally{
  actualizando=false;
  if(!document.hidden)programarConsulta(ultimoEstadoActivo?300:450);
}}
// Programa las consultas sin superponer solicitudes.
let proximaConsulta=null, ultimoEstadoActivo=false;
function programarConsulta(ms){clearTimeout(proximaConsulta);proximaConsulta=setTimeout(actualizar,ms)}
document.addEventListener("visibilitychange",()=>{
  if(document.hidden){clearTimeout(proximaConsulta);return}
  programarConsulta(0);
});
actualizar();
</script></body></html>
)HTML";
// Escapa caracteres para crear respuestas JSON validas.
String seguro(String s) {
  s.replace("\\","\\\\");s.replace("\"","\\\"");
  s.replace("\n"," ");s.replace("\r"," ");
  return s;
}
// Entrega el estado del sistema en JSON.
void estadoWeb() {
  String j; j.reserve(640);
  j="{\"ip\":\""+WiFi.localIP().toString()+"\"";
  j+=",\"red\":\""+seguro(String(WIFI))+"\"";
  j+=",\"microfono\":"+String(microON?"true":"false");
  j+=",\"inicioListo\":"+String(inicioListo?"true":"false");
  j+=",\"tono\":"+String(tono?"true":"false");
  j+=",\"botonActivo\":"+String(botonON?"true":"false");
  j+=",\"transmitiendo\":"+String(txON?"true":"false");
  j+=",\"txPausado\":"+String(txPausado?"true":"false");
  j+=",\"nivel\":"+String(energia,2)+",\"umbral\":"+String(umbral,2);
  j+=",\"duracion\":"+String(duracion);
  j+=",\"textoBoton\":\""+seguro(textoB)+"\",\"textoSonido\":\""+seguro(textoM)+"\"";
  j+=",\"morseBoton\":\""+secB+"\",\"morseSonido\":\""+secM+"\"";
  j+=",\"ultimoBoton\":\""+ultimoB+"\",\"ultimoSonido\":\""+ultimoM+"\"}";
  server.sendHeader("Cache-Control","no-store");
  server.send(200,"application/json",j);
}
// Ejecuta los pitidos iniciales y habilita el microfono.
void procesarArranque() {
  unsigned long ahora = millis();
  switch (faseArranque) {
    case 0:
      if (ahora - momentoArranque >= 1500) {
        tone(BUZZER, FREQ_BUZZER);
        digitalWrite(LED, LOW); ultimoBuzzer = ahora; faseArranque = 1; momentoArranque = ahora;
      }
      break;
    case 1:
      if (ahora - momentoArranque >= 110) {
        noTone(BUZZER);
        digitalWrite(LED, HIGH); ultimoBuzzer = ahora; faseArranque = 2; momentoArranque = ahora;
      }
      break;
    case 2:
      if (ahora - momentoArranque >= 160) {
        tone(BUZZER, FREQ_BUZZER);
        digitalWrite(LED, LOW); ultimoBuzzer = ahora; faseArranque = 3; momentoArranque = ahora;
      }
      break;
    case 3:
      if (ahora - momentoArranque >= 110) {
        noTone(BUZZER);
        digitalWrite(LED, HIGH); ultimoBuzzer = ahora; faseArranque = 4; momentoArranque = ahora;
      }
      break;
    case 4:
      if (ahora - momentoArranque >= 350) {
        reiniciarMicro(); microON = true; ultimoTono = ahora; ultimaMuestra = ahora; espacioM = true;
        botonAnterior = digitalRead(BOTON) == LOW; cambioBoton = ahora; inicioListo = true;
        Serial.println("[INICIO] Dos pitidos completados. Microfono ACTIVADO.");
      }
      break;
  }
}
// Gestiona la reconexion automatica Wi-Fi.
void mantenerWiFi(unsigned long ahora) {
  bool conectado = WiFi.status() == WL_CONNECTED;
  if (conectado && !wifiEstabaConectado) {
    wifiEstabaConectado = true;
    Serial.println("[WIFI] Conectado a " + String(WIFI));
    Serial.println("[WIFI] IP: " + WiFi.localIP().toString());
    if (!lcdTXVisible) lcdAvisoCentrado("IP MORSELAB", WiFi.localIP().toString(), 4500);
  } else if (!conectado && wifiEstabaConectado) {
    wifiEstabaConectado = false;
    Serial.println("[WIFI] Conexion perdida; se reintentara automaticamente.");
    if (!lcdHayEntrada && !lcdTXVisible) lcdAvisoCentrado("WIFI DESCONECT.", "RECONECTANDO", 1800);
  }
  if (!conectado && ahora - ultimoIntentoWiFi >= WIFI_REINTENTO_MS) {
    ultimoIntentoWiFi = ahora;
    WiFi.reconnect();
    Serial.println("[WIFI] Reintentando...");
  }
}
// Registra las rutas HTTP y arranca el servidor.
void iniciarWeb() {
  server.on("/",HTTP_GET,[](){server.send_P(200,"text/html; charset=utf-8",WEB);});
  server.on("/estado",HTTP_GET,estadoWeb);
  server.on("/micro",HTTP_GET,[](){
    if (!inicioListo) {
      server.send(409,"text/plain","Esperando inicio del sistema");
      return;
    }
    if (txON || botonON || avisoActivo) {
      server.send(409,"text/plain","Sistema ocupado");
      return;
    }
    bool nuevoEstado = server.arg("on")=="1";
    if (microON != nuevoEstado) {
      microON = nuevoEstado;
      reiniciarMicro();
      ultimoTono = millis(); ultimaMuestra = ultimoTono; espacioM = true;
      iniciarAvisoMicro();
      lcdAvisoMicro();
    }
    Serial.printf("[WEB] Microfono %s desde la web | A0=GPIO analogico\n",
                  microON ? "ACTIVADO" : "DESACTIVADO");
    server.send(200,"text/plain",microON?"on":"off");
  });
  server.on("/enviar",HTTP_POST,[](){
    if (!inicioListo) {
      server.send(409,"text/plain","Esperando inicio del sistema");
      return;
    }
    if(txON||botonON||avisoActivo||digitalRead(BOTON)==LOW||bloqueoBotonTX){server.send(409,"text/plain","Boton o transmisor ocupado");return;}
    String s=server.arg("texto");s.trim();
    if(!s.length()||s.length()>60){server.send(400,"text/plain","Mensaje invalido");return;}
    iniciarTX(s);
    server.send(txON?200:400,"text/plain",txON?"OK":"Sin caracteres compatibles");
  });
  // Control remoto de pausa y reanudacion de la transmision.
  server.on("/tx/pausar",HTTP_POST,[](){
    if (!txON || txPausado) {server.send(409,"text/plain","No hay transmision activa");return;}
    pausarTX(millis());
    server.send(200,"text/plain","PAUSADO");
  });
  server.on("/tx/reanudar",HTTP_POST,[](){
    if (!txON || !txPausado) {server.send(409,"text/plain","No hay transmision pausada");return;}
    reanudarTX(millis());
    server.send(200,"text/plain","REANUDADO");
  });
  server.on("/limpiar",HTTP_POST,[](){
    String tipo=server.arg("tipo");
    if(tipo=="micro"){
      textoM=secM=ultimoM="";
      lcdMorseM=""; lcdTextoM=""; if (lcdOrigen == 'M') lcdHayEntrada = false;
      pulsosDescartados=0; espacioM=true;
      server.send(200,"text/plain","OK");
    }else if(tipo=="boton"){
      textoB=secB=ultimoB="";
      lcdMorseB=""; lcdTextoB=""; if (lcdOrigen == 'B') lcdHayEntrada = false;
      espacioB=true;
      server.send(200,"text/plain","OK");
    }else{
      server.send(400,"text/plain","Tipo invalido");
    }
  });
  server.onNotFound([](){server.send(404,"text/plain","No encontrado");});
  server.begin();
}
// Configura LCD, pines, Wi-Fi y servidor al encender.
void setup() {
  Serial.begin(115200);
  delay(300);
  Wire.begin(LCD_SDA, LCD_SCL);
  Wire.setClock(100000);
  lcd.init();
  lcd.backlight();
  lcdDisponible = true;
  lcdAvisoCentrado("MORSELAB", "INICIANDO...", 1200);
  pinMode(BOTON,INPUT_PULLUP);
  pinMode(LED,OUTPUT);
  pinMode(BUZZER,OUTPUT);
  digitalWrite(LED,HIGH);
  noTone(BUZZER);
  prepararGoertzel();
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.setOutputPower(17.0f);
  lcdAvisoCentrado("WIFI NOTEBOOK", "CONECTANDO...", 1200);
  WiFi.begin(WIFI, CLAVE);
  ultimoIntentoWiFi = millis();
  unsigned long inicioConexion = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - inicioConexion < WIFI_ESPERA_INICIAL_MS) {
    delay(50);
  }
  bool ok = WiFi.status() == WL_CONNECTED;
  wifiEstabaConectado = ok;
  Serial.println("\n=== MORSELAB ESP8266 (MODO CLIENTE) ===");
  Serial.println("SSID: " + String(WIFI));
  Serial.println(ok ? "WiFi conectado" : "WiFi sin conexion (reintentos automaticos)");
  if (ok) Serial.println("IP: " + WiFi.localIP().toString());
  lcdAvisoCentrado(ok ? "WIFI: OK" : "WIFI: SIN RED", "WEB INICIANDO", 1200);
  iniciarWeb(); inicioListo = false;
  if (ok) {
    lcdAvisoCentrado("IP MORSELAB", WiFi.localIP().toString(), 4500);
  } else {
    lcdAvisoCentrado("SIN CONEXION", "MORSE LOCAL OK", 2200);
  }
  momentoArranque = millis();
  Serial.println("[INICIO] Web configurada. Preparando dos pitidos y microfono...");
  ultimoBoton=ultimoTono=cambioBoton=millis(); ultimoBuzzer = millis() - SILENCIO_BUZZER;
  Serial.println("Servidor listo.");
}
// Atiende continuamente web, audio, boton, transmision y LCD.
void loop() {
  mantenerWiFi(millis());
  server.handleClient();
  if (!inicioListo) {
    procesarArranque();
    delay(2); return;
  }
  unsigned long ahora=millis();
  leerBoton(ahora);
  procesarAvisoMicro(millis());
  procesarTX(ahora);
  leerMicro();
  server.handleClient();
  ahora=millis();
  if(!botonON && !txON && secB.length() && ahora-ultimoBoton>=LETRA_BOTON)
    finalizar(secB,textoB,ultimoB);
  if(microON && !tono && !txON && !botonON && !bloqueoBotonTX &&
     ahora-ultimoBuzzer>=SILENCIO_BUZZER &&
     secM.length() && ahora-ultimoTono>=LETRA_MIC)
    finalizar(secM,textoM,ultimoM);
  if(!botonON && !txON)
    pausa(secB,textoB,espacioB,ultimoBoton,PALABRA_BOTON,ahora);
  if(microON && !tono && !txON && !botonON && !bloqueoBotonTX &&
     ahora-ultimoBuzzer>=SILENCIO_BUZZER)
    pausa(secM,textoM,espacioM,ultimoTono,PALABRA_MIC,ahora);
  lcdActualizar(ahora);
  static unsigned long diagnosticoMIC = 0;
  if (DIAGNOSTICO_DETALLADO && ahora - diagnosticoMIC >= 1500) {
    diagnosticoMIC = ahora;
    int adcInstantaneo = analogRead(MIC); bool pausaBuzzer = ahora - ultimoBuzzer < SILENCIO_BUZZER;
    const char* motivo = !microON ? "APAGADO" :
                         txON ? "TX_WEB" :
                         botonON ? "BOTON_PRESIONADO" :
                         bloqueoBotonTX ? "BLOQUEO_BOTON" :
                         pausaBuzzer ? "ESPERANDO_BUZZER" : "LEYENDO_A0";
    Serial.printf("[DIAG MIC] estado=%s | A0=%d | media=%d | variacion=%d | Goertzel=%.2f | boton=%s | tx=%s\n",
                  motivo, adcInstantaneo, adcMedio, amplitud, energia,
                  botonON ? "PRESIONADO" : "LIBRE", txON ? "SI" : "NO");
  }
  static unsigned long revision=0;
  if(ahora-revision>=15000){
    revision=ahora;
    Serial.print("Estado WiFi: ");
    Serial.print(WiFi.status() == WL_CONNECTED ? "CONECTADO" : "DESCONECTADO");
    Serial.print(" | IP: ");
    Serial.println(WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("sin IP"));
  }
  delay(2);
}
