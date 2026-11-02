#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

// ================= PINES (SIN CONFLICTO) =================
const int PIN_MIC      = 13;  // Entrada para Goertzel
const int PIN_BOTON    = 12;  // D6 / GPIO12
const int PIN_LED      = 2;   // D4 / GPIO2
const int PIN_BUZZER   = 14;  // D5 / GPIO14

// ================= WIFI =================
const char* WIFI_NOMBRE = "MorseESP8266";
const char* WIFI_CLAVE  = "Morse123";

ESP8266WebServer server(80);

// ============= PARÁMETROS DE GOERTZEL (DSP) ==============
const float FRECUENCIA_TONO     = 2500.0;  // Frecuencia del tono Morse
const float FRECUENCIA_MUESTREO = 4000.0; // 4000 Hz (250 us por muestra)
const int N_MUESTRAS            = 64;     // Ventana de ~16 ms
const unsigned long PERIODO_US  = 1000000 / FRECUENCIA_MUESTREO;

float coeffGoertzel = 0.0;
int muestras[N_MUESTRAS];
float energiaActual = 0.0;

const float UMBRAL_ENERGIA_GOERTZEL = 75000.0;

// ================= UMBRALES MORSE =================
const unsigned long UMBRAL_PUNTO_RAYA = 200;
const unsigned long UMBRAL_LETRA      = 800;
const unsigned long UMBRAL_PALABRA    = 2000;
const unsigned long FILTRO_MINIMO_MS  = 35; // Ignora ruidos breves (< 35 ms)

// ================= ESTADO BOTÓN =================
int estadoAnteriorBoton = HIGH;
unsigned long tiempoInicioBoton = 0;
unsigned long tiempoUltimoEventoBoton = 0;
bool botonPresionado = false;
bool espacioBotonAgregado = true;

// ================= ESTADO SENSOR (GOERTZEL) =================
unsigned long tiempoInicioSonido = 0;
unsigned long tiempoUltimoEventoSonido = 0;
unsigned long ultimaDuracionSonido = 0;
bool sensorActivoAnterior = false;
bool sonidoDetectado = false;
int contadorEstabilidad = 0;       // Filtro suave de ventanas Goertzel
bool espacioSonidoAgregado = true;

// ================= MICRÓFONO =================
bool microfonoHabilitado = true;

// ================= SECUENCIAS =================
String secuenciaBoton    = "";
String secuenciaSonido   = "";
String textoBoton        = "";
String textoSonido       = "";
String ultimoMorseBoton  = "";
String ultimoMorseSonido = "";

// ================= REPETICIÓN POR BUZZER =================
String colaReproduccion = "";
bool reproduciendo = false;
unsigned long tiempoSiguienteSimbolo = 0;

// ================= DICCIONARIO MORSE =================
char decodificarMorse(String morse) {
  if (morse == ".-")   return 'A';
  if (morse == "-...") return 'B';
  if (morse == "-.-.") return 'C';
  if (morse == "-..")  return 'D';
  if (morse == ".")    return 'E';
  if (morse == "..-.") return 'F';
  if (morse == "--.")  return 'G';
  if (morse == "....") return 'H';
  if (morse == "..")   return 'I';
  if (morse == ".---") return 'J';
  if (morse == "-.-")  return 'K';
  if (morse == ".-..") return 'L';
  if (morse == "--")   return 'M';
  if (morse == "-.")   return 'N';
  if (morse == "---")  return 'O';
  if (morse == ".--.") return 'P';
  if (morse == "--.-") return 'Q';
  if (morse == ".-.")  return 'R';
  if (morse == "...")  return 'S';
  if (morse == "-")    return 'T';
  if (morse == "..-")  return 'U';
  if (morse == "...-") return 'V';
  if (morse == ".--")  return 'W';
  if (morse == "-..-") return 'X';
  if (morse == "-.--") return 'Y';
  if (morse == "--..") return 'Z';
  return '?';
}

void agregarSimbolo(String &secuencia, bool esPunto) {
  if (esPunto) secuencia += ".";
  else         secuencia += "-";
}

