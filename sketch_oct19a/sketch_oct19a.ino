
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <LedControl.h>
#include <math.h>

// Pines NodeMCU
#define BOTON 12     // D6 - GPIO12
#define LED 2        // D4 - LED integrado
#define BUZZER 14    // D5 - GPIO14
#define MIC A0       // Entrada analogica
#define DIN 4        // D2 - GPIO4
#define CLK 5        // D1 - GPIO5
#define CS 16        // D0 - GPIO16

// WiFi original
const char* WIFI = "MorseESP8266";
const char* CLAVE = "Morse123";

// Matriz desactivada hasta identificar DIN
const bool MATRIZ_HABILITADA = false;

// Configuracion Morse
const int FREQ = 700, N = 80;
const unsigned long UNIDAD = 300, RAYA = 600;
const unsigned long LETRA = 1100, PALABRA = 2700;
const float FS = 4000.0f;

ESP8266WebServer server(80);
LedControl matriz(DIN, CLK, CS, 1);

float energia = 0, umbral = 12.0f;
bool microON = false, tono = false, botonON = false, botonAnterior = false;
bool txON = false, txSonando = false, espacioB = true, espacioM = true;
bool matrizTemporal = false;
int positivos = 0, negativos = 0;

unsigned long inicioTono = 0, ultimoTono = 0, inicioBoton = 0, ultimoBoton = 0;
unsigned long cambioBoton = 0, duracion = 0, siguienteTX = 0;
unsigned long finMatriz = 0, ultimaMuestra = 0;

String secB="", secM="", textoB="", textoM="", ultimoB="", ultimoM="";
String colaTX="", mensajeTX="";

