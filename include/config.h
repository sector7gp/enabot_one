#pragma once

// --- I2C hacia el MCP4725 ---
// Pines por defecto de Wire en la mayoria de placas ESP32-S3 Super Mini.
// Verificar contra el silkscreen de tu placa si no funciona.
#define I2C_SDA_PIN 8
#define I2C_SCL_PIN 9
#define I2C_CLOCK_HZ 400000

// 0x60 si el pin ADDR del MCP4725 esta a GND, 0x61 si esta a VCC.
#define MCP4725_ADDR 0x60

// --- Boton ---
// Activo en bajo: un extremo al pin, el otro a GND. Usa pull-up interno.
// Evitar pines de strapping del S3 (0, 3, 45, 46).
#define BUTTON_PIN 4
#define BUTTON_DEBOUNCE_MS 300

// --- Portal cautivo ---
#define AP_SSID "EnaBot-Setup"
#define AP_PASSWORD "" // red abierta; poner clave de 8+ caracteres si se desea

// --- Almacenamiento del audio ---
#define AUDIO_FILE_PATH "/audio.wav"
// Tope de seguridad en la subida (la particion littlefs tiene ~1.9MB libres).
#define AUDIO_MAX_BYTES (1500UL * 1024UL)
// Limite de sample rate aceptado: mas alto que esto y el I2C a 400kHz no
// llega a sostener el ritmo de escritura de forma confiable en fast-mode.
#define AUDIO_MAX_SAMPLE_RATE 16000

// El navegador convierte cualquier audio (m4a, mp3, wav...) a WAV mono PCM
// con estos parametros antes de subirlo, asi el ESP32 nunca tiene que
// lidiar con decodificar un codec. 16kHz es voz "wideband" (bastante mas
// clara que los 8kHz de telefono); si en la practica el I2C no llega a
// sostenerlo vas a notar que el audio se reproduce mas lento/grave de lo
// normal (no se corta ni se traba, solo se atrasa) — en ese caso bajar
// a 8000.
#define AUDIO_CLIENT_SAMPLE_RATE 16000
#define AUDIO_CLIENT_MAX_SECONDS 20

#define AUDIO_STR_(x) #x
#define AUDIO_STR(x) AUDIO_STR_(x)
