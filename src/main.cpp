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

void IRAM_ATTR onButtonPress() {
    buttonFlag = true;
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
                if (!player.play(AUDIO_FILE_PATH)) {
                    Serial.println("No se pudo reproducir (archivo faltante o invalido).");
                }
            }
        }
    }
}
