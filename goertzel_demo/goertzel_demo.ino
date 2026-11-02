// ============================================================================
//  goertzel_demo.ino - Demo AISLADO del algoritmo de Goertzel
//  TP Arquitectura de Computadoras - Receptor Morse por audio (ESP8266)
//
//  Objetivo: validar el algoritmo de Goertzel sobre la salida ANALÓGICA del
//  módulo de sonido (A0) y calibrar el umbral de detección en la placa real.
//
//  Este sketch NO forma parte del firmware principal (sketch_oct19a):
//  no usa WiFi, ni servidor web, ni display. Sólo Serial a 115200 baudios.
//  Todo el DSP está escrito a mano, sin librerías externas.
//
//  Cadena de procesamiento:
//    A0 -> bloque de N muestras a Fs -> resta de DC -> Goertzel (bin k)
//       -> magnitud -> amp = 2*mag/N -> histéresis -> TONO / SILENCIO
//       -> duración -> punto / raya
// ============================================================================

#include <Arduino.h>   // único include permitido: ya provee PI, cos() y sqrt()

// ============================================================================
//  BLOQUE DE AJUSTE - todo lo afinable está acá
// ============================================================================

// Frecuencia del tono Morse a detectar, en Hz.
// Ajustar al tono que emite el celular o el generador (por defecto 700 Hz).
#define TONO_HZ              700

// Frecuencia de muestreo, en Hz. Con 4000 Hz el período es 250 us.
// Más alta = más margen sobre Nyquist, pero menos tiempo entre analogRead().
#define FS_HZ                4000

// Cantidad de muestras por bloque (ventana de análisis).
// 400 muestras a 4000 Hz = 100 ms de señal => resolución Fs/N = 10 Hz.
// Ventana más larga = mejor resolución en frecuencia, pero peor resolución
// temporal: un punto Morse de ~60 ms ya no entraría completo en el bloque.
#define N_MUESTRAS           400

// Umbral de detección, en CUENTAS DE ADC de amplitud estimada.
// No es el power crudo: gracias a la normalización 2*mag/N el número es
// comparable con la amplitud real de la senoidal (escala 0..1023 cuentas).
// Calibrar con la línea STATS: dejarlo entre el ruido de fondo y el tono.
#define UMBRAL_TONO          12.0f

// Histéresis temporal: bloques consecutivos por encima / por debajo del
// umbral antes de cambiar de estado. Evita que TONO/SILENCIO titile.
// Con bloques de 100 ms, 2 bloques = 200 ms de confirmación.
#define BLOQUES_ON           2
#define BLOQUES_OFF          2

// Duración (ms) que separa un punto de una raya:
//   duración <  PUNTO_MAX_MS -> punto (.)
//   duración >= PUNTO_MAX_MS -> raya  (-)
// Un punto Morse "estándar" ronda los 60-120 ms según la velocidad (WPM).
#define PUNTO_MAX_MS         200

// Fondo de escala del ADC del ESP8266: analogRead(A0) devuelve 0..1023.
// OJO: el rango de entrada es 0..1.0 V, NO 0..5 V. No cambiar salvo que
// se use otro ADC.
#define ADC_MAX              1023

// Aviso de saturación: si el pico absoluto del bloque llega a este valor,
// el ADC está pegado al riel y hay que bajar la ganancia del potenciómetro.
#define UMBRAL_SATURACION    1000

// Cada cuántos milisegundos se imprime la línea de estadísticas (calibración).
#define PERIODO_STATS_MS     10000

// 0 = salida normal. 1 = agrega magnitud cruda y power a cada línea de bloque.
#define MODO_CRUDO           0

// Pin de entrada analógica. En NodeMCU / Wemos D1 es A0 (único ADC).
#define PIN_MICROFONO        A0

// ---------------------------------------------------------------------------
//  Constantes derivadas (no hace falta tocarlas)
// ---------------------------------------------------------------------------

