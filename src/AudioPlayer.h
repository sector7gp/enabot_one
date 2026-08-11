#pragma once
#include <FS.h>
#include <WString.h>
#include <stdint.h>
#include <I2S.h>

// Ruta del archivo de audio para un slot (0-indexado): "/audio0.wav", etc.
String audioSlotPath(uint8_t slot);

struct WavInfo {
    uint16_t audioFormat;   // 1 = PCM
    uint16_t numChannels;
    uint32_t sampleRate;
    uint16_t bitsPerSample; // 8 o 16
    uint32_t dataOffset;    // posicion absoluta del chunk "data" en el archivo
    uint32_t dataSize;      // bytes
};

// Lee y valida el header de un WAV PCM mono (8 o 16 bit) abierto en `f`.
// Deja el file position al inicio del chunk "fmt "/"data" recorridos; el
// llamador debe hacer f.seek(info.dataOffset) antes de leer las muestras.
bool parseWavHeader(File &f, WavInfo &info);

class AudioPlayer {
public:
    void begin(I2SClass *i2s) { _i2s = i2s; }

    // Si el archivo existe y es valido: corta la reproduccion en curso (si
    // hay una) y arranca esta en una tarea aparte. Bloquea brevemente
    // (unos ms como mucho) solo para esperar a que la tarea anterior
    // termine de cortar, no por toda la duracion del audio.
    // Devuelve false si el archivo no existe o es invalido.
    bool play(const char *path);

    bool isPlaying() const { return _playing; }

private:
    static void taskFunc(void *param);
    void stopCurrent();

    I2SClass *_i2s = nullptr;
    volatile bool _playing = false;
    volatile bool _stopRequested = false;
    char _path[32] = {0};
};
