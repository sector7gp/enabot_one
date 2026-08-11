#include <Arduino.h>
#include <Wire.h>
#include <LittleFS.h>
#include "config.h"
#include "MCP4725Fast.h"
#include "AudioPlayer.h"
#include "CaptivePortal.h"

static MCP4725Fast dac;
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

    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(I2C_CLOCK_HZ);
    dac.begin(MCP4725_ADDR, Wire);

    if (!LittleFS.begin(true)) {
        Serial.println("Error montando LittleFS");
    }

    player.begin(&dac);
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
            if (!player.isPlaying()) {
                playNextSlot();
            }
        }
    }
}
