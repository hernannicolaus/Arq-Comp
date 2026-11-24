// BIBLIOTECAS: conexion Wi-Fi, servidor web y calculos matematicos.
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <math.h>
// PINES DEL NODEMCU: D6=GPIO12 (boton), D4=GPIO2 (LED), D5=GPIO14 (buzzer), A0 (microfono).
#define BOTON 12
#define LED 2
#define BUZZER 14
#define MIC A0
// RED WI-FI: nombre y clave del punto de acceso que crea el ESP8266.
const char* WIFI = "MorseLab-WIFI";
const char* CLAVE = "morsemorse";
// AUDIO: frecuencias del microfono y buzzer; cantidad y velocidad de muestras.
const int FREQ_MIC = 1500, FREQ_BUZZER = 2500, N = 80; const float FS = 4000.0f;
// TIEMPOS MORSE (ms): duracion de punto/raya y pausas entre letras y palabras.
const unsigned long UNIDAD = 150; const unsigned long RAYA_BOTON = 300; const unsigned long RAYA_MIC = 300;
const unsigned long LETRA_BOTON = 550; const unsigned long PALABRA_BOTON = 1400;
const unsigned long LETRA_MIC = 460; const unsigned long PALABRA_MIC = 2100;
// DETECCION DE AUDIO: umbrales, filtros y tiempos para evitar falsas lecturas.
const unsigned long SILENCIO_BUZZER = 300;
const bool DIAGNOSTICO_DETALLADO = false; // Cambiar a true solo para diagnosticar A0.
ESP8266WebServer server(80); float energia = 0, umbral = 8.0f; const float UMBRAL_APAGADO = 3.5f;
const unsigned long CONFIRMAR_INICIO_MS = 25; const unsigned long CONFIRMAR_SILENCIO_MS = 45;
const unsigned long PULSO_MIN_MS = 70; const unsigned long PULSO_MAX_MS = 650; float picoPulso = 0;
unsigned long inicioCandidato = 0, silencioCandidato = 0; unsigned long ultimoCambioMuestra = 0;
unsigned int pulsosDescartados = 0; int adcMin = 1023, adcMax = 0, adcMedio = 0, amplitud = 0;
// ESTADOS: indican si estan activos el microfono, el boton, el buzzer y la transmision.
bool microON = false, tono = false, botonON = false;
bool botonAnterior = false, txON = false, txSonando = false; bool espacioB = true, espacioM = true;
// MARCAS DE TIEMPO: registran inicio, final y duracion de pulsos y transmisiones.
int positivos = 0, negativos = 0; unsigned long inicioTono = 0, ultimoTono = 0, inicioBoton = 0;
unsigned long ultimoBoton = 0, cambioBoton = 0, duracion = 0; unsigned long siguienteTX = 0, ultimaMuestra = 0;
unsigned long duracionMicro = 0; unsigned long ultimoBuzzer = 0; bool bloqueoBotonTX = false;
// CONTROLES ESPECIALES: pulsacion de 5 s y avisos sonoros de activacion.
const unsigned long PULSACION_LARGA_MS = 5000; const unsigned long BUZZER_MANUAL_MAX_MS = 600;
bool pulsacionLargaAplicada = false; bool buzzerManualSilenciado = false; bool avisoActivo = false;
uint8_t faseAviso = 0; unsigned long momentoAviso = 0; bool inicioListo = false; uint8_t faseArranque = 0;
unsigned long momentoArranque = 0;
// MENSAJES: secB/secM guardan puntos y rayas; textoB/textoM, letras; colaTX, Morse a transmitir.
String secB="", secM="", textoB="", textoM="", ultimoB="", ultimoM="";
String colaTX="";
// DICCIONARIO MORSE: cada posicion de letras[] coincide con la de codigos[].
const char letras[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
const char* codigos[] = {
  ".-","-...","-.-.","-..",".","..-.","--.","....","..",".---",
  "-.-",".-..","--","-.","---",".--.","--.-",".-.","...","-",
  "..-","...-",".--","-..-","-.--","--..",
  "-----",".----","..---","...--","....-",".....",
  "-....","--...","---..","----."
};
// CODIFICAR: busca una letra o numero y devuelve sus puntos y rayas.
String codificar(char c) {
  c = toupper((unsigned char)c);
  for (int i = 0; i < 36; i++)
    if (letras[i] == c) return codigos[i];
  return "";
}
// DECODIFICAR: busca una secuencia de puntos y rayas y devuelve el caracter.
char decodificar(String s) {
  for (int i = 0; i < 36; i++)
    if (s == codigos[i]) return letras[i];
  return '?';
}
// CONVERTIR: recorre un texto y construye el Morse completo separando letras y palabras.
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
// AGREGAR: suma un punto o una raya a la secuencia recibida.
void agregar(String &sec, bool punto) {
  if (sec.length() >= 12) sec = "";
  sec += punto ? "." : "-";
  Serial.println(punto ? "[RX] PUNTO" : "[RX] RAYA");
}
// FINALIZAR: convierte la secuencia completa en una letra y la agrega al mensaje.
void finalizar(String &sec, String &texto, String &ultimo) {
  if (!sec.length()) return;
  ultimo = sec;
  char c = decodificar(sec); texto += c;
  if (texto.length() > 100)
    texto.remove(0, texto.length() - 100);
  Serial.println("[RX] " + sec + " -> " + String(c));
  sec = "";
}
// PAUSA: agrega un espacio cuando paso suficiente tiempo sin recibir señales.
void pausa(String &sec, String &texto, bool &espacio,
           unsigned long ultimo, unsigned long limite,
           unsigned long ahora) {
  if (!sec.length() && !espacio && texto.length() &&
      ahora - ultimo >= limite) {
    texto += " ";
    if (texto.length() > 100) texto.remove(0, texto.length() - 100);
    espacio = true;
  }
}
// GOERTZEL: prepara los datos y detecta la energia del tono de 1500 Hz en A0.
float ventanaGoertzel[N]; float coefGoertzel = 0;
// PREPARAR GOERTZEL: calcula los coeficientes que usara el detector de sonido.
void prepararGoertzel() {
  const float omega = 2.0f * PI * FREQ_MIC / FS;
  coefGoertzel = 2.0f * cosf(omega);
  for (int i = 0; i < N; i++) {
    ventanaGoertzel[i] = 0.5f - 0.5f * cosf(2.0f * PI * i / (N - 1));
  }
}
// GOERTZEL: toma muestras del microfono y calcula la energia del tono buscado.
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
// REINICIAR MICRO: limpia los estados y datos de la deteccion anterior.
void reiniciarMicro() {
  tono = false; positivos = negativos = 0; energia = 0; adcMin = 1023; adcMax = 0; adcMedio = 0; amplitud = 0;
  duracionMicro = 0; inicioCandidato = silencioCandidato = 0; picoPulso = 0;
  secM = "";
  digitalWrite(LED, HIGH);
}
// LEER MICRO: detecta sonidos validos y decide si son puntos o rayas.
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
// INICIAR AVISO MICRO: comienza los dos pitidos de confirmacion.
void iniciarAvisoMicro() {
  if (avisoActivo || txON || !inicioListo) return;
  avisoActivo = true; faseAviso = 0;
  momentoAviso = millis();
  tone(BUZZER, FREQ_BUZZER);
  digitalWrite(LED, LOW); ultimoBuzzer = momentoAviso;
}
// PROCESAR AVISO MICRO: controla los pitidos y el LED usando tiempos.
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
// LEER BOTON: distingue puntos/rayas y activa o desactiva el micro con 5 s.
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
    iniciarAvisoMicro();
    Serial.printf("[BOTON] Pulsacion larga 5 s: microfono %s\n",
                  microON ? "ACTIVADO" : "DESACTIVADO");
  }
}
// INICIAR TX: convierte el texto enviado por la web y prepara su transmision.
void iniciarTX(String texto) {
  texto.trim();
  if (texto.length() > 60) texto = texto.substring(0, 60);
  colaTX = convertir(texto); txON = colaTX.length() > 0; txSonando = false;
  siguienteTX = millis();
  reiniciarMicro();
  ultimoBuzzer = millis();
}
// PROCESAR TX: emite el Morse con el buzzer, respetando los tiempos.
void procesarTX(unsigned long ahora) {
  if (!txON || (long)(ahora - siguienteTX) < 0) return;
  if (txSonando) {
    noTone(BUZZER); ultimoBuzzer = ahora;
    digitalWrite(LED, HIGH); txSonando = false; siguienteTX = ahora + UNIDAD; return;
  }
  if (!colaTX.length()) {
    txON = false;
    noTone(BUZZER); ultimoBuzzer = ahora; ultimoTono = ahora; return;
  }
  char c = colaTX[0];
  colaTX.remove(0, 1);
  if (c == '.' || c == '-') {
    unsigned long tiempo = c == '.' ? UNIDAD : 3 * UNIDAD;
    tone(BUZZER, FREQ_BUZZER); ultimoBuzzer = ahora;
    digitalWrite(LED, LOW); txSonando = true; siguienteTX = ahora + tiempo;
  } else {
    siguienteTX = ahora + (c == '/' ? 6 : 2) * UNIDAD;
  }
}
// PAGINA WEB: HTML, CSS y JavaScript guardados en memoria de programa (PROGMEM).
const char WEB[] PROGMEM = R"HTML(
<!doctype html><html lang="es"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>MorseLab ESP8266</title>
<style>
/* CSS: estilos visuales de la cabecera, tarjetas, botones y paneles. */
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
/* DISENO RESPONSIVE: adapta las columnas al ancho del celular o pantalla. */
@media(max-width:900px){.grid{grid-template-columns:repeat(2,minmax(0,1fr))}.flow-grid{grid-template-columns:1fr}}
@media(max-width:620px){.grid{grid-template-columns:1fr}main{margin:10px auto}.card{padding:14px}}
</style></head><body>
<!-- CABECERA: nombre del proyecto, conexion y panel de informacion. -->
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
<div class="pop-row"><span>Clientes Wi-Fi</span><b id="clientes">0</b></div>
<div class="pop-row"><span>Transmision</span><b id="txEstado">Listo</b></div>
<div class="pop-row"><span>Umbral</span><b id="umbral">8</b></div>
</div></div></nav></header>
<!-- TARJETAS PRINCIPALES: boton, microfono y transmisor Morse. -->
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
<div class="actions"><button id="enviarBtn" onclick="enviar()" disabled>Transmitir</button><button class="secondary" onclick="limpiarEntrada()">Limpiar</button></div>
<p id="estadoEnvio">Transmisor listo.</p>
</section>
</div>
<!-- FLUJO EN VIVO: muestra de forma grafica que parte del sistema esta activa. -->
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
<script>
// JAVASCRIPT: controla la interaccion y actualiza los datos sin recargar la pagina.
const $=id=>document.getElementById(id),abc="ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
const dic=[".-","-...","-.-.","-..",".","..-.","--.","....","..",".---","-.-",".-..","--","-.","---",".--.","--.-",".-.","...","-","..-","...-",".--","-..-","-.--","--..","-----",".----","..---","...--","....-",".....","-....","--...","---..","----."];
let micro=true,ocupado=false,actualizando=false,listo=false;
function toggleInfo(){let p=$("infoPanel");p.hidden=!p.hidden;$("infoBtn").setAttribute("aria-expanded",String(!p.hidden))}
document.addEventListener("click",e=>{if(!e.target.closest(".right")&&!$("infoPanel").hidden)toggleInfo()});
document.addEventListener("keydown",e=>{if(e.key==="Escape"&&!$("infoPanel").hidden)toggleInfo()});
// VISTA PREVIA: convierte a Morse el texto escrito, solo para mostrarlo en pantalla.
function convertir(t){return t.toUpperCase().trim().split(/\s+/).map(p=>[...p].map(c=>dic[abc.indexOf(c)]||"").filter(Boolean).join(" ")).filter(Boolean).join(" / ")}
function preview(){$("prev").textContent=convertir($("mensaje").value)||"—"}
function limpiarEntrada(){$("mensaje").value="";preview()}
// ACCIONES WEB: enviar texto, encender/apagar microfono y limpiar mensajes.
async function enviar(){let t=$("mensaje").value.trim();if(!t||ocupado||!listo)return;ocupado=true;
try{let r=await fetch("/enviar",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body:"texto="+encodeURIComponent(t)});$("estadoEnvio").textContent=r.ok?"Mensaje enviado.":await r.text()}
catch(e){$("estadoEnvio").textContent="Error de conexion"}finally{ocupado=false}}
async function toggleMicro(){if(!listo)return;try{let r=await fetch("/micro?on="+(micro?0:1));if(r.ok)actualizar()}catch(e){}}
async function limpiar(tipo){try{let r=await fetch("/limpiar?tipo="+tipo,{method:"POST"});if(!r.ok)throw Error();actualizar()}catch(e){alert("No se pudo limpiar esta seccion")}}
// ESTADO EN VIVO: consulta /estado y actualiza mensajes, indicadores y botones.
async function actualizar(){if(actualizando||document.hidden)return;actualizando=true;try{
const ctrl=new AbortController(), limite=setTimeout(()=>ctrl.abort(),1800);
let r;
try{r=await fetch("/estado",{cache:"no-store",signal:ctrl.signal});if(!r.ok)throw Error();}
finally{clearTimeout(limite)}
let d=await r.json();
ultimoEstadoActivo=!!(d.tono||d.botonActivo||d.transmitiendo||d.morseSonido||d.morseBoton);
clientesConectados=Number(d.clientes)||1;
micro=d.microfono;listo=d.inicioListo;
$("conexion").textContent="Conectado";$("ip").textContent=d.ip;
$("microStat").textContent=!listo?"Iniciando":micro?"Activo":"Apagado";
$("txEstado").textContent=d.transmitiendo?"Transmitiendo":"Listo";
$("enviarBtn").disabled=!listo||d.transmitiendo||d.botonActivo;
for(let k of ["textoSonido","textoBoton","morseSonido","morseBoton","ultimoSonido","ultimoBoton"])$(k).textContent=d[k]||"—";
$("umbral").textContent=d.umbral;$("clientes").textContent=d.clientes;
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
  if(!document.hidden)programarConsulta(ultimoEstadoActivo?300:clientesConectados>=2?700:450);
}}
// ACTUALIZACIONES: ajusta la frecuencia de consulta segun la actividad y clientes.
let proximaConsulta=null, ultimoEstadoActivo=false, clientesConectados=1;
function programarConsulta(ms){clearTimeout(proximaConsulta);proximaConsulta=setTimeout(actualizar,ms)}
document.addEventListener("visibilitychange",()=>{
  if(document.hidden){clearTimeout(proximaConsulta);return}
  programarConsulta(0);
});
actualizar();
</script></body></html>
)HTML";
// SEGURO: prepara cadenas para enviarlas sin romper el formato JSON.
String seguro(String s) {
  s.replace("\\","\\\\");s.replace("\"","\\\"");
  s.replace("\n"," ");s.replace("\r"," ");
  return s;
}
// ESTADO WEB: responde con un JSON que contiene el estado actual del sistema.
void estadoWeb() {
  String j; j.reserve(640);
  j="{\"ip\":\""+WiFi.softAPIP().toString()+"\"";
  j+=",\"microfono\":"+String(microON?"true":"false");
  j+=",\"inicioListo\":"+String(inicioListo?"true":"false");
  j+=",\"tono\":"+String(tono?"true":"false");
  j+=",\"botonActivo\":"+String(botonON?"true":"false");
  j+=",\"transmitiendo\":"+String(txON?"true":"false");
  j+=",\"clientes\":"+String(WiFi.softAPgetStationNum());
  j+=",\"nivel\":"+String(energia,2)+",\"umbral\":"+String(umbral,2);
  j+=",\"duracion\":"+String(duracion);
  j+=",\"textoBoton\":\""+seguro(textoB)+"\",\"textoSonido\":\""+seguro(textoM)+"\"";
  j+=",\"morseBoton\":\""+secB+"\",\"morseSonido\":\""+secM+"\"";
  j+=",\"ultimoBoton\":\""+ultimoB+"\",\"ultimoSonido\":\""+ultimoM+"\"}";
  server.sendHeader("Cache-Control","no-store");
  server.send(200,"application/json",j);
}
// PROCESAR ARRANQUE: realiza los pitidos iniciales y luego activa el microfono.
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
// INICIAR WEB: registra las rutas /, /estado, /micro, /enviar y /limpiar.
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
  server.on("/limpiar",HTTP_POST,[](){
    String tipo=server.arg("tipo");
    if(tipo=="micro"){
      textoM=secM=ultimoM="";
      pulsosDescartados=0; espacioM=true;
      server.send(200,"text/plain","OK");
    }else if(tipo=="boton"){
      textoB=secB=ultimoB="";
      espacioB=true;
      server.send(200,"text/plain","OK");
    }else{
      server.send(400,"text/plain","Tipo invalido");
    }
  });
  server.onNotFound([](){server.send(404,"text/plain","No encontrado");});
  server.begin();
}
// SETUP: se ejecuta una vez; configura pines, Wi-Fi y servidor web.
void setup() {
  Serial.begin(115200);
  delay(300);
  pinMode(BOTON,INPUT_PULLUP);
  pinMode(LED,OUTPUT);
  pinMode(BUZZER,OUTPUT);
  digitalWrite(LED,HIGH);
  noTone(BUZZER);
  prepararGoertzel();
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  WiFi.setOutputPower(20.5f);
  bool ok = WiFi.softAP(WIFI, CLAVE, 1, false, 4);
  Serial.println("\n=== MORSELAB ESP8266 ===");
  Serial.println(ok?"WiFi iniciado":"ERROR WiFi");
  Serial.println("SSID: "+String(WIFI));
  Serial.println("IP: "+WiFi.softAPIP().toString());
  iniciarWeb(); inicioListo = false;
  if (!ok) faseArranque = 255;
  momentoArranque = millis();
  if (ok) Serial.println("[INICIO] WiFi y web iniciados. Preparando dos pitidos y microfono...");
  else Serial.println("[INICIO] WiFi fallo: microfono desactivado; revisar AP.");
  ultimoBoton=ultimoTono=cambioBoton=millis(); ultimoBuzzer = millis() - SILENCIO_BUZZER;
  Serial.println("Servidor listo.");
}
// LOOP: se repite continuamente; atiende web, boton, microfono y transmisor.
void loop() {
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
    Serial.print("Clientes WiFi: ");
    Serial.println(WiFi.softAPgetStationNum());
  }
  delay(2);
}