// Diccionario
const char letras[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
const char* codigos[] = {
  ".-","-...","-.-.","-..",".","..-.","--.","....","..",".---",
  "-.-",".-..","--","-.","---",".--.","--.-",".-.","...","-",
  "..-","...-",".--","-..-","-.--","--..",
  "-----",".----","..---","...--","....-",".....",
  "-....","--...","---..","----."
};

// Fuente 5x7
const char caracteres[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ.-/? ";
const uint8_t fuente[][5] PROGMEM = {
  {62,81,73,69,62},{0,66,127,64,0},{66,97,81,73,70},
  {33,65,69,75,49},{24,20,18,127,16},{39,69,69,69,57},
  {60,74,73,73,48},{1,113,9,5,3},{54,73,73,73,54},
  {6,73,73,41,30},
  {126,17,17,17,126},{127,73,73,73,54},{62,65,65,65,34},
  {127,65,65,34,28},{127,73,73,73,65},{127,9,9,9,1},
  {62,65,73,73,122},{127,8,8,8,127},{0,65,127,65,0},
  {32,64,65,63,1},{127,2,12,2,127},{127,4,8,16,127},
  {62,65,65,65,62},{127,9,9,9,6},{62,65,81,33,94},
  {127,9,25,41,70},{70,73,73,73,49},{1,1,127,1,1},
  {63,64,64,64,63},{31,32,64,32,31},{127,32,24,32,127},
  {99,20,8,20,99},{3,4,120,4,3},{97,81,73,69,67},
  {0,96,96,0,0},{8,8,8,8,8},{64,48,8,6,1},
  {2,1,81,9,6},{0,0,0,0,0}
};

// Matriz
void mostrar(char c) {
  if (!MATRIZ_HABILITADA) return;
  c = toupper((unsigned char)c);
  int idx = strlen(caracteres)-1;

  for (unsigned int i=0; i<strlen(caracteres); i++)
    if (caracteres[i]==c) { idx=i; break; }

  matriz.clearDisplay(0);
  for (int x=0; x<5; x++) {
    byte b=pgm_read_byte(&fuente[idx][x]);
    for (int y=0; y<7; y++) matriz.setLed(0,y,x+1,(b>>y)&1);
  }
}

void mostrarPor(char c, unsigned long ms) {
  if (!MATRIZ_HABILITADA) return;
  mostrar(c); finMatriz=millis()+ms; matrizTemporal=true;
}

void mostrarIP() {
  String ip=WiFi.softAPIP().toString();
  Serial.println("IP: "+ip);
  if (!MATRIZ_HABILITADA) return;

  for (unsigned int i=0; i<ip.length(); i++) {
    mostrar(ip[i]); delay(750); yield();
  }
  matriz.clearDisplay(0);
}

// Conversion Morse
String codificar(char c) {
  c=toupper((unsigned char)c);
  for (int i=0; i<36; i++) if (letras[i]==c) return codigos[i];
  return "";
}

char decodificar(String s) {
  for (int i=0; i<36; i++) if (s==codigos[i]) return letras[i];
  return '?';
}

String convertir(String texto) {
  texto.toUpperCase();
  String salida="";
  bool anterior=false, espacio=false;

  for (unsigned int i=0; i<texto.length(); i++) {
    char c=texto[i];
    if (c==' ') { if (anterior) espacio=true; continue; }

    String codigo=codificar(c);
    if (!codigo.length()) continue;

    if (anterior) salida+=espacio?"/":" ";
    salida+=codigo; anterior=true; espacio=false;
  }
  return salida;
}

// Decodificacion
void agregar(String &sec, bool punto) {
  if (sec.length()>=12) sec="";
  sec+=punto?".":"-";
  mostrarPor(punto?'.':'-',650);
  Serial.println(punto?"[RX] PUNTO":"[RX] RAYA");
}

void finalizar(String &sec, String &texto, String &ultimo) {
  if (!sec.length()) return;

  ultimo=sec;
  char c=decodificar(sec);
  texto+=c;
  if (texto.length()>100) texto.remove(0,texto.length()-100);

  mostrarPor(c,800);
  Serial.println("[RX] "+sec+" = "+String(c));
  sec="";
}

void pausa(String &sec, String &texto, bool &espacio,
           unsigned long ultimo, unsigned long ahora) {
  if (!sec.length() && !espacio && texto.length() &&
      ahora-ultimo>=PALABRA) {
    texto+=' ';
    if (texto.length()>100) texto.remove(0,texto.length()-100);
    mostrarPor('/',700);
    espacio=true;
  }
}

// Goertzel analogico
float goertzel() {
  float muestras[N], media=0;
  unsigned long t=micros();

  for (int i=0; i<N; i++) {
    while ((long)(micros()-t)<0) yield();
    muestras[i]=analogRead(MIC);
    media+=muestras[i];
    t+=250;
  }

  media/=N;
  float omega=2.0f*PI*FREQ/FS, coef=2.0f*cosf(omega);
  float q0=0,q1=0,q2=0;

  for (int i=0; i<N; i++) {
    float ventana=0.5f-0.5f*cosf(2.0f*PI*i/(N-1));
    q0=(muestras[i]-media)*ventana+coef*q1-q2;
    q2=q1; q1=q0;
  }

  float potencia=q1*q1+q2*q2-coef*q1*q2;
  return 4.0f*sqrtf(fmaxf(0.0f,potencia))/N;
}

void reiniciarMicro() {
  tono=false; positivos=negativos=0;
  energia=0; secM="";
  digitalWrite(LED,HIGH);
}

void leerMicro() {
  if (!microON || txON || botonON) return;
  if (millis()-ultimaMuestra<50) return;
  ultimaMuestra=millis();

  energia=goertzel();
  bool detectado=energia>umbral;
  unsigned long ahora=millis();

  if (detectado) {
    positivos=min(positivos+1,5);
    negativos=0;
  } else {
    negativos=min(negativos+1,5);
    positivos=0;
  }

  if (!tono && positivos>=2) {
    tono=true;
    inicioTono=ahora-20;
    digitalWrite(LED,LOW);
  }

  if (tono && negativos>=2) {
    tono=false;
    digitalWrite(LED,HIGH);

    duracion=ahora-inicioTono;
    if (duracion>20) duracion-=20;

    if (duracion>=65) {
      agregar(secM,duracion<RAYA);
      ultimoTono=ahora; espacioM=false;
    }
  }
}

// Boton
void leerBoton(unsigned long ahora) {
  bool lectura=digitalRead(BOTON)==LOW;

  if (lectura!=botonAnterior) {
    cambioBoton=ahora;
    botonAnterior=lectura;
  }

  if (ahora-cambioBoton<25 || lectura==botonON) return;

  botonON=lectura;

  if (botonON) {
    inicioBoton=ahora;
    if (!txON) {
      tone(BUZZER,FREQ);
      digitalWrite(LED,LOW);
    }
  } else if (!txON) {
    noTone(BUZZER);
    digitalWrite(LED,HIGH);

    duracion=ahora-inicioBoton;
    if (duracion>=40) {
      agregar(secB,duracion<RAYA);
      ultimoBoton=ahora;
      espacioB=false;
    }
  }
}

// Transmisor web
void iniciarTX(String texto) {
  texto.trim();
  if (texto.length()>60) texto=texto.substring(0,60);

  mensajeTX=texto;
  colaTX=convertir(texto);
  txON=colaTX.length()>0;
  txSonando=false;
  siguienteTX=millis();

  reiniciarMicro();
}

void procesarTX(unsigned long ahora) {
  if (!txON || (long)(ahora-siguienteTX)<0) return;

  if (txSonando) {
    noTone(BUZZER);
    digitalWrite(LED,HIGH);
    txSonando=false;
    siguienteTX=ahora+UNIDAD;
    return;
  }

  if (!colaTX.length()) {
    txON=false;
    noTone(BUZZER);
    mostrarPor(' ',100);
    ultimoTono=millis();
    return;
  }

  char c=colaTX[0];
  colaTX.remove(0,1);

  if (c=='.' || c=='-') {
    unsigned long tiempo=c=='.'?UNIDAD:3*UNIDAD;

    mostrarPor(c,tiempo);
    tone(BUZZER,FREQ);
    digitalWrite(LED,LOW);

    txSonando=true;
    siguienteTX=ahora+tiempo;
  } else {
    if (c=='/') mostrarPor('/',700);
    siguienteTX=ahora+(c=='/'?6:2)*UNIDAD;
  }
}

// Web
const char WEB[] PROGMEM=R"HTML(
<!DOCTYPE html><html lang="es"><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>MorseLab ESP8266</title>
<style>
*{box-sizing:border-box}
body{margin:0;background:#f1f5fa;color:#17253c;font:14px "Segoe UI",Arial}
header{background:white;padding:20px;border-bottom:1px solid #dde5ee}
nav{max-width:1050px;margin:auto;display:flex;justify-content:space-between;align-items:center}
.logo{font-size:24px;font-weight:800}.logo span{color:#2563eb}
.badge{background:#dcfce7;color:#166534;border-radius:20px;padding:8px 12px;font-size:12px}
main{max-width:1050px;margin:30px auto;padding:0 15px}
h1{font-size:26px;margin:0}h2{font-size:17px;margin:0}
p{color:#64748b;line-height:1.5}
.stats{display:grid;grid-template-columns:repeat(4,1fr);gap:12px;margin:22px 0}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:18px}
.card{background:white;padding:21px;border:1px solid #e2e8f0;border-radius:15px}
.label{font-size:11px;color:#64748b;font-weight:700;letter-spacing:.6px}
.valor{font-size:21px;font-weight:800;margin-top:10px}
.result{background:#f8fafc;border:1px solid #e2e8f0;border-radius:10px;padding:16px;margin:15px 0;min-height:105px}
.msg{font-size:23px;font-weight:700;margin:12px 0;overflow-wrap:anywhere}
.code{font:18px Consolas,monospace;color:#2563eb;overflow-wrap:anywhere}
.row{display:flex;justify-content:space-between;gap:10px;padding:11px 0;border-bottom:1px solid #e2e8f0}
textarea{width:100%;min-height:105px;padding:12px;border:1px solid #cbd5e1;border-radius:9px;font:15px Arial;margin:12px 0}
button{background:#2563eb;color:white;border:0;border-radius:9px;padding:13px;font-weight:700;cursor:pointer}
button:disabled{opacity:.5}.secondary{background:#e2e8f0;color:#17253c}
.actions{display:flex;gap:10px;margin-top:12px}.actions button{flex:1}
.preview{padding:14px;background:#f8fafc;border:1px dashed #cbd5e1;border-radius:9px;color:#2563eb;font:15px Consolas,monospace;overflow-wrap:anywhere}
.bar{height:12px;background:#e2e8f0;border-radius:10px;overflow:hidden;margin:13px 0}
.bar div{height:100%;background:#2563eb;width:0}
.full{grid-column:1/-1}footer{text-align:center;color:#94a3b8;padding:30px}
@media(max-width:750px){.grid{grid-template-columns:1fr}.stats{grid-template-columns:repeat(2,1fr)}.full{grid-column:auto}}
</style></head><body>
<header><nav><div class="logo">Morse<span>Lab</span></div><div class="badge" id="conexion">Conectando</div></nav></header>
<main><h1>Centro de comunicacion Morse</h1>
<p>ESP8266 - Recepcion acustica, Goertzel y transmision Morse.</p>
<div class="stats">
<div class="card"><div class="label">DIRECCION IP</div><div class="valor" id="ip">—</div></div>
<div class="card"><div class="label">FRECUENCIA</div><div class="valor">700 Hz</div></div>
<div class="card"><div class="label">MICROFONO</div><div class="valor" id="microStat">—</div></div>
<div class="card"><div class="label">TRANSMISION</div><div class="valor" id="txStat">Listo</div></div>
</div>
<div class="grid">
<section class="card"><h2>Recepcion por microfono</h2>
<p>Deteccion mediante algoritmo Goertzel.</p>
<div class="result"><div class="label">MENSAJE RECIBIDO</div><div class="msg" id="textoSonido">—</div><div class="code" id="morseSonido">—</div></div>
<div class="row"><span>Ultimo codigo</span><b id="ultimoSonido">—</b></div>
<div class="row"><span>Nivel Goertzel</span><b id="nivel">0</b></div>
<div class="bar"><div id="barra"></div></div>
<div class="row"><span>Estado</span><b id="senal">Silencio</b></div>
<div class="actions"><button id="microBtn" onclick="toggleMicro()">Activar microfono</button></div>
</section>

<section class="card"><h2>Boton manual</h2>
<p>Ingreso Morse mediante pulsador.</p>
<div class="result"><div class="label">MENSAJE DECODIFICADO</div><div class="msg" id="textoBoton">—</div><div class="code" id="morseBoton">—</div></div>
<div class="row"><span>Ultimo codigo</span><b id="ultimoBoton">—</b></div>
<div class="row"><span>Boton</span><b id="botonEstado">Libre</b></div>
<div class="row"><span>Ultimo pulso</span><b id="duracion">0 ms</b></div>
</section>

<section class="card"><h2>Transmisor Morse</h2>
<p>Escribi un mensaje y reproducilo mediante el buzzer.</p>
<textarea id="mensaje" maxlength="60" placeholder="HOLA MUNDO" oninput="preview()"></textarea>
<div class="preview" id="prev">—</div>
<div class="actions"><button id="enviarBtn" onclick="enviar()">Transmitir</button><button class="secondary" onclick="limpiarEntrada()">Limpiar</button></div>
<p id="estadoEnvio">Transmisor listo.</p></section>

<section class="card"><h2>Estado del sistema</h2>
<div class="row"><span>Goertzel</span><b>700 Hz</b></div>
<div class="row"><span>Muestreo nominal</span><b>4000 Hz</b></div>
<div class="row"><span>Ventana</span><b>80 muestras</b></div>
<div class="row"><span>Umbral</span><b id="umbral">12</b></div>
<div class="row"><span>Punto</span><b>300 ms</b></div>
<div class="row"><span>Raya</span><b>900 ms</b></div>
<div class="row"><span>Matriz</span><b id="matrizEstado">—</b></div>
<div class="row"><span>Clientes WiFi</span><b id="clientes">0</b></div>
</section>

<section class="card full"><h2>Administracion</h2>
<button class="secondary" onclick="borrar()">Borrar mensajes</button></section>
</div></main><footer>MorseLab - ESP8266</footer>
<script>
const $=id=>document.getElementById(id),abc="ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
const dic=[".-","-...","-.-.","-..",".","..-.","--.","....","..",".---","-.-",".-..","--","-.","---",".--.","--.-",".-.","...","-","..-","...-",".--","-..-","-.--","--..","-----",".----","..---","...--","....-",".....","-....","--...","---..","----."];
let micro=false,ocupado=false;
function convertir(t){return t.toUpperCase().trim().split(/\s+/).map(p=>[...p].map(c=>dic[abc.indexOf(c)]||"").filter(Boolean).join(" ")).filter(Boolean).join(" / ")}
function preview(){$("prev").textContent=convertir($("mensaje").value)||"—"}
function limpiarEntrada(){$("mensaje").value="";preview()}
async function enviar(){
 let t=$("mensaje").value.trim();if(!t||ocupado)return;ocupado=true;
 try{
  let r=await fetch("/enviar",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body:"texto="+encodeURIComponent(t)});
  $("estadoEnvio").textContent=r.ok?"Mensaje enviado.":await r.text();
 }catch(e){$("estadoEnvio").textContent="Error de conexion"}finally{ocupado=false}
}
async function toggleMicro(){
 try{let r=await fetch("/micro?on="+(micro?0:1));if(r.ok)actualizar()}catch(e){}
}
async function borrar(){try{await fetch("/limpiar",{method:"POST"});actualizar()}catch(e){}}
async function actualizar(){
 try{
  let r=await fetch("/estado",{cache:"no-store"});if(!r.ok)throw Error();
  let d=await r.json();micro=d.microfono;
  $("conexion").textContent="Conectado";$("ip").textContent=d.ip;
  $("microStat").textContent=micro?"Activo":"Apagado";
  $("txStat").textContent=d.transmitiendo?"Enviando":"Listo";
  $("enviarBtn").disabled=d.transmitiendo||d.botonActivo;
  for(let k of ["textoSonido","textoBoton","morseSonido","morseBoton","ultimoSonido","ultimoBoton"])$(k).textContent=d[k]||"—";
  $("nivel").textContent=Number(d.nivel).toFixed(1);
  $("umbral").textContent=d.umbral;
  $("barra").style.width=Math.min(100,d.nivel/Math.max(d.umbral*3,1)*100)+"%";
  $("senal").textContent=!micro?"Desactivado":d.tono?"Tono detectado":"Silencio";
  $("botonEstado").textContent=d.botonActivo?"Presionado":"Libre";
  $("duracion").textContent=d.duracion+" ms";
  $("matrizEstado").textContent=d.matriz?"Habilitada":"Deshabilitada";
  $("clientes").textContent=d.clientes;
  $("microBtn").textContent=micro?"Desactivar microfono":"Activar microfono";
 }catch(e){$("conexion").textContent="Sin conexion"}
}
actualizar();setInterval(actualizar,800);
</script></body></html>
)HTML";

// JSON
String seguro(String s) {
  s.replace("\\","\\\\"); s.replace("\"","\\\"");
  s.replace("\n"," "); s.replace("\r"," ");
  return s;
}

void estadoWeb() {
  String j="{\"ip\":\""+WiFi.softAPIP().toString()+"\"";
  j+=",\"microfono\":"+String(microON?"true":"false");
  j+=",\"tono\":"+String(tono?"true":"false");
  j+=",\"botonActivo\":"+String(botonON?"true":"false");
  j+=",\"transmitiendo\":"+String(txON?"true":"false");
  j+=",\"matriz\":"+String(MATRIZ_HABILITADA?"true":"false");
  j+=",\"clientes\":"+String(WiFi.softAPgetStationNum());
  j+=",\"nivel\":"+String(energia,2)+",\"umbral\":"+String(umbral,2);
  j+=",\"duracion\":"+String(duracion);
  j+=",\"textoBoton\":\""+seguro(textoB)+"\",\"textoSonido\":\""+seguro(textoM)+"\"";
  j+=",\"morseBoton\":\""+secB+"\",\"morseSonido\":\""+secM+"\"";
  j+=",\"ultimoBoton\":\""+ultimoB+"\",\"ultimoSonido\":\""+ultimoM+"\"}";
  server.sendHeader("Cache-Control","no-store");
  server.send(200,"application/json",j);
}

// Rutas web
void iniciarWeb() {
  server.on("/",HTTP_GET,[](){
    server.send_P(200,"text/html; charset=utf-8",WEB);
  });

  server.on("/estado",HTTP_GET,estadoWeb);

  server.on("/micro",HTTP_GET,[](){
    microON=server.arg("on")=="1";
    reiniciarMicro();
    ultimoTono=millis();
    espacioM=true;
    server.send(200,"text/plain",microON?"on":"off");
  });

  server.on("/enviar",HTTP_POST,[](){
    if (txON||botonON) {
      server.send(409,"text/plain","Transmisor ocupado");
      return;
    }
    String s=server.arg("texto");s.trim();
    if (!s.length()||s.length()>60) {
      server.send(400,"text/plain","Mensaje invalido");
      return;
    }
    iniciarTX(s);
    server.send(txON?200:400,"text/plain",txON?"OK":"Sin caracteres compatibles");
  });

  server.on("/limpiar",HTTP_POST,[](){
    textoB=textoM=secB=secM=ultimoB=ultimoM="";
    espacioB=espacioM=true;
    server.send(200,"text/plain","OK");
  });

  server.onNotFound([](){
    server.send(404,"text/plain","No encontrado");
  });
  server.begin();
}

// Inicio
void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(BOTON,INPUT_PULLUP);
  pinMode(LED,OUTPUT);
  pinMode(BUZZER,OUTPUT);
  digitalWrite(LED,HIGH);
  noTone(BUZZER);

  if (MATRIZ_HABILITADA) {
    matriz.shutdown(0,false);
    matriz.setScanLimit(0,7);
    matriz.setIntensity(0,3);
    matriz.clearDisplay(0);
  }

  // WiFi original que ya funcionaba
  WiFi.mode(WIFI_AP);
  bool ok=WiFi.softAP(WIFI,CLAVE);

  Serial.println("\n=== MORSELAB ESP8266 ===");
  Serial.println(ok?"WiFi iniciado":"ERROR WiFi");
  Serial.println("SSID: "+String(WIFI));
  Serial.println("Clave: "+String(CLAVE));
  Serial.println("IP: "+WiFi.softAPIP().toString());

  iniciarWeb();
  mostrarIP();

  ultimoBoton=ultimoTono=cambioBoton=millis();
  Serial.println("Servidor web iniciado.");
}

// Bucle
void loop() {
  server.handleClient();
  unsigned long ahora=millis();

  leerBoton(ahora);
  procesarTX(ahora);
  leerMicro();

  ahora=millis();

  if (!botonON && secB.length() && ahora-ultimoBoton>=LETRA)
    finalizar(secB,textoB,ultimoB);

  if (microON && !tono && !txON && !botonON &&
      secM.length() && ahora-ultimoTono>=LETRA)
    finalizar(secM,textoM,ultimoM);

  if (!botonON)
    pausa(secB,textoB,espacioB,ultimoBoton,ahora);

  if (microON && !tono && !txON && !botonON)
    pausa(secM,textoM,espacioM,ultimoTono,ahora);

  if (MATRIZ_HABILITADA && matrizTemporal &&
      (long)(ahora-finMatriz)>=0) {
    matrizTemporal=false;
    mostrar(' ');
  }

  static unsigned long revision=0;+
  if (ahora-revision>=5000) {
    revision=ahora;
    Serial.print("Clientes WiFi: ");
    Serial.println(WiFi.softAPgetStationNum());
    Serial.print("Memoria libre: ");
    Serial.println(ESP.getFreeHeap());
  }

  delay(2);
}
