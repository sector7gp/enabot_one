#pragma once
#include <stddef.h>
#include <stdint.h>

// Salida de audio por I2S hacia el MAX98357A.
//
// Usa el driver moderno del IDF (driver/i2s_std.h) en vez de la libreria
// "I2S" de Arduino: esa es un wrapper del driver legacy, viene marcada
// architectures=esp32 (no S3) y, cuando se la fuerza en un S3, deja el
// WiFi arriba "en apariencia" (softAP() devuelve OK) pero sin irradiar.

// Inicializa el canal TX, pero lo deja DESHABILITADO. Mientras esta
// deshabilitado no salen ni BCLK ni LRC, con lo cual el MAX98357A queda en
// reposo: no consume ni mete ruido. Devuelve false si algo falla.
bool i2sOutBegin(int bclkPin, int lrcPin, int doutPin, uint32_t sampleRate);

// Habilita el canal para reproducir, ajustando el sample rate a este clip
// (los WAV de los distintos slots no tienen por que compartir el mismo).
bool i2sOutStart(uint32_t sampleRate);

// Vuelve a dejar el canal en reposo al terminar de reproducir.
void i2sOutStop();

// Escribe muestras estereo interleaved (16 bit). Bloquea hasta que entran
// en el buffer DMA; no hay espera activa.
void i2sOutWrite(const void *data, size_t bytes);
