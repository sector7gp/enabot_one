# EnaBot audio button — PoC

Un boton fisico reproduce, uno por apretada y en secuencia, 4 audios cortos
guardados en la flash del ESP32-S3. Los audios se cargan desde el celular a
traves de un portal cautivo por WiFi, en cualquier formato (m4a, mp3, wav):
el propio navegador los convierte a WAV mono antes de subirlos.

## Hardware

- ESP32-S3 Super Mini
- MAX98357A (ampli Class-D con DAC I2S integrado — no es un DAC I2C como el
  MCP4725 que se probo al principio; recibe audio digital por I2S y ya
  entrega la señal amplificada, no hace falta ampli ni filtro externo)
- Parlante (segun el breakout del MAX98357A, tipicamente 4-8 Ω)
- Pulsador

## Conexionado

| MAX98357A | ESP32-S3 Super Mini |
|---|---|
| VIN | 5V (o 3.3V si el breakout lo soporta — revisar su datasheet) |
| GND | GND |
| BCLK | GPIO5 |
| LRC | GPIO6 |
| DIN | GPIO7 |
| SD (si esta expuesto) | ver nota abajo |

| Boton | ESP32-S3 Super Mini |
|---|---|
| Pin 1 | GPIO4 |
| Pin 2 | GND |

Los pines estan centralizados en [`include/config.h`](include/config.h) —
si tu placa tiene otra distribucion, cambialos ahi. Se eligieron GPIO5/6/7
por estar libres de: pines de strapping del S3 (0, 3, 45, 46) y el rango
usado internamente por el flash/PSRAM embebidos de esta placa
(aproximadamente GPIO26-37, confirmado que esta placa tiene PSRAM via
`esptool flash_id`) — evitalos si cambias los pines.

**Nota sobre el pin SD/MODE del MAX98357A:** segun como este cableado (a
GND, a VCC, flotando, o con un divisor resistivo) el chip selecciona si
reproduce el canal Left, Right, o la mezcla (L+R)/2. El firmware manda el
mismo valor mono en ambos canales I2S, asi que suena bien sin importar cual
de esos modos tenga tu breakout en particular — no hace falta averiguarlo.

## Compilar y flashear

```bash
pio run --target upload
pio device monitor
```

Si no ves nada por el monitor serie: las Super Mini no tienen chip
USB-UART, usan el USB nativo del S3. Ya esta resuelto en
`platformio.ini` (`ARDUINO_USB_CDC_ON_BOOT`), pero si conectaste por el
puerto UART0 en vez del USB-C vas a necesitar un adaptador USB-serie aparte.

## Uso

1. **Reiniciá la placa y apretá el botón dentro de los primeros 5 segundos**
   (`PORTAL_TRIGGER_WINDOW_MS`); también sirve arrancar con el botón ya
   apretado. Un **pitido corto** confirma que entró en modo configuración y
   que se creó la red WiFi abierta `EnaBot-Setup`. Si no hay pitido, no
   entró: reintentá. En un arranque normal (sin botón) la radio no se
   enciende nunca: solo botón → audio.
2. Conectate desde el celular/PC. Debería aparecer el popup de "portal
   cautivo" solo; si no, abrí `http://enabot.local/` (o `http://192.168.4.1/`)
   a mano.
   El portal **se apaga solo a los 10 minutos** (`PORTAL_TIMEOUT_MS` en
   `config.h`) y apaga la radio con él. Para volver a entrar hay que
   reiniciar de nuevo y apretar el botón.
   El volumen de salida se ajusta con `AUDIO_VOLUME_PERCENT` (50% por
   defecto).
3. El portal muestra **4 audios independientes** ("Audio 1".."Audio 4"),
   cada uno con su propio formulario. Elegí cualquier grabación de voz del
   celular (m4a de Voice Memos en iPhone, mp3, wav, lo que sea) de hasta 20
   segundos por audio. La propia página la convierte a WAV mono 16 kHz **en
   el navegador** (Web Audio API decodifica el archivo original y lo
   re-encodea) antes de subirla — el ESP32 nunca tiene que lidiar con
   decodificar AAC/MP3.
4. Cada audio tiene su propio reproductor (`<audio controls>`) para
   escucharlo en el celular sin ir hasta el botón físico.