// Bin de Goertzel que corresponde a TONO_HZ: k = round(N * f / Fs).
// Con 700 Hz, N=400 y Fs=4000 => k = 70 exacto.
const int          K_BIN               = (int)((float)N_MUESTRAS * TONO_HZ / FS_HZ + 0.5f);

// Período de muestreo en microsegundos (250 us con Fs = 4000 Hz).
// Si Fs no divide exacto 1e6, se redondea al microsegundo más cercano.
const unsigned int PERIODO_MUESTREO_US = (unsigned int)(1000000UL / FS_HZ);

// Resolución en frecuencia del bloque: Fs / N (10 Hz con los valores por defecto).
const float        RESOLUCION_HZ       = (float)FS_HZ / (float)N_MUESTRAS;

// Verificación en tiempo de compilación: el bin tiene que existir y ser útil.
#if (TONO_HZ >= (FS_HZ / 2))
#error "TONO_HZ debe quedar por debajo de Fs/2 (teorema de Nyquist)."
#endif
#if (TONO_HZ < (FS_HZ / N_MUESTRAS))
#error "TONO_HZ cae por debajo del primer bin resoluble (Fs/N)."
#endif
#if (UMBRAL_SATURACION > ADC_MAX)
#error "UMBRAL_SATURACION no puede superar el fondo de escala del ADC (ADC_MAX)."
#endif

// ============================================================================
//  ESTADO GLOBAL
// ============================================================================

// Buffer de muestras a nivel de archivo (400 int = 800 bytes).
// Es global a propósito: en la pila del loop de un ESP8266 (4 KB) un array
// de este tamaño puede desbordarla, y este sketch no necesita reentrancia.
static int muestras[N_MUESTRAS];

unsigned long bloqueActual     = 0;   // contador de bloques desde el arranque
bool          enTono           = false; // estado filtrado (con histéresis)
unsigned int  contadorOn       = 0;   // bloques consecutivos sobre el umbral
unsigned int  contadorOff      = 0;   // bloques consecutivos bajo el umbral

// Marcas de tiempo de los flancos REALES (no del bloque que confirma el
// cambio). Así la histéresis no infla la duración medida del símbolo.
unsigned long inicioTonoMs     = 0;   // primer bloque del tono confirmado
unsigned long inicioCandidatoMs = 0;  // primer bloque sobre umbral estando en silencio
unsigned long finCandidatoMs   = 0;   // primer bloque bajo umbral estando en tono

bool          saturadoAntes    = false; // para avisar sólo al entrar en saturación
unsigned long totalSobrepasos  = 0;     // muestras tomadas tarde (diagnóstico de Fs)

// Estadísticas para calibrar el umbral en la placa real.
struct Estadisticas {
  unsigned int n;       // cantidad de bloques acumulados
  float        minAmp;  // mínimo de amp visto
  float        maxAmp;  // máximo de amp visto
  float        suma;    // suma de amp (para el promedio)
};

Estadisticas statsSilencio = { 0, 0.0f, 0.0f, 0.0f };
Estadisticas statsTono     = { 0, 0.0f, 0.0f, 0.0f };

unsigned long ultimoStatsMs = 0;

// Evento de flanco detectado en un bloque (se imprime después de la línea B).
enum TipoEvento { EVENTO_NINGUNO, EVENTO_INICIO, EVENTO_FIN };

struct Evento {
  TipoEvento    tipo;
  unsigned long duracionMs;
};

// ============================================================================
//  BANNER DE ARRANQUE
// ============================================================================