// ============= FUNCIÓN ALGORITMO DE GOERTZEL =============
float calcularGoertzel() {
  long sumaDC = 0;

  // capturar muestras analógicas de A0 a 4000 Hz
  for (int i = 0; i < N_MUESTRAS; i++) {
    unsigned long t0 = micros();
    muestras[i] = analogRead(PIN_MIC);
    sumaDC += muestras[i];
    while (micros() - t0 < PERIODO_US) {
      // Espera
    }
  }

  // Calcular y restar la componente continua (Offset DC)
  float offsetDC = (float)sumaDC / N_MUESTRAS;

  // Recurrencia del filtro de Goertzel
  float s_prev = 0.0;
  float s_prev2 = 0.0;

  for (int i = 0; i < N_MUESTRAS; i++) {
    float x = muestras[i] - offsetDC;
    float s = x + coeffGoertzel * s_prev - s_prev2;
    s_prev2 = s_prev;
    s_prev = s;
  }

  // Retornar energía (magnitud al cuadrado) 
  return (s_prev * s_prev) + (s_prev2 * s_prev2) - (coeffGoertzel * s_prev * s_prev2);
}

// ================= REPRODUCIR EN BUZZER =================
void encolarReproduccion(const String &codigo) {
  for (unsigned int i = 0; i < codigo.length(); i++) {
    colaReproduccion += codigo[i];
  }
  colaReproduccion += " ";
}

void procesarReproduccion(unsigned long ahora) {
  if (reproduciendo) {
    if ((long)(ahora - tiempoSiguienteSimbolo) >= 0) {
      noTone(PIN_BUZZER);
      reproduciendo = false;
      tiempoSiguienteSimbolo = ahora + 100;
    }
    return;
  }
  if ((long)(ahora - tiempoSiguienteSimbolo) < 0) return;
  if (colaReproduccion.length() == 0) return;

  char c = colaReproduccion[0];
  colaReproduccion.remove(0, 1);

  if (c == '.') {
    tone(PIN_BUZZER, (int)FRECUENCIA_TONO, 100);
    reproduciendo = true;
    tiempoSiguienteSimbolo = ahora + 100;
  } else if (c == '-') {
    tone(PIN_BUZZER, (int)FRECUENCIA_TONO, 300);
    reproduciendo = true;
    tiempoSiguienteSimbolo = ahora + 300;
  } else if (c == ' ') {
    tiempoSiguienteSimbolo = ahora + 400;
  }
}

// ================= CERRAR LETRA =================
void finalizarLetra(String &secuencia, String &texto, String &ultimoMorse,
                    const char* origen, bool reemitir) {
  if (secuencia.length() == 0) return;
  String codigo = secuencia;
  char letra = decodificarMorse(codigo);
  ultimoMorse = codigo;
  texto += letra;
  if (texto.length() > 80) texto.remove(0, texto.length() - 80);
  Serial.print("["); Serial.print(origen); Serial.print("] ");
  Serial.print(codigo); Serial.print(" -> "); Serial.println(letra);
  if (reemitir) encolarReproduccion(codigo);
  secuencia = "";
}