5. Apretá el botón: reproduce **Audio 1**. La próxima apretada reproduce
   **Audio 2**, despues **Audio 3**, **Audio 4**, y vuelve a **Audio 1** —
   uno por apretada, en secuencia. Si algún slot esta vacio o con un archivo
   invalido, se saltea solo (no te deja "mudo" en esa apretada).
   Si apretás mientras todavía suena algo, **corta la reproducción en curso
   y pasa al siguiente** en el acto (~16 ms), en vez de ignorar la apretada.

Podés volver a entrar al portal en cualquier momento para reemplazar
cualquiera de los 4 (pisa el anterior de ese slot, los otros tres quedan
igual).

Si el selector de archivos del celular no te deja elegir el `.m4a`: el
input ya pide explícitamente `audio/*, video/mp4, audio/mp4, .m4a, ...`
porque algunos pickers (sobre todo iOS) clasifican el m4a de Voice Memos
como si fuera un video por el contenedor MP4 que comparten. Si igual no
aparece, probá exportarlo/compartirlo primero a la app Archivos.

**Nota:** esto usa `decodeAudioData`/`OfflineAudioContext` (Web Audio API),
que funcionan sobre HTTP plano sin restricciones — a diferencia de grabar
directo con el microfono del navegador (`getUserMedia`), que exige HTTPS y
por eso no lo implementamos: hubiera requerido servir un certificado
autofirmado desde el ESP32 con toda la friccion que eso genera en el
celular. Grabar con la app nativa del telefono y subir el archivo evita
todo ese problema.

## Sobre los limites de memoria (resumen de la PoC)

- Flash total: 4 MB fisicos (confirmado con `esptool flash_id` sobre la
  placa real). Con OTA hay que repartirlo entre **dos** particiones de app
  de 1.1875 MB c/u y el filesystem.
- Particion de datos (LittleFS): **1.5 MB** para los 4 slots
  (`/audio0.wav`..`/audio3.wav`). Antes de OTA eran 2.625 MB.
- **Esto acota la duracion total.** Un WAV de 20s mono a 16 kHz/16 bit pesa
  ~640 KB, asi que ya no entran 4 clips de 20s (serian ~2.5 MB). Si los 4
  slots se usan parejos, el techo practico es de ~11s cada uno. Se pueden
  combinar: uno largo y tres cortos entra sin problema. El limite sigue
  siendo por archivo (`AUDIO_MAX_BYTES`, 700 KB) y ademas el total real de
  la particion.
- El firmware con OTA+mDNS ocupa ~1.01 MB de los 1.1875 MB de la particion
  de app (85%). Si se agrega mucho codigo y no entra, hay que rebalancear
  `partitions.csv` — sacandole al filesystem.
- **Cambiar `partitions.csv` borra los audios cargados**, porque se mueve el
  offset del filesystem. Conviene bajarlos antes desde el portal
  (`/audio/0`..`/audio/3`) y volver a subirlos despues.
- Los 16 kHz de `AUDIO_CLIENT_SAMPLE_RATE` no son un techo del hardware: el
  I2S por DMA aguanta bastante mas. Es un compromiso entre calidad y
  espacio (a 16 kHz/16 bit cada segundo son 32 KB). Si lo subis, revisa
  antes el presupuesto de LittleFS de arriba. Cada slot se reproduce al
  sample rate que diga su propio header WAV, asi que convivir con clips
  viejos a otra tasa no es problema.
- La reproduccion usa `i2s_channel_write()` bloqueante, que espera con
  primitivas reales de FreeRTOS (no espera activa) hasta que hay lugar en
  el buffer DMA. Por eso **no hace falta** ningun truco para evitar el Task
  Watchdog: al bloquear "de verdad", la tarea idle corre sola sin que haya
  que cederle CPU a mano (el MCP4725 por I2C, que se probo primero, si lo
  necesitaba porque hacia espera activa muestra por muestra).

## Decisiones de implementacion del audio

