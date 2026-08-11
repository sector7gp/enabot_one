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

// El portal solo existe si se arranco con el boton apretado; ver setup().
static bool portalActive = false;
static uint32_t portalStartMs = 0;

void IRAM_ATTR onButtonPress() {
    buttonFlag = true;
}

// Lista el contenido de LittleFS al arrancar: sirve para ver de un vistazo
// si los 4 slots estan realmente guardados y con que tamano, y cuanto
// espacio libre queda (4 clips de 20s a 16kHz son ~2.5MB y la particion
// tiene ~2.6MB, asi que el margen es finito).
#if ENABLE_DEBUG_LOG
static void dumpFilesystem() {
    size_t total = LittleFS.totalBytes();
    size_t used = LittleFS.usedBytes();
    DBG_PRINTF("LittleFS: total=%u used=%u libre=%u\n", total, used, total - used);

    File root = LittleFS.open("/");
    if (!root) {
        DBG_PRINTLN("LittleFS: no pude abrir la raiz");
        return;
    }
    File entry = root.openNextFile();
    if (!entry) DBG_PRINTLN("LittleFS: (vacio, ningun archivo)");
    while (entry) {
        DBG_PRINTF("  %s  %u bytes\n", entry.name(), (unsigned)entry.size());
        entry = root.openNextFile();
    }
    root.close();
}

// Causa del ultimo arranque: si aparece BROWNOUT (o resets repetidos justo
// despues de reproducir) el problema es la alimentacion, no el firmware.
static void dumpResetReason() {
    const char *reasonStr = "otro";
    switch (esp_reset_reason()) {
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
    DBG_PRINTF("Causa del ultimo reset: %s\n", reasonStr);
}
#else
static void dumpFilesystem() {}
static void dumpResetReason() {}
#endif

// Reproduce el siguiente slot en la secuencia (avanza siempre, aunque el
// slot este vacio/invalido) y salta los que no tengan un WAV valido, asi
// un boton no se queda "mudo" si todavia no subiste los 4 audios.
static void playNextSlot() {
    for (uint8_t attempts = 0; attempts < AUDIO_NUM_SLOTS; attempts++) {
        uint8_t slot = nextSlot;
        nextSlot = (nextSlot + 1) % AUDIO_NUM_SLOTS;
        if (player.play(audioSlotPath(slot).c_str())) {
            DBG_PRINTF("Reproduciendo slot %u\n", slot);
            return;
        }
    }
    DBG_PRINTLN("No hay audios validos cargados en ningun slot.");
}

void setup() {
    DBG_BEGIN();
    dumpResetReason();

    pinMode(BUTTON_PIN, INPUT_PULLUP);
    // Leer el boton ANTES de enganchar la interrupcion, y con una pausa para
    // que el pull-up interno termine de levantar la linea: si se arranca con
    // el boton apretado, esto lee LOW y es la señal de "quiero configurar".
    delay(50);
    bool portalRequested = (digitalRead(BUTTON_PIN) == LOW);

    attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), onButtonPress, FALLING);

    if (!LittleFS.begin(true)) {
        DBG_PRINTLN("Error montando LittleFS");
    }
    dumpFilesystem();

    if (portalRequested) {
        captivePortalBegin();
        portalActive = true;
        portalStartMs = millis();
        DBG_PRINTLN("Boton apretado al arrancar -> portal encendido.");
        DBG_PRINT("Conectate a la red WiFi: ");
        DBG_PRINTLN(AP_SSID);
        DBG_PRINTLN("y abri http://192.168.4.1/ (o esperá el popup de portal cautivo).");
    } else {
        // Sin portal la radio nunca se enciende, que es lo que mas bateria
        // ahorra en el uso normal (boton -> audio y nada mas).
        DBG_PRINTLN("Arranque normal (sin portal). Para configurar, reiniciar "
                    "manteniendo el boton apretado.");
    }

    if (!i2sOutBegin(I2S_BCLK_PIN, I2S_LRC_PIN, I2S_DOUT_PIN, AUDIO_CLIENT_SAMPLE_RATE)) {
        DBG_PRINTLN("Error inicializando I2S");
    }
    player.begin();

    DBG_PRINTLN("Listo.");
}

void loop() {
    if (portalActive) {
        captivePortalLoop();
        if (millis() - portalStartMs > PORTAL_TIMEOUT_MS) {
            captivePortalEnd();
            portalActive = false;
        }
    }

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

    // Sin portal no hay nada que atender salvo el boton, que llega por
    // interrupcion: cedemos el CPU en vez de quemar ciclos girando en vacio.
    if (!portalActive) {
        delay(20);
    }
}
