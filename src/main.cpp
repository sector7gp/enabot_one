#include <Arduino.h>
#include <esp_system.h>
#include <LittleFS.h>
#include "config.h"
#include "AudioPlayer.h"
#include "CaptivePortal.h"
#include "I2sOut.h"

static AudioPlayer player;

static volatile bool buttonFlag = false;
static uint32_t lastButtonMs = 0;
static uint8_t nextSlot = 0;

void IRAM_ATTR onButtonPress() {
    buttonFlag = true;
}

// Lista el contenido de LittleFS al arrancar: sirve para ver de un vistazo
// si los 4 slots estan realmente guardados y con que tamano, y cuanto
// espacio libre queda (4 clips de 20s a 16kHz son ~2.5MB y la particion
// tiene ~2.6MB, asi que el margen es finito).
static void dumpFilesystem() {
    size_t total = LittleFS.totalBytes();
    size_t used = LittleFS.usedBytes();
    Serial.printf("LittleFS: total=%u used=%u libre=%u\n", total, used, total - used);

    File root = LittleFS.open("/");
    if (!root) {
        Serial.println("LittleFS: no pude abrir la raiz");
        return;
    }
    File entry = root.openNextFile();
    if (!entry) Serial.println("LittleFS: (vacio, ningun archivo)");
    while (entry) {
        Serial.printf("  %s  %u bytes\n", entry.name(), (unsigned)entry.size());
        entry = root.openNextFile();
    }
    root.close();
}

// Reproduce el siguiente slot en la secuencia (avanza siempre, aunque el
// slot este vacio/invalido) y salta los que no tengan un WAV valido, asi
// un boton no se queda "mudo" si todavia no subiste los 4 audios.
static void playNextSlot() {
    for (uint8_t attempts = 0; attempts < AUDIO_NUM_SLOTS; attempts++) {
        uint8_t slot = nextSlot;
        nextSlot = (nextSlot + 1) % AUDIO_NUM_SLOTS;
        if (player.play(audioSlotPath(slot).c_str())) {
            Serial.printf("Reproduciendo slot %u\n", slot);
            return;
        }
    }
    Serial.println("No hay audios validos cargados en ningun slot.");
}

void setup() {
    Serial.begin(115200);

    // Por que arrancamos: si aparece BROWNOUT (o resets repetidos justo
    // despues de reproducir) el problema es la alimentacion, no el firmware.
    esp_reset_reason_t reason = esp_reset_reason();
    const char *reasonStr = "otro";
    switch (reason) {
        case ESP_RST_POWERON:  reasonStr = "POWERON (arranque normal)"; break;
        case ESP_RST_SW:       reasonStr = "SW (reset por software)"; break;
        case ESP_RST_PANIC:    reasonStr = "PANIC (crash del firmware)"; break;
        case ESP_RST_INT_WDT:  reasonStr = "INT_WDT (watchdog de interrupciones)"; break;
        case ESP_RST_TASK_WDT: reasonStr = "TASK_WDT (watchdog de tareas)"; break;
        case ESP_RST_WDT:      reasonStr = "WDT (otro watchdog)"; break;
        case ESP_RST_BROWNOUT: reasonStr = "BROWNOUT (cayo la tension!)"; break;
        case ESP_RST_EXT:      reasonStr = "EXT (reset externo)"; break;
        default: break;
    }
    Serial.printf("Causa del ultimo reset: %s\n", reasonStr);

    pinMode(BUTTON_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), onButtonPress, FALLING);

    if (!LittleFS.begin(true)) {
        Serial.println("Error montando LittleFS");
    }
    dumpFilesystem();

    captivePortalBegin();

    if (!i2sOutBegin(I2S_BCLK_PIN, I2S_LRC_PIN, I2S_DOUT_PIN, AUDIO_CLIENT_SAMPLE_RATE)) {
        Serial.println("Error inicializando I2S");
    }
    player.begin();

    Serial.println("Listo.");
    Serial.print("Conectate a la red WiFi: ");
    Serial.println(AP_SSID);
    Serial.println("y abri http://192.168.4.1/ (o esperá el popup de portal cautivo).");
}

void loop() {
    captivePortalLoop();

    if (buttonFlag) {
        buttonFlag = false;
        uint32_t now = millis();
        if (now - lastButtonMs > BUTTON_DEBOUNCE_MS) {
            lastButtonMs = now;
            // player.play() corta la reproduccion en curso (si hay) antes
            // de arrancar la nueva, asi que no hace falta chequear
            // isPlaying() aca: cada apretada interrumpe y pasa al siguiente.
            playNextSlot();
        }
    }
}
