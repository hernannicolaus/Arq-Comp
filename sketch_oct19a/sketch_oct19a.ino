#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

// ================= PINES (SIN CONFLICTO) =================
const int PIN_BOTON    = 12;  // D6 / GPIO12
const int PIN_SENSOR   = 13;  // D7 / GPIO13  (salida DIGITAL del módulo)
const int PIN_LED      = 2;   // D4 / GPIO2
const int PIN_BUZZER   = 14;  // D5 / GPIO14

// ================= WIFI =================
const char* WIFI_NOMBRE = "MorseESP8266";
const char* WIFI_CLAVE  = "Morse123";

ESP8266WebServer server(80);

// ================= UMBRALES MORSE =================
const unsigned long UMBRAL_PUNTO_RAYA = 200;
const unsigned long UMBRAL_LETRA      = 800;
const unsigned long UMBRAL_PALABRA    = 2000;
const unsigned long FILTRO_SENSOR     = 40;

const int NIVEL_ACTIVO_SENSOR = LOW;

// ================= FRECUENCIA DEL BUZZER =================
// Ajusta si tu buzzer pasivo suena débil o no suena
const unsigned int FRECUENCIA_BUZZER = 2000;  // Hz

// ================= ESTADO BOTÓN =================
int estadoAnteriorBoton = HIGH;
unsigned long tiempoInicioBoton = 0;
unsigned long tiempoUltimoEventoBoton = 0;
bool botonPresionado = false;

// ================= ESTADO SENSOR =================
int lecturaAnteriorSensor = HIGH;
int lecturaEstableSensor  = HIGH;
unsigned long tiempoCambioSensor = 0;
unsigned long tiempoInicioSonido  = 0;
unsigned long tiempoUltimoEventoSonido = 0;
bool sensorActivoAnterior = false;
bool sonidoDetectado = false;

// ================= MICRÓFONO =================
bool microfonoHabilitado = true;

// ================= SECUENCIAS =================
String secuenciaBoton  = "";
String secuenciaSonido = "";
String textoBoton  = "";
String textoSonido = "";
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
    tone(PIN_BUZZER, FRECUENCIA_BUZZER, 100);
    reproduciendo = true;
    tiempoSiguienteSimbolo = ahora + 100;
  } else if (c == '-') {
    tone(PIN_BUZZER, FRECUENCIA_BUZZER, 300);
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
<header><h1>Teclado Morse <span>· Receptor ESP8266</span></h1></header>
<main class="chat">

  <section class="panel">
    <div class="panel-head">
      <h2>Sensor de sonido (app del celular)</h2>
      <div class="estado"><span class="led" id="sonidoLed"></span><span id="sonidoEstado">Esperando</span></div>
    </div>
    <div class="historial" id="sonidoTexto">—</div>
    <div class="codigo" id="sonidoCodigo">—</div>
    <div class="meta"><span>último código</span><code id="sonidoUltimo">—</code></div>
    <div class="control">
      <button id="btnMicro" class="habilitado">🎤 Micrófono: ACTIVO</button>
    </div>
    <div class="aviso">
      Desactivá el micrófono mientras usás el botón manual o el buzzer para evitar
      que el sensor capte esos sonidos y los decodifique por error.
    </div>
  </section>

  <section class="panel">
    <div class="panel-head">
      <h2>Botón manual</h2>
      <div class="estado"><span class="led" id="botonLed"></span><span id="botonEstado">Listo</span></div>
    </div>
    <div class="historial" id="botonTexto">—</div>
    <div class="codigo" id="botonCodigo">—</div>
    <div class="meta"><span>último código</span><code id="botonUltimo">—</code></div>
  </section>

</main>
<footer>
  <b>Punto</b> = pulso corto · <b>Raya</b> = pulso largo.<br>
  La letra se agrega cuando hacés una pausa. La vista se refresca sola.
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
      $('sonidoEstado').textContent = d.sonidoActivo ? 'Detectando' : 'Esperando';
      $('sonidoLed').className = d.sonidoActivo ? 'led on' : 'led';
    }

    $('botonEstado').textContent = d.botonActivo ? 'Presionado' : 'Listo';
    $('botonLed').classList.toggle('on', !!d.botonActivo);

    $('botonCodigo').textContent  = d.morseBoton  || '—';
    $('sonidoCodigo').textContent = d.morseSonido || '—';

    $('botonTexto').textContent  = d.textoBoton  || '—';
    $('sonidoTexto').textContent = d.textoSonido || '—';

    $('botonUltimo').textContent  = d.ultimoBoton  || '—';
    $('sonidoUltimo').textContent = d.ultimoSonido || '—';

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
setInterval(actualizar, 300);
</script>
</body>
</html>
)HTML";

