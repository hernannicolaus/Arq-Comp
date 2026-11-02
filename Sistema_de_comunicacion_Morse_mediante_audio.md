# Sistema de comunicación Morse mediante audio

## Escenario

En un pequeño pueblo ubicado en una zona aislada, las comunicaciones convencionales pueden verse afectadas por cortes de energía, falta de cobertura celular o ausencia de infraestructura de Internet. Como alternativa, se propone implementar un sistema de comunicación mediante señales acústicas utilizando código Morse.

El sistema deberá utilizar un ESP8266 para recibir mediante un micrófono los tonos generados por otro dispositivo, identificar puntos, rayas y pausas mediante el análisis de la señal y decodificar el mensaje recibido.

Para la detección de la frecuencia del tono se utilizará el algoritmo de Goertzel. El mensaje obtenido deberá visualizarse en un display y, mediante la conexión Wi-Fi del ESP8266, también deberá estar disponible en una interfaz web.

La comunicación Morse deberá realizarse exclusivamente mediante sonido. El Wi-Fi será utilizado únicamente para visualizar en la web el mensaje que el ESP8266 haya recibido y decodificado.

## Objetivo del trabajo

Diseñar e implementar un sistema capaz de:

- Recibir señales acústicas correspondientes a código Morse.
- Detectar la frecuencia del tono recibido.
- Diferenciar entre puntos, rayas y pausas.
- Decodificar el código Morse y obtener el mensaje original.
- Mostrar el mensaje recibido en un display.
- Publicar el mensaje decodificado mediante una interfaz web utilizando el ESP8266.
- Transmitir código Morse mediante un buzzer para permitir una comunicación bidireccional.

## Cadena de Procesamiento del Sistema

Se desea desarrollar un sistema de comunicación que permita intercambiar mensajes mediante señales acústicas.

El dispositivo principal estará construido alrededor de un ESP8266.

El sistema deberá poder recibir un mensaje Morse generado externamente. Para ello, podrá utilizarse un teléfono celular que reproduzca los tonos correspondientes al código Morse.

Por ejemplo, si desde el teléfono se reproduce:

```text
.... --- .-.. .-
```

correspondiente a:

```text
HOLA
```

El microcontrolador ESP8266 deberá:

1. Capturar el sonido mediante el micrófono.
2. Analizar la señal de audio.
3. Detectar la presencia del tono.
4. Medir la duración del tono.
5. Determinar si corresponde a un punto o una raya.
6. Detectar las pausas entre símbolos, letras y palabras.
7. Reconstruir el código Morse.
8. Decodificar el mensaje.
9. Mostrar el resultado en el display.
10. Mostrar el mensaje en una página web.

El sistema deberá funcionar de manera similar en sentido inverso: el usuario podrá ingresar código Morse mediante un botón o llave Morse y el ESP8266 deberá generar los tonos mediante un buzzer, permitiendo que otro dispositivo, como un teléfono celular, pueda recibir y decodificar el mensaje.

## Requisitos

La información deberá transmitirse mediante **SONIDO**. El Wi-Fi del ESP8266 deberá utilizarse exclusivamente para visualizar los resultados del procesamiento en una interfaz web.

Por lo tanto, la cadena principal del sistema deberá ser:

```text
SONIDO
  ↓
MICRÓFONO
  ↓
ESP8266
  ↓
PROCESAMIENTO DIGITAL
  ↓
GOERTZEL
  ↓
PUNTO / RAYA / PAUSA
  ↓
DECODIFICADOR MORSE
  ↓
MENSAJE
  ↓
DISPLAY + INTERFAZ WEB
```

## Implementación de Hardware

El sistema deberá utilizar los siguientes componentes:

| Componente | Función |
|---|---|
| ESP8266 | Procesamiento de señales y comunicación Wi-Fi |
| Micrófono con salida analógica | Captura de la señal acústica |
| Display | Visualización del código Morse y/o mensaje |
| Buzzer | Generación del tono Morse |
| Pulsador o llave Morse | Ingreso manual de código Morse |

El uso de un LED indicador será opcional.

### Nota técnica / Sugerencia de componentes

- Para el módulo de micrófono, se puede utilizar el módulo Grove - Sound Sensor (o equivalente con salida analógica).
- Tener en cuenta que la salida del sensor entrega una componente continua (offset DC), por lo que en el software se debe restar la tensión media antes de aplicar el algoritmo de Goertzel.
- Se recomienda calibrar la ganancia mediante el potenciómetro del sensor para evitar saturar la entrada A0 del ESP8266.