void imprimirBanner() {
  Serial.println();
  Serial.println("=== DEMO GOERTZEL - DETECCION DE TONO MORSE (ESP8266) ===");
  Serial.println("Entrada: A0 (analogRead 0..1023, rango 0..1.0 V)");
  Serial.printf("Fs = %d Hz (periodo %u us) | N = %d muestras | bloque = %.0f ms\n",
                FS_HZ, PERIODO_MUESTREO_US, N_MUESTRAS,
                1000.0f * (float)N_MUESTRAS / (float)FS_HZ);
  Serial.printf("Resolucion en frecuencia = %.1f Hz (Fs/N)\n", RESOLUCION_HZ);
  Serial.printf("Tono objetivo = %d Hz | k = %d | frecuencia real del bin = %.1f Hz\n",
                TONO_HZ, K_BIN, (float)K_BIN * (float)FS_HZ / (float)N_MUESTRAS);
  Serial.printf("Umbral TONO = %.1f cuentas ADC | ON = %d bloques | OFF = %d bloques\n",
                (float)UMBRAL_TONO, BLOQUES_ON, BLOQUES_OFF);
  Serial.printf("Clasificacion: < %d ms = punto (.) | >= %d ms = raya (-)\n",
                PUNTO_MAX_MS, PUNTO_MAX_MS);
  Serial.printf("Saturacion si max|x| >= %d (bajar ganancia del potenciometro)\n",
                UMBRAL_SATURACION);
  Serial.println("Formato por bloque: B0001 dc= 512 amp=  3.4 max= 520 silencio");
  Serial.println();

  if (K_BIN <= 0 || K_BIN >= (N_MUESTRAS / 2)) {
    Serial.println("AVISO: k fuera del rango util [1, N/2). Revisar TONO_HZ y Fs.");
  }
}

// ============================================================================
//  ADQUISICIÓN
// ============================================================================

// Llena 'destino' con n muestras de A0 a Fs, calcula la media (DC) del bloque
// y el pico absoluto CRUDO (sin restar DC).
//
// Timing: no se usa delay(). Se espera el próximo período con una espera
// activa acotada a PERIODO_MUESTREO_US (250 us), que es el mínimo inevitable
// para muestrear a Fs.
//
// Desborde de micros(): micros() vuelve a 0 cada ~71.6 min (2^32 us). Todas
// las comparaciones se hacen con aritmética modular de unsigned long,
// (long)(micros() - proximo) < 0. La diferencia es correcta mientras el
// intervalo sea menor a 2^31 us (~35 min), muchísimo mayor que los 250 us
// que manejamos, así que el wrap no afecta al muestreo.
//
// Si el lazo se atrasa más de un período (por ejemplo porque analogRead()
// tardó de más), NO se bloquea para "recuperar" el tiempo: se resincroniza
// 'proximo' con el reloj actual y se cuenta el atraso en totalSobrepasos.
// Preferimos honestidad en Fs antes que acumular deriva.
// OJO: si 'atrasos' crece en la línea STATS, la Fs real es menor que FS_HZ y
// el bin k deja de caer en TONO_HZ. En ese caso hay que subir N_MUESTRAS o
// bajar FS_HZ (o revisar por qué analogRead() se está demorando).
void leerBloque(int *destino, int n, float &dcMedia, int &maxAbs) {
  unsigned long proximo = micros();
  long suma = 0;
  maxAbs = 0;

  for (int i = 0; i < n; i++) {
    // Espera activa hasta el próximo flanco de muestreo (máx. un período).
    while ((long)(micros() - proximo) < 0) {
      // sin delay(): mantener la cadencia exacta de Fs
    }

    int v = analogRead(PIN_MICROFONO);   // 0..1023 en el ESP8266
    destino[i] = v;
    suma += v;

    // Pico absoluto sobre la muestra CRUDA: la saturación ocurre en los
    // rieles del ADC (0 / 1023), no en la señal ya centrada. Por eso se mide
    // antes de restar la media.
    int absv = (v < 0) ? -v : v;
    if (absv > maxAbs) maxAbs = absv;

    proximo += PERIODO_MUESTREO_US;

    // ¿Ya estamos atrasados más de un período? Resincronizar sin bloquear.
    if ((long)(micros() - proximo) >= 0) {
      proximo = micros() + PERIODO_MUESTREO_US;
      totalSobrepasos++;
    }
  }

  dcMedia = (float)suma / (float)n;
}

