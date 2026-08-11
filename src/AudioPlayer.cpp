#include "AudioPlayer.h"
#include <LittleFS.h>
#include <esp_timer.h>
#include <string.h>
#include "config.h"

bool parseWavHeader(File &f, WavInfo &info) {
    f.seek(0);
    uint8_t hdr[12];
    if (f.read(hdr, 12) != 12) return false;
    if (memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0) return false;

    bool haveFmt = false, haveData = false;

    while (f.available() >= 8) {
        uint8_t chunkHdr[8];
        if (f.read(chunkHdr, 8) != 8) break;
        uint32_t chunkSize = chunkHdr[4] | (chunkHdr[5] << 8) | (chunkHdr[6] << 16) |
                              ((uint32_t)chunkHdr[7] << 24);

        if (memcmp(chunkHdr, "fmt ", 4) == 0) {
            uint8_t fmt[16] = {0};
            size_t toRead = chunkSize < sizeof(fmt) ? chunkSize : sizeof(fmt);
            if (f.read(fmt, toRead) != toRead) return false;
            if (chunkSize > toRead) f.seek(f.position() + (chunkSize - toRead));

            info.audioFormat = fmt[0] | (fmt[1] << 8);
            info.numChannels = fmt[2] | (fmt[3] << 8);
            info.sampleRate = fmt[4] | (fmt[5] << 8) | (fmt[6] << 16) | ((uint32_t)fmt[7] << 24);
            info.bitsPerSample = fmt[14] | (fmt[15] << 8);
            haveFmt = true;
        } else if (memcmp(chunkHdr, "data", 4) == 0) {
            info.dataOffset = f.position();
            info.dataSize = chunkSize;
            haveData = true;
            break; // no nos interesa nada despues del chunk "data"
        } else {
            uint32_t skip = chunkSize + (chunkSize & 1); // los chunks van alineados a 2 bytes
            f.seek(f.position() + skip);
        }
    }

    if (!haveFmt || !haveData) return false;
    if (info.audioFormat != 1) return false;                     // solo PCM sin comprimir
    if (info.numChannels != 1) return false;                     // solo mono
    if (info.bitsPerSample != 8 && info.bitsPerSample != 16) return false;
    if (info.sampleRate == 0 || info.sampleRate > AUDIO_MAX_SAMPLE_RATE) return false;
    if (info.dataSize == 0) return false;

    return true;
}

bool AudioPlayer::play(const char *path) {
    if (_playing) return false;
    if (!LittleFS.exists(path)) return false;

    strncpy(_path, path, sizeof(_path) - 1);
    _path[sizeof(_path) - 1] = '\0';

    TaskHandle_t handle = nullptr;
    // Prioridad alta y pineada al core 1 para que el timing de muestreo no
    // dependa de lo que este haciendo el stack de WiFi (que corre en core 0).
    BaseType_t ok = xTaskCreatePinnedToCore(
        taskFunc, "audio_play", 4096, this, configMAX_PRIORITIES - 1, &handle, 1);

    return ok == pdPASS;
}

void AudioPlayer::taskFunc(void *param) {
    AudioPlayer *self = static_cast<AudioPlayer *>(param);
    self->_playing = true;

    File f = LittleFS.open(self->_path, FILE_READ);
    WavInfo info;
    if (!f || !parseWavHeader(f, info)) {
        if (f) f.close();
        self->_playing = false;
        vTaskDelete(nullptr);
        return;
    }

    f.seek(info.dataOffset);
    uint32_t remaining = info.dataSize;
    const uint16_t bytesPerSample = info.bitsPerSample / 8;
    const int64_t periodUs = 1000000LL / info.sampleRate;

    const size_t BUF_SAMPLES = 256;
    uint8_t buf[BUF_SAMPLES * 2]; // hasta 16 bit por muestra

    int64_t nextTick = esp_timer_get_time();

    // Cada cuantas muestras cedemos el CPU un tick (~1ms) a proposito.
    // Sin esto, esta tarea (prioridad maxima, espera activa) nunca deja
    // correr a la tarea idle del core 1, y el Task Watchdog de ESP-IDF
    // termina reseteando la placa a mitad de la reproduccion (por defecto
    // dispara a los ~5s de idle starvation) — pasaba seguro con clips de
    // varios segundos. Cada ~200ms de audio perdemos ~1ms real (imperceptible).
    const uint32_t yieldEverySamples = (info.sampleRate / 5) + 1;
    size_t sampleIndex = 0;

    while (remaining >= bytesPerSample) {
        size_t bytesToRead = BUF_SAMPLES * bytesPerSample;
        if (bytesToRead > remaining) bytesToRead = remaining - (remaining % bytesPerSample);
        if (bytesToRead == 0) break;

        size_t got = f.read(buf, bytesToRead);
        if (got < bytesPerSample) break;

        size_t samplesGot = got / bytesPerSample;
        for (size_t i = 0; i < samplesGot; i++) {
            uint16_t dacVal;
            if (info.bitsPerSample == 16) {
                int16_t s = (int16_t)(buf[i * 2] | (buf[i * 2 + 1] << 8));
                dacVal = (uint16_t)(((int32_t)s + 32768) >> 4); // 16 bit con signo -> 12 bit
            } else {
                dacVal = (uint16_t)buf[i] << 4; // 8 bit sin signo -> 12 bit
            }

            while (esp_timer_get_time() < nextTick) {
                // espera activa: a estas frecuencias (<=16kHz) el periodo es
                // de decenas/cientos de us, no vale la pena usar semaforos.
            }
            self->_dac->write12(dacVal);
            nextTick += periodUs;

            if (++sampleIndex % yieldEverySamples == 0) {
                vTaskDelay(1);
                nextTick = esp_timer_get_time(); // no arrastrar el hueco del yield como "atraso"
            }
        }

        remaining -= got;
    }

    f.close();
    self->_playing = false;
    vTaskDelete(nullptr);
}
