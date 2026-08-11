# EnaBot audio button — PoC

Boton -> reproduce un audio corto guardado en la flash del ESP32-S3, subido
previamente mediante un portal cautivo por WiFi.

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

1. Al bootear, el ESP32 crea una red WiFi abierta llamada `EnaBot-Setup`.
2. Conectate desde el celular/PC. Debería aparecer el popup de "portal
   cautivo" solo; si no, abrí `http://192.168.4.1/` a mano.
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
   un audio completo por apretada, en secuencia. Si algún slot esta vacio o
   con un archivo invalido, se saltea solo (no te deja "mudo" en esa
   apretada).

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

- Particion de datos (LittleFS): ~2.625 MB de una flash de 4 MB (fisicos,
  confirmado con `esptool flash_id` sobre la placa real) — repartidos entre
  los 4 slots (`/audio0.wav`..`/audio3.wav`). Un WAV de 20s mono a 16
  kHz/16 bit pesa ~625 KB, asi que los 4 al maximo ocupan ~2.5 MB, con
  margen para el overhead de LittleFS. `AUDIO_MAX_BYTES` en `config.h`
  limita cada slot individualmente a 700 KB para que un archivo grande no
  se coma el espacio de los otros tres.
- La particion de la app se redujo a 1.25 MB para hacerle lugar a esto
  (el firmware ocupa ~1 MB, sigue con margen). Si agregás mucho codigo
  nuevo y no compila por espacio, hay que rebalancear `partitions.csv`.
- El limite de sample rate (16 kHz) ya no viene de un cuello de botella de
  hardware como con el MCP4725 por I2C — el I2S por DMA aguanta mas sin
  problema — sino de que la libreria `I2S` de Arduino-ESP32 solo da
  soporte oficial hasta 16kHz/16bit en `I2S_PHILIPS_MODE` (a mas, avisa
  por serial que puede sonar con ruido). Se podria probar mas alto subiendo
  `AUDIO_CLIENT_SAMPLE_RATE`/`AUDIO_MAX_SAMPLE_RATE`, pero eso tambien
  agranda cada clip — revisar el presupuesto de LittleFS de arriba antes.
- La reproduccion via I2S usa `write_blocking()`, que espera con
  primitivas reales de FreeRTOS (no espera activa) hasta que hay lugar en
  el buffer DMA — a diferencia del MCP4725 por I2C, esto **no necesita**
  ningun truco para evitar el Task Watchdog: al bloquear "de verdad", la
  tarea idle corre sola sin que haya que cederle CPU a mano.

## Estructura

```
include/config.h        pines, credenciales del AP, cantidad de slots, limites
lib/I2S/                copia local de la libreria I2S del core (ver nota abajo)
src/AudioPlayer.h/.cpp  parser de WAV + tarea de reproduccion por I2S (write_blocking)
src/CaptivePortal.h/.cpp AP + DNS wildcard + servidor web + subida por slot (/upload/N, /audio/N)
src/main.cpp            setup/loop, boton con debounce + secuencia de slots
partitions.csv          tabla de particiones (app 1.25MB / littlefs ~2.625MB)
```

**Nota sobre `lib/I2S/`:** es una copia de la libreria `I2S` que trae
Arduino-ESP32 (`framework-arduinoespressif32/libraries/I2S`). Su
`library.properties` declara `architectures=esp32`, y el Library
Dependency Finder de PlatformIO la filtra para el target `esp32s3` con esa
declaracion (aunque el chip la soporta perfectamente) — vendorearla en
`lib/` evita ese filtro. Si en el futuro PlatformIO/el core arreglan esto,
se puede borrar `lib/I2S/` y usar la del framework directamente.
