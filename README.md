# EnaBot audio button — PoC

Boton -> reproduce un audio corto guardado en la flash del ESP32-S3, subido
previamente mediante un portal cautivo por WiFi.

## Hardware

- ESP32-S3 Super Mini
- MCP4725 (DAC I2C 12 bit)
- Pulsador
- Recomendado para escuchar algo: filtro RC pasivo a la salida del MCP4725 +
  amplificador (ej. PAM8403) + parlante. El MCP4725 no tiene potencia de
  salida ni filtrado, solo entrega un voltaje analogico escalonado.

## Conexionado

| MCP4725 | ESP32-S3 Super Mini |
|---|---|
| VCC | 3V3 |
| GND | GND |
| SDA | GPIO8 |
| SCL | GPIO9 |
| OUT | -> filtro RC -> entrada del amplificador |

| Boton | ESP32-S3 Super Mini |
|---|---|
| Pin 1 | GPIO4 |
| Pin 2 | GND |

Los pines estan centralizados en [`include/config.h`](include/config.h) —
si tu placa tiene otra distribucion, cambialos ahi.

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
3. Elegí cualquier grabación de voz del celular (m4a de Voice Memos en
   iPhone, mp3, wav, lo que sea) de hasta 20 segundos. La propia página la
   convierte a WAV mono 16 kHz **en el navegador** (Web Audio API decodifica
   el archivo original y lo re-encodea) antes de subirla — el ESP32 nunca
   tiene que lidiar con decodificar AAC/MP3.
4. Debajo del formulario hay un reproductor (`<audio controls>`) para
   escuchar en el celular lo que quedó guardado, sin tener que ir hasta el
   botón físico.
5. Apretá el boton: reproduce el ultimo archivo subido.

Podés volver a entrar al portal en cualquier momento para reemplazar el
audio (pisa el anterior).

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

- Particion de datos (LittleFS): ~1.9 MB de una flash de 4 MB — un WAV de
  20s mono a 16 kHz/16 bit pesa ~625 KB, entra holgado.
- El limite real no es la memoria sino la velocidad del bus I2C hacia el
  DAC: por eso `AUDIO_MAX_SAMPLE_RATE` en `config.h` rechaza WAVs con
  sample rate mayor a 16 kHz al subirlos (a esa tasa el "fast mode write"
  del MCP4725 se sostiene sin problema; mas alto empieza a arriesgar
  cortes de timing). Si en la practica notás que el audio suena mas
  lento/grave de lo que grabaste, es sintoma de que el I2C no llega a
  16kHz en tu cableado particular — bajá `AUDIO_CLIENT_SAMPLE_RATE` a 8000
  en `config.h`, o probá subir `I2C_CLOCK_HZ` (400kHz hoy, dentro de spec
  del MCP4725; 1MHz es "Fast Mode Plus" y podria dar problemas de señal en
  protoboard, probarlo con cuidado).
- Para audio con mejor calidad a futuro, la alternativa seria un DAC I2S
  (ej. PCM5102) en vez del MCP4725 por I2C — pero para esta prueba de
  concepto no hace falta.
- **Bug de watchdog corregido:** reproducir de forma continua sin ceder
  CPU (necesario para el timing exacto de las muestras) hace que la tarea
  idle del core 1 nunca corra, y el Task Watchdog de ESP-IDF resetea la
  placa a mitad de la reproduccion (~5s por defecto). `AudioPlayer.cpp`
  cede el CPU 1 tick cada ~200ms de audio para evitarlo (perdida real
  imperceptible, ~0.5% de tiempo total).

## Estructura

```
include/config.h       pines, credenciales del AP, limites
src/MCP4725Fast.h       driver I2C fast-mode para el DAC
src/AudioPlayer.h/.cpp  parser de WAV + tarea de reproduccion con timing por sample
src/CaptivePortal.h/.cpp AP + DNS wildcard + servidor web + subida de archivo
src/main.cpp            setup/loop, boton con debounce
partitions.csv          tabla de particiones (app 2MB / littlefs ~1.9MB)
```
