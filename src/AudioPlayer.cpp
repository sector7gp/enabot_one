#include "AudioPlayer.h"
#include <LittleFS.h>
#include <string.h>
#include "config.h"
#include "I2sOut.h"

String audioSlotPath(uint8_t slot) {
    return "/audio" + String(slot) + ".wav";
}

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

    if (!haveFmt || !haveData) {
        DBG_PRINTF("parseWavHeader: falta chunk fmt/data (fmt=%d data=%d)\n", haveFmt, haveData);
        return false;
    }
    if (info.audioFormat != 1) {
        DBG_PRINTF("parseWavHeader: audioFormat=%u (esperado 1=PCM)\n", info.audioFormat);
        return false;
    }
    if (info.numChannels != 1) {
        DBG_PRINTF("parseWavHeader: numChannels=%u (esperado 1=mono)\n", info.numChannels);
        return false;
    }
    if (info.bitsPerSample != 8 && info.bitsPerSample != 16) {
        DBG_PRINTF("parseWavHeader: bitsPerSample=%u (esperado 8 o 16)\n", info.bitsPerSample);
        return false;
    }
    if (info.sampleRate == 0 || info.sampleRate > AUDIO_MAX_SAMPLE_RATE) {
        DBG_PRINTF("parseWavHeader: sampleRate=%u (max %u)\n", info.sampleRate, (unsigned)AUDIO_MAX_SAMPLE_RATE);
        return false;
    }
    if (info.dataSize == 0) {
        DBG_PRINTLN("parseWavHeader: dataSize=0");
        return false;
    }

    return true;
}

bool AudioPlayer::play(const char *path) {
    bool exists = LittleFS.exists(path);
    DBG_PRINTF("AudioPlayer::play(%s) exists=%d\n", path, exists);
    if (!exists) return false;

    // Si ya hay algo sonando, lo cortamos primero: un boton fisico debe
    // interrumpir e ir directo al siguiente audio, no ignorar la apretada.
    stopCurrent();

    strncpy(_path, path, sizeof(_path) - 1);
    _path[sizeof(_path) - 1] = '\0';

    TaskHandle_t handle = nullptr;
    // Prioridad un poco por encima de la tarea normal de Arduino: alcanza
    // porque i2s_channel_write() bloquea con primitivas reales de FreeRTOS
    // (no hay espera activa aca), asi que no hace falta prioridad maxima
    // ni ceder CPU a mano para evitar el Task Watchdog.
    BaseType_t ok = xTaskCreatePinnedToCore(
        taskFunc, "audio_play", 4096, this, tskIDLE_PRIORITY + 2, &handle, 1);

    return ok == pdPASS;
}

void AudioPlayer::stopCurrent() {
    if (!_playing) return;
    _stopRequested = true;
    // Espera corta y acotada: la tarea revisa _stopRequested entre bloques
    // de ~256 muestras (~16ms a 16kHz), asi que esto tarda como mucho eso
    // mas lo que falte de la escritura I2S en curso — no toda la duracion
    // del audio.
    while (_playing) {
        vTaskDelay(1);
    }
    _stopRequested = false;
}

void AudioPlayer::taskFunc(void *param) {
    AudioPlayer *self = static_cast<AudioPlayer *>(param);
    self->_playing = true;
    self->_stopRequested = false;

    File f = LittleFS.open(self->_path, FILE_READ);
    DBG_PRINTF("taskFunc: abriendo %s, ok=%d, size=%u\n", self->_path, (bool)f, f ? f.size() : 0);
    WavInfo info;
    if (!f || !parseWavHeader(f, info)) {
        DBG_PRINTLN("taskFunc: parseWavHeader fallo, no reproduzco");
        if (f) f.close();
        self->_playing = false;
        vTaskDelete(nullptr);
        return;
    }

    DBG_PRINTF("taskFunc: WAV valido, sampleRate=%u bits=%u dataSize=%u dataOffset=%u\n",
                  info.sampleRate, info.bitsPerSample, info.dataSize, info.dataOffset);

    // Habilita el I2S solo mientras dura la reproduccion, ajustando el reloj
    // a este clip (cada slot puede tener su propio sample rate). En reposo el
    // canal queda apagado: sin BCLK/LRC el MAX98357A no consume ni hace ruido.
    if (!i2sOutStart(info.sampleRate)) {
        DBG_PRINTLN("taskFunc: no pude habilitar el I2S");
        f.close();
        self->_playing = false;
        vTaskDelete(nullptr);
        return;
    }

    f.seek(info.dataOffset);
    uint32_t remaining = info.dataSize;
    const uint16_t bytesPerSample = info.bitsPerSample / 8;

    const size_t BUF_SAMPLES = 256;
    uint8_t rawBuf[BUF_SAMPLES * 2];     // hasta 16 bit por muestra, mono
    int16_t stereoBuf[BUF_SAMPLES * 2];  // interleaved L/R (mismo valor en ambos)

    while (remaining >= bytesPerSample && !self->_stopRequested) {
        size_t bytesToRead = BUF_SAMPLES * bytesPerSample;
        if (bytesToRead > remaining) bytesToRead = remaining - (remaining % bytesPerSample);
        if (bytesToRead == 0) break;

        size_t got = f.read(rawBuf, bytesToRead);
        if (got < bytesPerSample) break;

        size_t samplesGot = got / bytesPerSample;
        for (size_t i = 0; i < samplesGot; i++) {
            int16_t s;
            if (info.bitsPerSample == 16) {
                s = (int16_t)(rawBuf[i * 2] | (rawBuf[i * 2 + 1] << 8));
            } else {
                s = (int16_t)(((int)rawBuf[i] - 128) << 8); // 8 bit sin signo -> 16 bit con signo
            }
            // El MAX98357A puede estar cableado para tomar L, R o (L+R)/2
            // segun su pin SD/MODE; mandando el mismo valor en ambos
            // canales suena correcto sin importar cual sea.
            stereoBuf[i * 2] = s;
            stereoBuf[i * 2 + 1] = s;
        }

        // Bloquea (espera FreeRTOS real, no espera activa) hasta que haya
        // lugar en el buffer DMA, asi no se pierden muestras.
        i2sOutWrite(stereoBuf, samplesGot * 2 * sizeof(int16_t));

        remaining -= got;
    }
    f.close();

    // Cola de silencio para que el ampli no quede con el ultimo nivel DC
    // pegado en la salida al terminar (el canal esta con
    // auto_clear_after_cb, pero esto ademas cubre el corte por boton).
    const size_t SILENCE_FRAMES = 1600; // 100ms a 16kHz
    memset(stereoBuf, 0, sizeof(stereoBuf));
    size_t silenceLeft = SILENCE_FRAMES;
    while (silenceLeft > 0) {
        size_t chunk = silenceLeft < BUF_SAMPLES ? silenceLeft : BUF_SAMPLES;
        i2sOutWrite(stereoBuf, chunk * 2 * sizeof(int16_t));
        silenceLeft -= chunk;
    }

    i2sOutStop(); // vuelve a dejar el ampli en reposo

    self->_playing = false;
    vTaskDelete(nullptr);
}