- **Se usa el driver nativo del IDF (`driver/i2s_std.h`), no la libreria
  `I2S` de Arduino.** Esa libreria es un wrapper del driver *legacy*
  (deprecado, tira warning al compilar) y ademas declara
  `architectures=esp32` en su manifest, por lo que PlatformIO la filtra
  para el target `esp32s3`. Se probo forzarla vendoreando una copia en
  `lib/I2S/` y funcionaba a medias, pero el filtro tenia razon: no esta
  validada para el S3. El driver nativo no necesita ninguna dependencia
  extra porque ya viene en el framework.
- **El canal I2S se habilita solo mientras dura la reproduccion**
  (`i2sOutStart()` / `i2sOutStop()`). En reposo no salen BCLK ni LRC, con
  lo cual el MAX98357A queda sin actividad: no consume de mas ni mete el
  ruido de fondo que hacia cuando el clock corria permanentemente.
- Las muestras mono se escriben duplicadas en L y R, asi suena bien sin
  importar como este configurado el pin SD/MODE del breakout (ver nota mas
  arriba).
- Los `i2s_std_*` del SDK traen macros de configuracion
  (`I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG`, etc.) que **no se pueden usar
  desde C++**: listan los designadores en distinto orden que la
  declaracion del struct, lo que en C es legal pero en C++ es error de
  compilacion. Por eso `I2sOut.cpp` asigna los campos de a uno, con los
  mismos valores que usaria el macro para ESP32-S3.

## mDNS y actualizacion por WiFi (OTA)

Ambos **solo existen mientras el portal esta levantado**, asi que el modo
normal (sin radio) no paga nada por tenerlos.

**mDNS:** el portal responde tambien en `http://enabot.local/`, sin tener
que acordarse de la IP. El hostname sale de `MDNS_HOSTNAME` en `config.h`.

**OTA:** se puede actualizar el firmware sin cable. Hay que levantar el
portal (reiniciar apretando el boton), conectarse a `EnaBot-Setup` y:

```bash
pio run -e esp32-s3-supermini-ota -t upload
```

Detalles que importan:

- **OTA obliga a tener dos particiones de app.** La imagen nueva se escribe
  en la que no esta corriendo y recien al final se cambia el puntero de
  arranque; por eso no se puede "actualizar sobre si misma". Eso cuesta
  ~1.19MB de flash, que salieron del espacio de audio: la particion de
  datos paso de 2.625MB a **1.5MB** (ver mas abajo).
- **El timeout del portal no corta un OTA en curso**
  (`captivePortalOtaInProgress()`). Cortar el WiFi a mitad de un flasheo
  dejaria una imagen incompleta.
- **Sin clave por defecto** (`OTA_PASSWORD` vacio en `config.h`). La red es
  abierta, asi que mientras el portal este activo cualquiera con alcance
  podria flashear la placa. Hoy lo que protege es que el portal exige
  apretar el boton fisico y se apaga a los 10 minutos. Para algo que vaya a
  un lugar publico, poner una clave ahi y agregar
  `upload_flags = --auth=...` al entorno de OTA.

## Consumo y bateria

Tres cosas mantienen al equipo lo mas quieto posible cuando no se lo usa:

- **La radio WiFi no se enciende en el arranque normal.** Es de lejos lo
  que mas consume; solo se levanta si se arranco con el boton apretado, y
  aun asi se apaga sola a los 10 minutos (`captivePortalEnd()` baja el
  servidor, el DNS y deja la radio en `WIFI_OFF`).
- **El canal I2S esta apagado salvo mientras suena un audio**, asi que el
  MAX98357A no queda consumiendo ni metiendo ruido entre reproduccion y
  reproduccion.
- **Los logs por serial estan compilados afuera** (ver abajo), y el `loop()`
  cede CPU en vez de girar en vacio cuando no hay portal que atender.

Si hiciera falta estirar mas la bateria, el proximo paso seria light sleep
con wakeup por GPIO en el boton, pero eso ya es otro nivel de cambio.

## Diagnostico por serial

Todo el logging esta detras de `ENABLE_DEBUG_LOG` en
[`include/config.h`](include/config.h), **en 0 por defecto**: con el flag
apagado no se compila ni una llamada a `Serial`.

Esto no es solo prolijidad. La placa usa el USB nativo del S3 como puerto
serie, y cuando no hay ningun terminal conectado del otro lado cada
escritura se queda esperando su timeout antes de descartar el dato — o sea
que en uso normal (a bateria, sin PC) los logs cuestan tiempo de CPU real.

