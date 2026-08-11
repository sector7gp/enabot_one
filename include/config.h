#pragma once

// --- I2S hacia el MAX98357A (ampli Class-D con DAC I2S integrado) ---
// Elegidos para evitar pines de strapping del S3 (0, 3, 45, 46) y el rango
// reservado para el flash/PSRAM embebido (aprox. GPIO26-37 en esta placa).
#define I2S_BCLK_PIN 5  // BCLK del MAX98357A
#define I2S_LRC_PIN 6   // LRC / WS del MAX98357A
#define I2S_DOUT_PIN 7  // DIN del MAX98357A

// --- Boton ---
// Activo en bajo: un extremo al pin, el otro a GND. Usa pull-up interno.
// Evitar pines de strapping del S3 (0, 3, 45, 46).
#define BUTTON_PIN 4
#define BUTTON_DEBOUNCE_MS 300

// --- Portal cautivo ---
#define AP_SSID "EnaBot-Setup"
#define AP_PASSWORD "" // red abierta; poner clave de 8+ caracteres si se desea

// --- Almacenamiento del audio ---
// 4 slots independientes ("/audio0.wav" .. "/audio3.wav"), cada uno con el
// mismo limite de tamano/duracion. El boton los reproduce en secuencia.
#define AUDIO_NUM_SLOTS 4
// Tope de seguridad por archivo: a 16kHz/16bit/20s un clip pesa ~640KB, asi
// que 700KB da margen sin permitir que un solo slot devore la particion
// entera (littlefs tiene ~2.6MB repartidos entre los 4 slots).
#define AUDIO_MAX_BYTES (700UL * 1024UL)
// Limite de sample rate aceptado al subir. La libreria I2S de Arduino-ESP32
// solo da soporte oficial hasta 16kHz/16bit en I2S_PHILIPS_MODE (a mas,
// avisa que el audio puede sonar con ruido) — nos quedamos ahi.
#define AUDIO_MAX_SAMPLE_RATE 16000

// El navegador convierte cualquier audio (m4a, mp3, wav...) a WAV mono PCM
// con estos parametros antes de subirlo, asi el ESP32 nunca tiene que
// lidiar con decodificar un codec. 16kHz es voz "wideband" (bastante mas
// clara que los 8kHz de telefono).
#define AUDIO_CLIENT_SAMPLE_RATE 16000
#define AUDIO_CLIENT_MAX_SECONDS 20

#define AUDIO_STR_(x) #x
#define AUDIO_STR(x) AUDIO_STR_(x)
