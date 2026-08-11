#include <Arduino.h>
#include <I2S.h>
#include <LittleFS.h>
#include "config.h"
#include "AudioPlayer.h"
#include "CaptivePortal.h"

static AudioPlayer player;

static volatile bool buttonFlag = false;
static uint32_t lastButtonMs = 0;
static uint8_t nextSlot = 0;

void IRAM_ATTR onButtonPress() {
    buttonFlag = true;
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

    pinMode(BUTTON_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), onButtonPress, FALLING);

    I2S.setSckPin(I2S_BCLK_PIN);
    I2S.setFsPin(I2S_LRC_PIN);
    I2S.setDataPin(I2S_DOUT_PIN);
    if (!I2S.begin(I2S_PHILIPS_MODE, AUDIO_CLIENT_SAMPLE_RATE, 16)) {
        Serial.println("Error inicializando I2S");
    }

    if (!LittleFS.begin(true)) {
        Serial.println("Error montando LittleFS");
    }

    player.begin(&I2S);
    captivePortalBegin();

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