## Procesamiento de la señal

Una de las partes principales del trabajo será el procesamiento digital de la señal (DSP).

Se deberá seleccionar una frecuencia de trabajo para el tono Morse. Por ejemplo: **Frecuencia del tono = 700 Hz**. El micrófono captará la señal y el ESP8266 deberá analizarla mediante el algoritmo de Goertzel. El algoritmo deberá utilizarse para determinar la presencia de energía en la frecuencia seleccionada.

Se espera que conceptualmente:

```text
SEÑAL DEL MICRÓFONO
  ↓
ADC
  ↓
MUESTRAS
  ↓
GOERTZEL
  ↓
¿EXISTE EL TONO?
  ├── SÍ → TONO
  └── NO → SILENCIO
```

No será necesario implementar una FFT completa para detectar la frecuencia seleccionada.

## Identificación de puntos y rayas

Una vez detectada la presencia o ausencia del tono, se deberá realizar un análisis temporal.

El sistema deberá determinar la duración de cada tono.

| Elemento Morse | Tipo de Estado |
|---|---|
| Punto (.) | TONO |
| Raya (-) | TONO |
| Pausa entre símbolos | SILENCIO |
| Pausa entre letras | SILENCIO |
| Pausa entre palabras | SILENCIO |

Los valores podrán modificarse durante las pruebas para mejorar la robustez del sistema.

## Interfaces de Salida

### Visualización mediante display

El dispositivo deberá mostrar información correspondiente al mensaje recibido.

Como mínimo, deberá poder visualizarse el mensaje decodificado.

Por ejemplo:

| Morse | Mensaje |
|---|---|
| `.... --- .-.. .-` | Hola |

Durante la recepción también podrá mostrarse información del estado del sistema:

| Estado |
|---|
| Recibido |
| Tono detectado |

### Interfaz web

El ESP8266 deberá implementar un servidor web accesible desde un navegador conectado a la misma red.

La información deberá actualizarse a medida que se procesa el mensaje.

- **Requisitos Mínimos:**
  - Estado actual del sistema y de la conexión.
  - Cadena de símbolos Morse en recepción actual.
  - Texto decodificado resultante.
- **Extensiones Sugeridas:**
  - Nivel de señal y energía calculada por Goertzel.
  - Indicador instantáneo binario (TONO / SILENCIO).
  - Duración medida del tono en milisegundos.
  - Historial de mensajes enviados y recibidos.
  - Gráficas en tiempo real de la señal del micrófono y salida del algoritmo.

## Comunicación bidireccional

El sistema deberá contemplar la posibilidad de realizar comunicación en ambos sentidos.

### Sentido 1: teléfono → ESP8266

```text
TELÉFONO
  ↓
AUDIO MORSE
  ↓
MICRÓFONO
  ↓
ESP8266
  ↓
GOERTZEL
  ↓
DECODIFICADOR
  ↓
DISPLAY + WEB
  ↓
HOLA MENSAJE
```

### Sentido 2: ESP8266 → teléfono

```text
USUARIO
  ↓
LLAVE MORSE / BOTÓN
  ↓
ESP8266
  ↓
BUZZER
  ↓
AUDIO MORSE
  ↓
TELÉFONO
```

Para el segundo sentido, el teléfono podrá utilizar una aplicación o software capaz de recibir y decodificar los tonos Morse.

## Actividades

El/Los estudiante/s deberán:

1. Diseñar el circuito electrónico.
2. Implementar el programa en Arduino IDE.
3. Configurar el ESP8266 para conectarse a una red WiFi.
4. Crear una página web alojada en el ESP8266.
5. Integrar sensores, actuadores y la interfaz web.
6. Verificar el funcionamiento del sistema.

## Entregable

El alumno deberá presentar:

1. Repositorio de código público o privado (GitHub / GitLab) que contenga la estructura completa del proyecto Diagrama de conexión.
2. Esquema de conexión.
3. Capturas de la interfaz web funcionando.
4. Presentación del Poster.
5. Demostración práctica mostrando el sistema IoT operando, la actualización de datos en la pantalla LCD (en caso de que lo requiera), el comportamiento de la interfaz web y la activación de alertas/actuadores ante condiciones solicitadas.