// Resta la media del bloque (se hace en el lugar, sobre el mismo buffer).
//
// POR QUÉ ES OBLIGATORIO: el módulo de sonido entrega la señal montada sobre
// un offset de continua (típicamente VCC/2). Ese offset de cientos de cuentas
// domina la recursión de Goertzel (los acumuladores s crecen con él), arrastra
// error de redondeo en float hacia el bin de interés y, además, el escalón de
// DC entre bloques consecutivos genera fuga espectral. Centrar el bloque
// elimina las tres cosas y hace que 'amp' sea la amplitud AC real.
//
// La media se redondea a entero porque las muestras son enteras: el error de
// ±0.5 cuentas es despreciable frente a decenas de cuentas de tono.
void restarMedia(int *destino, int n, float media) {
  int m = (int)(media + (media >= 0.0f ? 0.5f : -0.5f));
  for (int i = 0; i < n; i++) {
    destino[i] -= m;
  }
}

// ============================================================================
//  ALGORITMO DE GOERTZEL
// ============================================================================

// Calcula la magnitud del bin k con la recursión de segundo orden de Goertzel
// (una sola pasada, sin senos ni cosenos por muestra: sólo un coseno al inicio).
//
//   coeff = 2*cos(2*PI*k/N)
//   s = x + coeff*s_prev - s_prev2
//   power = s_prev2^2 + s_prev^2 - coeff*s_prev*s_prev2
//   magnitud = sqrt(power)
//
// Devuelve la magnitud y deja el power crudo en 'powerCrudo' (lo usa MODO_CRUDO).
//
// POR QUÉ 2/N DESPUÉS: para una senoidal x[n] = A*cos(2*PI*k*n/N) centrada en
// el bin k, la DFT da |X[k]| = A*N/2 (la otra mitad de la energía cae en el
// bin espejo N-k). Por eso la amplitud estimada es A = 2*|X[k]|/N. Esa
// normalización es la que vuelve al umbral interpretable en cuentas de ADC.
float goertzel(const int *x, int n, int k, float &powerCrudo) {
  const float coeff = 2.0f * cos(2.0f * PI * (float)k / (float)n);

  float sPrev  = 0.0f;   // s[n-1]
  float sPrev2 = 0.0f;   // s[n-2]

  for (int i = 0; i < n; i++) {
    float s = (float)x[i] + coeff * sPrev - sPrev2;
    sPrev2 = sPrev;
    sPrev  = s;
  }

  float power = sPrev2 * sPrev2 + sPrev * sPrev - coeff * sPrev * sPrev2;

  // El power es un cuadrado: no puede ser negativo. Un valor negativo sólo
  // puede venir del redondeo de float; se recorta para que sqrt() sea seguro.
  if (power < 0.0f) power = 0.0f;

  powerCrudo = power;
  return sqrt(power);
}

// ============================================================================
//  DETECCIÓN Y CLASIFICACIÓN
// ============================================================================

// Clasifica una duración de tono en punto (.) o raya (-).
char clasificar(unsigned long duracionMs) {
  return (duracionMs < (unsigned long)PUNTO_MAX_MS) ? '.' : '-';
}

// Aplica la histéresis sobre 'amp' y devuelve el flanco detectado, si hubo.
//
// Las marcas de tiempo se toman en el PRIMER bloque que cruza el umbral, no
// en el que confirma el cambio. Así, con BLOQUES_ON = 2, la duración medida
// del tono no incluye los 100 ms de confirmación.
Evento actualizarEstado(float amp, unsigned long ahoraMs) {
  Evento ev = { EVENTO_NINGUNO, 0 };

  if (!enTono) {
    if (amp >= UMBRAL_TONO) {
      if (contadorOn == 0) inicioCandidatoMs = ahoraMs;  // flanco de subida real
      contadorOn++;
      if (contadorOn >= BLOQUES_ON) {
        enTono        = true;
        inicioTonoMs  = inicioCandidatoMs;
        contadorOn    = 0;
        contadorOff   = 0;
        ev.tipo       = EVENTO_INICIO;
      }
    } else {
      contadorOn = 0;
    }
  } else {
    if (amp < UMBRAL_TONO) {
      if (contadorOff == 0) finCandidatoMs = ahoraMs;    // flanco de bajada real
      contadorOff++;
      if (contadorOff >= BLOQUES_OFF) {
        enTono      = false;
        contadorOn  = 0;
        contadorOff = 0;
        ev.tipo     = EVENTO_FIN;
        // Aritmética de unsigned long: correcta incluso si millis() dio la vuelta.
        ev.duracionMs = finCandidatoMs - inicioTonoMs;
      }
    } else {
      contadorOff = 0;
    }
  }

  return ev;
}

