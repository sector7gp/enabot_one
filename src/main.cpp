#include <Arduino.h>
#include <esp_system.h>
#include <math.h>
#include <string.h>
#include <LittleFS.h>
#include "config.h"
#include "AudioPlayer.h"
#include "CaptivePortal.h"
#include "I2sOut.h"

static AudioPlayer player;

static uint32_t lastButtonMs = 0;
static uint8_t nextSlot = 0;

// Estado del antirrebote. Se sondea el pin en vez de usar attachInterrupt:
// una interrupcion por flanco no distingue una pulsacion real de un glitch
// de ruido de microsegundos, y con el I2S andando al lado del cable del
// boton eso disparaba reproducciones solas, una atras de otra.
static int btnLastRead = HIGH;
static int btnStable = HIGH;
static uint32_t btnLastChangeMs = 0;

// Devuelve true una sola vez por pulsacion, cuando el pin ya se quedo
// estable en LOW el tiempo suficiente.
static bool buttonPressed() {
    int reading = digitalRead(BUTTON_PIN);
    uint32_t now = millis();

    if (reading != btnLastRead) {
        btnLastRead = reading;
        btnLastChangeMs = now;
        return false;
    }
    if (now - btnLastChangeMs < BUTTON_STABLE_MS) return false;
    if (reading == btnStable) return false;

    btnStable = reading;
    return (reading == LOW); // solo nos interesa el flanco de bajada ya confirmado
}

// El portal solo existe si se arranco con el boton apretado; ver setup().
static bool portalActive = false;
static uint32_t portalStartMs = 0;

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

// Pitido corto de confirmacion. Sin pantalla ni serial conectado, es la
// unica forma de saber que el portal efectivamente arranco (y no quedarse
// adivinando si se acerto la ventana del boton).
static void playBeep(uint16_t freqHz, uint16_t ms) {
    const uint32_t rate = AUDIO_CLIENT_SAMPLE_RATE;
    if (!i2sOutStart(rate)) return;

    const size_t FRAMES = 256;
    int16_t buf[FRAMES * 2];
    const uint32_t totalFrames = (uint32_t)rate * ms / 1000;
    const float step = 2.0f * PI * freqHz / rate;
    float phase = 0.0f;

    for (uint32_t done = 0; done < totalFrames;) {
        size_t n = totalFrames - done < FRAMES ? totalFrames - done : FRAMES;
        for (size_t i = 0; i < n; i++) {
            int16_t s = (int16_t)(6000.0f * AUDIO_VOLUME_PERCENT / 100.0f * sinf(phase));
            phase += step;
            if (phase > 2.0f * PI) phase -= 2.0f * PI;
            buf[i * 2] = s;
            buf[i * 2 + 1] = s;
        }
        i2sOutWrite(buf, n * 2 * sizeof(int16_t));
        done += n;
    }

    memset(buf, 0, sizeof(buf)); // cola de silencio
    i2sOutWrite(buf, sizeof(buf));
    i2sOutStop();
}

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
    delay(50); // que el pull-up interno termine de levantar la linea

    DBG_PRINTF("Boton al arrancar: %s (esperando %u ms para pedir el portal)\n",
               digitalRead(BUTTON_PIN) == LOW ? "APRETADO" : "suelto",
               (unsigned)PORTAL_TRIGGER_WINDOW_MS);

    // Se sondea el boton durante toda la ventana en vez de mirarlo una sola
    // vez: asi sirve tanto arrancar con el boton ya apretado como apretarlo
    // en los primeros segundos, sin depender de acertar el instante justo.
    // Se exige que quede estable en LOW para no confundir ruido con pulsacion.
    bool portalRequested = false;
    uint32_t windowStart = millis();
    uint32_t lowSinceMs = 0;
    while (millis() - windowStart < PORTAL_TRIGGER_WINDOW_MS) {
        if (digitalRead(BUTTON_PIN) == LOW) {
            if (lowSinceMs == 0) lowSinceMs = millis();
            if (millis() - lowSinceMs >= BUTTON_STABLE_MS) {
                portalRequested = true;
                break;
            }
        } else {
            lowSinceMs = 0;
        }
        delay(5);
    }

    // Deja el antirrebote alineado con el estado real del pin, para que
    // soltar el boton despues de pedir el portal no cuente como pulsacion.
    btnLastRead = btnStable = digitalRead(BUTTON_PIN);
    btnLastChangeMs = millis();

    if (!LittleFS.begin(true)) {
        DBG_PRINTLN("Error montando LittleFS");
    }
    dumpFilesystem();

#ifdef FORCE_PORTAL
    portalRequested = true; // solo para poder probar el AP sin apretar el boton
#endif

    // ORDEN IMPORTANTE: el AP se levanta ANTES de tocar el I2S. Con el I2S ya
    // inicializado (y peor, despues de hacer sonar algo), el AP arranca "OK"
    // segun el driver pero no llega a irradiar. Ver la seccion de
    // troubleshooting del README.
    if (portalRequested) {
        captivePortalBegin();
        portalActive = true;
        portalStartMs = millis();
        DBG_PRINTLN("Boton detectado en el arranque -> portal encendido.");
        DBG_PRINT("Conectate a la red WiFi: ");
        DBG_PRINTLN(AP_SSID);
        DBG_PRINTLN("y abri http://192.168.4.1/ (o esperá el popup de portal cautivo).");
    } else {
        // Sin portal la radio nunca se enciende, que es lo que mas bateria
        // ahorra en el uso normal (boton -> audio y nada mas).
        DBG_PRINTLN("Arranque normal (sin portal). Para configurar, reiniciar y "
                    "apretar el boton dentro de los primeros segundos.");
    }

    if (!i2sOutBegin(I2S_BCLK_PIN, I2S_LRC_PIN, I2S_DOUT_PIN, AUDIO_CLIENT_SAMPLE_RATE)) {
        DBG_PRINTLN("Error inicializando I2S");
    }
    player.begin();

    // El pitido va al final, con el AP ya establecido: es la confirmacion de
    // que se entro en modo configuracion.
    if (portalRequested) {
        playBeep(1200, 180);
    }

    DBG_PRINTLN("Listo.");
}

void loop() {
    if (portalActive) {
        captivePortalLoop();
        // Nunca cortar el WiFi con un OTA a medio camino: quedaria una
        // imagen incompleta en flash.
        if (millis() - portalStartMs > PORTAL_TIMEOUT_MS && !captivePortalOtaInProgress()) {
            captivePortalEnd();
            portalActive = false;
        }
    }

    if (buttonPressed()) {
        uint32_t now = millis();
        if (now - lastButtonMs > BUTTON_DEBOUNCE_MS) {
            lastButtonMs = now;
            // player.play() corta la reproduccion en curso (si hay) antes
            // de arrancar la nueva, asi que no hace falta chequear
            // isPlaying() aca: cada apretada interrumpe y pasa al siguiente.
            playNextSlot();
        }
    }

    // Cede CPU en vez de girar en vacio, pero lo bastante seguido como para
    // que el antirrebote muestree bien el boton.
    delay(5);
}