// ================= JSON DE ESTADO =================
void enviarEstado() {
  String json = "{";
  json += "\"botonActivo\":";  json += (botonPresionado ? "true" : "false");
  json += ",\"sonidoActivo\":"; json += (sonidoDetectado ? "true" : "false");
  json += ",\"microfono\":";    json += (microfonoHabilitado ? "true" : "false");

  json += ",\"morseBoton\":\"";  json += secuenciaBoton;  json += "\"";
  json += ",\"morseSonido\":\""; json += secuenciaSonido; json += "\"";

  json += ",\"textoBoton\":\"";  json += textoBoton;  json += "\"";
  json += ",\"textoSonido\":\""; json += textoSonido; json += "\"";

  json += ",\"ultimoBoton\":\"";  json += ultimoMorseBoton;  json += "\"";
  json += ",\"ultimoSonido\":\""; json += ultimoMorseSonido; json += "\"}";

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
      lecturaAnteriorSensor = digitalRead(PIN_SENSOR);
      lecturaEstableSensor  = lecturaAnteriorSensor;
      tiempoCambioSensor    = millis();
      Serial.print("[MIC] ");
      Serial.println(microfonoHabilitado ? "ACTIVO" : "DESACTIVADO");
    }
  }
  server.send(200, "text/plain", microfonoHabilitado ? "on" : "off");
}

// ================= CONFIGURAR WEB =================
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
  pinMode(PIN_SENSOR, INPUT);
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);

  digitalWrite(PIN_LED, HIGH);
  digitalWrite(PIN_BUZZER, LOW);

  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("=== MORSE RECEPTOR ESP8266 ===");

  // Prueba de sonido al arrancar (para verificar el buzzer)
  Serial.println("Prueba de buzzer...");
  tone(PIN_BUZZER, FRECUENCIA_BUZZER, 500);
  delay(600);
  noTone(PIN_BUZZER);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(WIFI_NOMBRE, WIFI_CLAVE);

  Serial.print("Red Wi-Fi: "); Serial.println(WIFI_NOMBRE);
  Serial.print("Clave: ");     Serial.println(WIFI_CLAVE);
  Serial.print("Abre: http://"); Serial.println(WiFi.softAPIP());

  iniciarWeb();

  tiempoUltimoEventoBoton = millis();
  tiempoUltimoEventoSonido = millis();

  lecturaAnteriorSensor = digitalRead(PIN_SENSOR);
  lecturaEstableSensor  = lecturaAnteriorSensor;
  tiempoCambioSensor = millis();

  Serial.println("Servidor web iniciado.");
}

// ================= LOOP =================
void loop() {
  server.handleClient();
  unsigned long ahora = millis();

  // ---------- BOTÓN MANUAL ----------
  int estadoBoton = digitalRead(PIN_BOTON);
  botonPresionado = (estadoBoton == LOW);

  if (estadoBoton == LOW && estadoAnteriorBoton == HIGH) {
    tiempoInicioBoton = ahora;
    digitalWrite(PIN_LED, LOW);
    // Buzzer pasivo: tone() continuo mientras se mantiene pulsado
    tone(PIN_BUZZER, FRECUENCIA_BUZZER);
  }
  if (estadoBoton == HIGH && estadoAnteriorBoton == LOW) {
    unsigned long duracion = ahora - tiempoInicioBoton;
    // Detener el tono continuo
    noTone(PIN_BUZZER);
    digitalWrite(PIN_LED, HIGH);
    tiempoUltimoEventoBoton = ahora;
    agregarSimbolo(secuenciaBoton, duracion < UMBRAL_PUNTO_RAYA);
  }
  estadoAnteriorBoton = estadoBoton;

  // ---------- SENSOR (SOLO SI MIC HABILITADO) ----------
  if (microfonoHabilitado) {
    int lectura = digitalRead(PIN_SENSOR);
    if (lectura != lecturaAnteriorSensor) {
      lecturaAnteriorSensor = lectura;
      tiempoCambioSensor = ahora;
    }
    if (ahora - tiempoCambioSensor >= FILTRO_SENSOR) {
      lecturaEstableSensor = lecturaAnteriorSensor;
    }
    bool sensorActivo = (lecturaEstableSensor == NIVEL_ACTIVO_SENSOR);
    sonidoDetectado = sensorActivo;

    if (sensorActivo && !sensorActivoAnterior) {
      tiempoInicioSonido = ahora;
      digitalWrite(PIN_LED, LOW);
    }
    if (!sensorActivo && sensorActivoAnterior) {
      unsigned long duracion = ahora - tiempoInicioSonido;
      digitalWrite(PIN_LED, HIGH);
      tiempoUltimoEventoSonido = ahora;
      if (duracion >= 30) {
        agregarSimbolo(secuenciaSonido, duracion < UMBRAL_PUNTO_RAYA);
      }
    }
    sensorActivoAnterior = sensorActivo;
  } else {
    sonidoDetectado = false;
    sensorActivoAnterior = false;
  }

  // ---------- DECODIFICAR BOTÓN ----------
  if (estadoBoton == HIGH && secuenciaBoton.length() > 0) {
    unsigned long silencio = ahora - tiempoUltimoEventoBoton;
    if (silencio >= UMBRAL_LETRA) {
      finalizarLetra(secuenciaBoton, textoBoton, ultimoMorseBoton, "BOTON", false);
    }
  }

  // ---------- DECODIFICAR SENSOR + RE-EMITIR ----------
  if (microfonoHabilitado && !sonidoDetectado && secuenciaSonido.length() > 0) {
    unsigned long silencio = ahora - tiempoUltimoEventoSonido;
    if (silencio >= UMBRAL_LETRA) {
      finalizarLetra(secuenciaSonido, textoSonido, ultimoMorseSonido, "SONIDO", true);
    }
  }

  // ---------- REPRODUCCIÓN POR BUZZER ----------
  procesarReproduccion(ahora);

  delay(2);
}