// ============================================================================
//  SALIDA POR SERIAL
// ============================================================================

// Línea compacta y parseable por bloque:
//   B0123 dc= 512 amp=  3.4 max= 520 silencio
void imprimirBloque(unsigned long numero, float dc, float amp, int maxAbs,
                    bool tono, float magnitud, float power) {
  Serial.printf("B%04lu dc=%4.0f amp=%5.1f max=%4d %s",
                numero, dc, amp, maxAbs, tono ? "TONO" : "silencio");

#if MODO_CRUDO
  Serial.printf(" mag=%8.1f pow=%11.1f", magnitud, power);
#endif

  Serial.println();
}

// Resumen min/max/promedio de una categoría (silencio o tono).
void imprimirResumen(const char *etiqueta, const Estadisticas &e) {
  if (e.n == 0) {
    Serial.printf("%s: sin datos (n=0)", etiqueta);
  } else {
    Serial.printf("%s: min=%.1f max=%.1f avg=%.1f (n=%u)",
                  etiqueta, e.minAmp, e.maxAmp, e.suma / (float)e.n, e.n);
  }
}

// Línea de calibración cada PERIODO_STATS_MS.
void imprimirEstadisticas() {
  Serial.print("STATS ");
  imprimirResumen("silencio", statsSilencio);
  Serial.print(" | ");
  imprimirResumen("tono", statsTono);
  Serial.printf(" | atrasos=%lu\n", totalSobrepasos);

  // Reiniciar la ventana de estadísticas.
  statsSilencio = { 0, 0.0f, 0.0f, 0.0f };
  statsTono     = { 0, 0.0f, 0.0f, 0.0f };
}

// Acumula una medición de amp en la categoría correspondiente al estado actual.
void acumularEstadistica(Estadisticas &e, float amp) {
  if (e.n == 0) {
    e.minAmp = amp;
    e.maxAmp = amp;
  } else {
    if (amp < e.minAmp) e.minAmp = amp;
    if (amp > e.maxAmp) e.maxAmp = amp;
  }
  e.suma += amp;
  e.n++;
}

// ============================================================================
//  SETUP
// ============================================================================

void setup() {
  Serial.begin(115200);

  // Único delay() del sketch: le da tiempo al monitor serie a abrirse.
  // No afecta al muestreo porque todavía no empezó.
  delay(300);

  imprimirBanner();

  ultimoStatsMs = millis();
}

// ============================================================================
//  LOOP
// ============================================================================