Para reactivarlo, poner el flag en 1 o compilar con
`-DENABLE_DEBUG_LOG=1` (ambas variantes se verificaron que compilan). Con
el flag encendido, al arrancar se imprime:

- **Causa del ultimo reset** (`POWERON`, `PANIC`, `BROWNOUT`, ...). Si
  aparece `BROWNOUT`, el problema es la alimentacion, no el firmware.
- **Volcado de LittleFS**: espacio total/usado/libre y el tamaño de cada
  slot. Sirve para confirmar de un vistazo si los 4 audios estan
  realmente guardados.
- Si el portal arranco o no, y por que.
- Durante la reproduccion, el slot elegido; y si un WAV es rechazado, que
  validacion exactamente fallo (formato, canales, bits, sample rate...).

## Si el WiFi/portal deja de aparecer

Sintoma: `WiFi.softAP()` devuelve OK y el firmware sigue corriendo normal,
pero **el AP no irradia** y la red no aparece en ningun scan. No es un bug
del portal (se verifico sirviendo la pagina y descargando los 4 WAV
completos por HTTP).

**La causa es el orden de inicializacion.** El AP tiene que levantarse
ANTES de tocar el I2S. Si se inicializa el I2S primero — y peor todavia si
se hace sonar algo por el ampli antes de arrancar el AP — el AP queda
"arriba" segun el driver pero nunca sale al aire. En `setup()` el orden
correcto es:

1. `captivePortalBegin()` (AP arriba)
2. `i2sOutBegin()`
3. recien ahi cualquier sonido (el pitido de confirmacion, por ejemplo)

Esto se rompio una vez al agregar el pitido de confirmacion, que obligaba a
inicializar el I2S antes del AP. Con el orden correcto, el pitido suena
despues del AP y no lo afecta.

Lo que ayudo a aislarlo en su momento fue mover el I2S a **pines al aire**,
sin conectar al ampli: asi el WiFi funcionaba siempre. Eso descarto un bug
del driver y apunto a la interaccion con el ampli/el orden de arranque.

Como precaucion adicional, el AP arranca con la potencia de transmision
bajada (`WIFI_AP_TX_POWER`, 8.5 dBm): al celular que va a estar al lado le
sobra, y reduce el pico de corriente de la radio. Si el alcance quedara
corto, subirla ahi.

Si aun asi no aparece, entonces si revisar la parte electrica: alimentar el
MAX98357A desde **5V** en vez del pin 3V3 del ESP, ponerle un capacitor de
470-1000 uF entre VIN y GND pegado al modulo, y alejar el parlante de la
antena.

## Ruido y disparos falsos del boton

El cable del boton corre al lado de las lineas de I2S (BCLK son ~512 kHz) y
el pull-up interno del ESP es debil (~45 kOhm), asi que la linea es de alta
impedancia y se le inducen glitches. Con una interrupcion por flanco eso se
traducia en **reproducciones disparandose solas, una atras de otra**.

Por eso el boton **se sondea, no se usa `attachInterrupt`**: solo cuenta
como pulsacion si el pin se queda estable en `LOW` durante
`BUTTON_STABLE_MS` (30 ms). Un glitch dura microsegundos y queda
descartado. Si aun asi aparecieran disparos falsos, lo que corresponde es
un pull-up externo de 10 kOhm a 3V3 en la linea del boton y/o un capacitor
de 100 nF a GND.

## Estructura

```
include/config.h         pines, credenciales del AP, cantidad de slots, limites
src/I2sOut.h/.cpp        salida I2S al MAX98357A (driver nativo del IDF)
src/AudioPlayer.h/.cpp   parser de WAV + tarea de reproduccion, con corte por boton
src/CaptivePortal.h/.cpp AP + DNS wildcard + servidor web + subida por slot (/upload/N, /audio/N)
src/main.cpp             setup/loop, boton con debounce + secuencia de slots
partitions.csv           tabla de particiones (app 1.25MB / littlefs ~2.625MB)
```

No hay carpeta `lib/`: el proyecto no necesita ninguna libreria externa,
todo sale del framework de Arduino-ESP32 y del IDF.