// ================= PÁGINA WEB =================
const char PAGINA[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Teclado Morse · Receptor</title>
<style>
  :root{--fondo:#1e1f22;--panel:#2b2d31;--panel-2:#313338;--borde:#3f4147;
    --texto:#dbdee1;--texto-suave:#949ba4;--acento:#57b3a0;
    --punto:#e0b44a;--raya:#e07a4a;--rojo:#e05a5a;}
  *{box-sizing:border-box;}
  body{margin:0;font-family:-apple-system,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;
    background:var(--fondo);color:var(--texto);font-size:15px;line-height:1.45;}
  header{background:var(--panel);border-bottom:1px solid var(--borde);padding:14px 20px;}
  header h1{margin:0;font-size:16px;font-weight:600;letter-spacing:.3px;}
  header h1 span{color:var(--texto-suave);font-weight:400;}
  main{max-width:820px;margin:0 auto;padding:20px;}
  .chat{display:flex;flex-direction:column;gap:18px;}
  .panel{background:var(--panel-2);border:1px solid var(--borde);border-radius:8px;overflow:hidden;}
  .panel-head{display:flex;align-items:center;justify-content:space-between;
    padding:10px 14px;border-bottom:1px solid var(--borde);background:var(--panel);}
  .panel-head h2{margin:0;font-size:13px;font-weight:600;text-transform:uppercase;
    letter-spacing:.6px;color:var(--texto-suave);}
  .estado{font-size:12px;color:var(--texto-suave);display:flex;align-items:center;gap:6px;}
  .led{width:8px;height:8px;border-radius:50%;background:#4a4d55;display:inline-block;}
  .led.on{background:var(--acento);box-shadow:0 0 6px var(--acento);}
  .led.apagado{background:var(--rojo);box-shadow:0 0 6px var(--rojo);}
  .historial{min-height:70px;max-height:150px;overflow-y:auto;padding:12px 14px;
    font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;font-size:18px;
    letter-spacing:2px;word-break:break-all;color:var(--texto);
    background:repeating-linear-gradient(to bottom,transparent,transparent 27px,
      rgba(255,255,255,.03) 28px);}
  .historial:empty::before{content:"—";color:var(--texto-suave);}
  .codigo{padding:10px 14px;border-top:1px solid var(--borde);
    font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;font-size:14px;
    color:var(--punto);display:flex;align-items:center;gap:8px;min-height:38px;}
  .codigo::before{content:"›";color:var(--texto-suave);font-size:16px;}
  .meta{display:flex;justify-content:space-between;padding:8px 14px;font-size:12px;
    color:var(--texto-suave);border-top:1px solid var(--borde);}
  .meta code{font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;color:var(--texto);}
  .control{padding:12px 14px;border-top:1px solid var(--borde);}
  .control button{appearance:none;width:100%;border:1px solid var(--borde);
    background:var(--panel);color:var(--texto);font-size:15px;padding:14px 10px;
    border-radius:6px;cursor:pointer;font-family:inherit;font-weight:600;
    transition:transform .05s, background .15s, border-color .15s;}
  .control button:active{transform:scale(.98);background:#3a3d44;}
  .control button.habilitado{border-color:var(--acento);color:var(--acento);}
  .control button.deshabilitado{border-color:var(--rojo);color:var(--rojo);}
  .aviso{padding:0 14px 12px;font-size:12px;color:var(--texto-suave);}
  footer{max-width:820px;margin:0 auto;padding:0 20px 30px;font-size:12px;
    color:var(--texto-suave);line-height:1.6;}
  footer b{color:var(--texto);font-weight:500;}
</style>
</head>
<body>
<header><h1>Teclado Morse <span>· Receptor ESP8266 (Goertzel 700 Hz)</span></h1></header>
<main class="chat">

  <section class="panel">
    <div class="panel-head">
      <h2>Sensor de sonido (Goertzel 700 Hz · Pin A0)</h2>
      <div class="estado"><span class="led" id="sonidoLed"></span><span id="sonidoEstado">Esperando</span></div>
    </div>
    <div class="historial" id="sonidoTexto">—</div>
    <div class="codigo" id="sonidoCodigo">—</div>
    <div class="meta"><span>último código</span><code id="sonidoUltimo">—</code></div>
    <div class="meta">
      <span>Energía Goertzel: <code id="sonidoEnergia">0</code></span>
      <span>Último pulso: <code id="sonidoDuracion">0 ms</code></span>
    </div>
    <div class="control">
      <button id="btnMicro" class="habilitado">🎤 Micrófono: ACTIVO</button>
    </div>
    <div class="aviso">
      Reproducí tonos de <b>700 Hz</b> desde el celular. El sistema ignora automáticamente
      el micrófono mientras presionás el botón manual o suena el buzzer.
    </div>
  </section>

  <section class="panel">
    <div class="panel-head">
      <h2>Botón manual (Emisor 700 Hz)</h2>
      <div class="estado"><span class="led" id="botonLed"></span><span id="botonEstado">Listo</span></div>
    </div>
    <div class="historial" id="botonTexto">—</div>
    <div class="codigo" id="botonCodigo">—</div>
    <div class="meta"><span>último código</span><code id="botonUltimo">—</code></div>
  </section>

</main>
<footer>
  <b>Punto</b> = pulso corto (&lt; 200 ms) · <b>Raya</b> = pulso largo (&ge; 200 ms).<br>
  La letra se decodifica a los 800 ms de silencio y el espacio entre palabras a los 2000 ms.
</footer>

<script>
const $ = id => document.getElementById(id);
let microLocal = true;

function actualizarBotonMicro(activo){
  microLocal = activo;
  const b = $('btnMicro');
  b.textContent = activo ? '🎤 Micrófono: ACTIVO' : '🔇 Micrófono: DESACTIVADO';
  b.className = activo ? 'habilitado' : 'deshabilitado';
}

$('btnMicro').onclick = async () => {
  const nuevo = !microLocal;
  try{
    const r = await fetch('/micro?on=' + (nuevo ? '1' : '0'), {cache:'no-store'});
    if(!r.ok) throw new Error('sin respuesta');
    actualizarBotonMicro(nuevo);
  }catch(e){ console.error('No se pudo cambiar el micrófono', e); }
};

async function actualizar(){
  try{
    const r = await fetch('/estado', {cache:'no-store'});
    if(!r.ok) throw new Error('Sin respuesta');
    const d = await r.json();

    actualizarBotonMicro(!!d.microfono);

    if (!d.microfono){
      $('sonidoEstado').textContent = 'Apagado';
      $('sonidoLed').className = 'led apagado';
    } else {
      $('sonidoEstado').textContent = d.sonidoActivo ? 'Tono 700Hz Detectado' : 'Silencio';
      $('sonidoLed').className = d.sonidoActivo ? 'led on' : 'led';
    }

    $('botonEstado').textContent = d.botonActivo ? 'Transmitiendo' : 'Listo';
    $('botonLed').classList.toggle('on', !!d.botonActivo);

    $('botonCodigo').textContent  = d.morseBoton  || '—';
    $('sonidoCodigo').textContent = d.morseSonido || '—';

    $('botonTexto').textContent  = d.textoBoton  || '—';
    $('sonidoTexto').textContent = d.textoSonido || '—';

    $('botonUltimo').textContent  = d.ultimoBoton  || '—';
    $('sonidoUltimo').textContent = d.ultimoSonido || '—';

    $('sonidoEnergia').textContent  = Math.round(d.energia || 0);
    $('sonidoDuracion').textContent = (d.duracion || 0) + ' ms';

    ['botonTexto','sonidoTexto'].forEach(id=>{
      const el = $(id);
      el.scrollTop = el.scrollHeight;
    });
  }catch(e){
    ['botonEstado','sonidoEstado'].forEach(id=>{
      $(id).textContent = 'Reconectando…';
    });
  }
}

actualizar();
setInterval(actualizar, 250);
</script>
</body>
</html>
)HTML";

// ================= JSON DE ESTADO =================
void enviarEstado() {
  String json = "{";
  json += "\"botonActivo\":";   json += (botonPresionado ? "true" : "false");
  json += ",\"sonidoActivo\":";  json += (sonidoDetectado ? "true" : "false");
  json += ",\"microfono\":";     json += (microfonoHabilitado ? "true" : "false");

  json += ",\"morseBoton\":\"";  json += secuenciaBoton;  json += "\"";
  json += ",\"morseSonido\":\""; json += secuenciaSonido; json += "\"";

  json += ",\"textoBoton\":\"";  json += textoBoton;  json += "\"";
  json += ",\"textoSonido\":\""; json += textoSonido; json += "\"";

  json += ",\"ultimoBoton\":\"";  json += ultimoMorseBoton;  json += "\"";
  json += ",\"ultimoSonido\":\""; json += ultimoMorseSonido; json += "\"";

  json += ",\"energia\":";  json += String(energiaActual, 0);
  json += ",\"duracion\":"; json += String(ultimaDuracionSonido);
  json += "}";

  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}

// ================= HANDLER MICRÓFONO =================
void handleMicro() {
  if (server.hasArg("on")) {
    String v = server.arg("on");
    bool nuevo = (v == "1" || v == "true");
    if (microfonoHabilitado != nuevo) {
      microfonoHabilitado = nuevo;
      secuenciaSonido = "";
      sensorActivoAnterior = false;
      sonidoDetectado = false;
      contadorEstabilidad = 0;
      Serial.print("[MIC] ");
      Serial.println(microfonoHabilitado ? "ACTIVO" : "DESACTIVADO");
    }
  }
  server.send(200, "text/plain", microfonoHabilitado ? "on" : "off");
}

// ================= CONFIGURACION DE WEB =================
void iniciarWeb() {
  server.on("/", HTTP_GET, []() {
    server.send_P(200, "text/html; charset=utf-8", PAGINA);
  });
  server.on("/estado", HTTP_GET, enviarEstado);
  server.on("/micro",  HTTP_GET, handleMicro);
  server.onNotFound([]() { server.send(404, "text/plain", "No encontrado"); });
  server.begin();
}

// ================= SETUP =================
void setup() {
  pinMode(PIN_BOTON, INPUT_PULLUP);
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);

  digitalWrite(PIN_LED, HIGH);
  digitalWrite(PIN_BUZZER, LOW);

  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("=== MORSE RECEPTOR ESP8266 (GOERTZEL 700 Hz) ===");

  // Precalcular coeficiente de Goertzel
  float k = round((N_MUESTRAS * FRECUENCIA_TONO) / FRECUENCIA_MUESTREO);
  float omega = (2.0 * PI * k) / N_MUESTRAS;
  coeffGoertzel = 2.0 * cos(omega);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(WIFI_NOMBRE, WIFI_CLAVE);

  Serial.print("Red Wi-Fi: "); Serial.println(WIFI_NOMBRE);
  Serial.print("Clave: ");     Serial.println(WIFI_CLAVE);
  Serial.print("Abre: http://"); Serial.println(WiFi.softAPIP());

  iniciarWeb();

  tiempoUltimoEventoBoton = millis();
  tiempoUltimoEventoSonido = millis();

  Serial.println("Servidor web iniciado.");
}

// ================= LOOP =================
void loop() {
  server.handleClient();
  unsigned long ahora = millis();

  // ---------- BOTÓN MANUAL (EMISIÓN A 700 Hz) ----------
  int estadoBoton = digitalRead(PIN_BOTON);
  botonPresionado = (estadoBoton == LOW);

  if (estadoBoton == LOW && estadoAnteriorBoton == HIGH) {
    tiempoInicioBoton = ahora;
    digitalWrite(PIN_LED, LOW);
    tone(PIN_BUZZER, (int)FRECUENCIA_TONO); // Emitir tono Morse de 700 Hz
  }
  if (estadoBoton == HIGH && estadoAnteriorBoton == LOW) {
    unsigned long duracion = ahora - tiempoInicioBoton;
    noTone(PIN_BUZZER);
    digitalWrite(PIN_LED, HIGH);
    if (duracion >= FILTRO_MINIMO_MS) {
      tiempoUltimoEventoBoton = ahora;
      espacioBotonAgregado = false;
      agregarSimbolo(secuenciaBoton, duracion < UMBRAL_PUNTO_RAYA);
    }
  }
  estadoAnteriorBoton = estadoBoton;

  // ---------- SENSOR DE SONIDO CON GOERTZEL ----------
  // Se ejecuta si el micrófono está activo y NO estamos emitiendo con el botón/buzzer
  if (microfonoHabilitado && !botonPresionado && !reproduciendo && colaReproduccion.length() == 0) {
    energiaActual = calcularGoertzel();
    bool superaUmbral = (energiaActual > UMBRAL_ENERGIA_GOERTZEL);

    // Pequeño filtro de estabilidad (2 ventanas seguidas) para evitar falsos cortes
    if (superaUmbral) {
      if (contadorEstabilidad < 2) contadorEstabilidad++;
    } else {
      if (contadorEstabilidad > 0) contadorEstabilidad--;
    }

    bool sensorActivo = (contadorEstabilidad >= 1);
    sonidoDetectado = sensorActivo;

    // Flanco de inicio de tono
    if (sensorActivo && !sensorActivoAnterior) {
      tiempoInicioSonido = ahora;
      digitalWrite(PIN_LED, LOW);
    }

    // Flanco de fin de tono
    if (!sensorActivo && sensorActivoAnterior) {
      unsigned long duracion = ahora - tiempoInicioSonido;
      digitalWrite(PIN_LED, HIGH);
      tiempoUltimoEventoSonido = ahora;
      if (duracion >= FILTRO_MINIMO_MS) {
        ultimaDuracionSonido = duracion;
        espacioSonidoAgregado = false;
        agregarSimbolo(secuenciaSonido, duracion < UMBRAL_PUNTO_RAYA);
      }
    }
    sensorActivoAnterior = sensorActivo;
  } else {
    sonidoDetectado = false;
    sensorActivoAnterior = false;
    contadorEstabilidad = 0;
  }

  // ---------- DECODIFICAR BOTÓN (LETRAS Y PALABRAS) ----------
  if (estadoBoton == HIGH) {
    unsigned long silencio = ahora - tiempoUltimoEventoBoton;
    if (secuenciaBoton.length() > 0 && silencio >= UMBRAL_LETRA) {
      finalizarLetra(secuenciaBoton, textoBoton, ultimoMorseBoton, "BOTON", false);
    }
    if (!espacioBotonAgregado && secuenciaBoton.length() == 0 && textoBoton.length() > 0 && silencio >= UMBRAL_PALABRA) {
      textoBoton += " ";
      espacioBotonAgregado = true;
    }
  }

  // ---------- DECODIFICAR SENSOR (LETRAS Y PALABRAS) ----------
  if (microfonoHabilitado && !sonidoDetectado) {
    unsigned long silencio = ahora - tiempoUltimoEventoSonido;
    if (secuenciaSonido.length() > 0 && silencio >= UMBRAL_LETRA) {
      finalizarLetra(secuenciaSonido, textoSonido, ultimoMorseSonido, "SONIDO", false);
    }
    if (!espacioSonidoAgregado && secuenciaSonido.length() == 0 && textoSonido.length() > 0 && silencio >= UMBRAL_PALABRA) {
      textoSonido += " ";
      espacioSonidoAgregado = true;
    }
  }

  // ---------- REPRODUCCIÓN POR BUZZER ----------
  procesarReproduccion(ahora);

  delay(2);
}