void loop() {
  unsigned long ahoraMs = millis();

  // 1) Adquirir un bloque completo (100 ms) a Fs exacta.
  float dcMedia = 0.0f;
  int   maxAbs  = 0;
  leerBloque(muestras, N_MUESTRAS, dcMedia, maxAbs);

  // 2) Centrar en cero (obligatorio: el módulo entrega offset de continua).
  restarMedia(muestras, N_MUESTRAS, dcMedia);

  // 3) Goertzel sobre el bin del tono.
  float powerCrudo = 0.0f;
  float magnitud   = goertzel(muestras, N_MUESTRAS, K_BIN, powerCrudo);

  // 4) Amplitud estimada en cuentas de ADC (número principal del umbral).
  float amp = 2.0f * magnitud / (float)N_MUESTRAS;

  // 5) Histéresis temporal -> TONO / SILENCIO y flancos.
  Evento ev = actualizarEstado(amp, ahoraMs);

  bloqueActual++;

  // 6) Línea por bloque, con el estado ya decidido.
  imprimirBloque(bloqueActual, dcMedia, amp, maxAbs, enTono, magnitud, powerCrudo);

  // 7) Flancos: inicio y fin del tono (fin incluye duración y símbolo).
  if (ev.tipo == EVENTO_INICIO) {
    Serial.println(">> TONO inicio");
  } else if (ev.tipo == EVENTO_FIN) {
    Serial.printf("<< TONO fin %lums -> %c\n", ev.duracionMs, clasificar(ev.duracionMs));
  }

  // 8) Aviso de saturación del ADC (sólo al entrar, para no inundar el monitor).
  bool saturadoAhora = (maxAbs >= UMBRAL_SATURACION);
  if (saturadoAhora && !saturadoAntes) {
    Serial.printf("!! SATURACION ADC: max=%d (bajar la ganancia del potenciometro)\n",
                  maxAbs);
  }
  saturadoAntes = saturadoAhora;

  // 9) Estadísticas para calibrar UMBRAL_TONO.
  acumularEstadistica(enTono ? statsTono : statsSilencio, amp);

  if ((millis() - ultimoStatsMs) >= (unsigned long)PERIODO_STATS_MS) {
    ultimoStatsMs = millis();
    imprimirEstadisticas();
  }
}

// ============================================================================
//  COMO PROBAR
// ============================================================================
//
// 1) Hardware: módulo de sonido con salida ANALÓGICA al pin A0 del ESP8266,
//    alimentado como corresponde. A0 acepta 0..1.0 V: si el módulo entrega más
//    (por ejemplo 1.65 V de offset), hay que atenuar con el potenciómetro o un
//    divisor resistivo. Nunca conectar 5 V a A0.
//
// 2) Cargar el sketch y abrir el Monitor Serie a 115200 baudios. Verificar el
//    banner: Fs = 4000, N = 400, k = 70, umbral = 12.0. Confirmar que no
//    aparezca el AVISO de k fuera de rango.
//
// 3) Con el ambiente en silencio, observar unos segundos las líneas B: el
//    valor 'amp' es el ruido de fondo del bin. Anotar el máximo (usar la línea
//    STATS: "silencio: min/max/avg").
//
// 4) Reproducir 700 Hz desde el celular (app generadora de tonos o video de
//    YouTube) apuntando al micrófono. Subir el volumen y girar el potenciómetro
//    del módulo hasta que 'amp' en TONO sea al menos ~5 veces el ruido de fondo
//    y 'max' quede por debajo de 1000 (sin saturar).
//
// 5) Anotar "tono: min/max/avg" de la línea STATS. Fijar UMBRAL_TONO en el
//    punto medio entre el máximo de silencio y el mínimo de tono, por ejemplo:
//    silencio max = 4, tono min = 40  =>  UMBRAL_TONO ~ 20.
//
// 6) Verificar que al cortar el tono el estado vuelva a "silencio" sin titilar
//    y que ">> TONO inicio" / "<< TONO fin ... -> ." aparezcan una sola vez
//    por pulsación. Ajustar BLOQUES_ON / BLOQUES_OFF si hace falta.
//
// 7) Reproducir una secuencia Morse real (por ejemplo "SOS": ... --- ...) y
//    comprobar que la clasificación punto/raya coincide con la esperada.
//    Si las rayas se cortan en puntos, subir PUNTO_MAX_MS; si los puntos se
//    alargan a rayas, bajarlo.
//
// 8) Opcional: poner MODO_CRUDO en 1 para ver la magnitud y el power crudos y
//    entender cómo se relacionan con 'amp' (amp = 2*mag/N).
//
// 9) Recién con UMBRAL_TONO y PUNTO_MAX_MS validados en la placa, trasladar
//    los valores al firmware principal.
//
// ============================================================================
