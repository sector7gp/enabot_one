#include "I2sOut.h"
#include <Arduino.h>
#include <driver/i2s_std.h>
#include "config.h"

static i2s_chan_handle_t txHandle = nullptr;
static uint32_t currentRate = 0;
static bool channelEnabled = false;

// Los campos se asignan de a uno en vez de usar los macros
// I2S_STD_*_DEFAULT_CONFIG del SDK: esos macros listan los designadores en
// un orden distinto al de declaracion del struct, lo que en C++ es un error
// de compilacion (en C es legal). Los valores son los mismos que usa el
// macro Philips para ESP32-S3.
static void fillStdConfig(i2s_std_config_t &cfg, uint32_t sampleRate,
                          int bclkPin, int lrcPin, int doutPin) {
    cfg.clk_cfg.sample_rate_hz = sampleRate;
    cfg.clk_cfg.clk_src = I2S_CLK_SRC_DEFAULT;
    cfg.clk_cfg.ext_clk_freq_hz = 0;
    cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;

    cfg.slot_cfg.data_bit_width = I2S_DATA_BIT_WIDTH_16BIT;
    cfg.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO;
    cfg.slot_cfg.slot_mode = I2S_SLOT_MODE_STEREO;
    cfg.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;
    cfg.slot_cfg.ws_width = I2S_DATA_BIT_WIDTH_16BIT;
    cfg.slot_cfg.ws_pol = false;
    cfg.slot_cfg.bit_shift = true;
    cfg.slot_cfg.left_align = true;
    cfg.slot_cfg.big_endian = false;
    cfg.slot_cfg.bit_order_lsb = false;

    cfg.gpio_cfg.mclk = I2S_GPIO_UNUSED; // el MAX98357A genera su propio clock interno
    cfg.gpio_cfg.bclk = (gpio_num_t)bclkPin;
    cfg.gpio_cfg.ws = (gpio_num_t)lrcPin;
    cfg.gpio_cfg.dout = (gpio_num_t)doutPin;
    cfg.gpio_cfg.din = I2S_GPIO_UNUSED; // solo salida, no capturamos audio
    cfg.gpio_cfg.invert_flags.mclk_inv = false;
    cfg.gpio_cfg.invert_flags.bclk_inv = false;
    cfg.gpio_cfg.invert_flags.ws_inv = false;
}

bool i2sOutBegin(int bclkPin, int lrcPin, int doutPin, uint32_t sampleRate) {
    i2s_chan_config_t chanCfg = {};
    chanCfg.id = I2S_NUM_AUTO;
    chanCfg.role = I2S_ROLE_MASTER;
    chanCfg.dma_desc_num = 6;
    chanCfg.dma_frame_num = 240;
    chanCfg.auto_clear_after_cb = true; // repite silencio, no la cola del audio, si nos quedamos sin datos
    chanCfg.auto_clear_before_cb = false;
    chanCfg.intr_priority = 0;

    esp_err_t err = i2s_new_channel(&chanCfg, &txHandle, nullptr);
    if (err != ESP_OK) {
        DBG_PRINTF("i2sOutBegin: i2s_new_channel fallo (%s)\n", esp_err_to_name(err));
        return false;
    }

    i2s_std_config_t stdCfg = {};
    fillStdConfig(stdCfg, sampleRate, bclkPin, lrcPin, doutPin);

    err = i2s_channel_init_std_mode(txHandle, &stdCfg);
    if (err != ESP_OK) {
        DBG_PRINTF("i2sOutBegin: init_std_mode fallo (%s)\n", esp_err_to_name(err));
        return false;
    }

    // Ojo: NO se llama i2s_channel_enable() aca a proposito. Ver i2sOutStart().
    currentRate = sampleRate;
    channelEnabled = false;
    return true;
}

bool i2sOutStart(uint32_t sampleRate) {
    if (!txHandle || sampleRate == 0) return false;
    if (channelEnabled) return true;

    // El clock solo se puede reconfigurar con el canal deshabilitado, que es
    // justo el estado en el que estamos antes de arrancar.
    if (sampleRate != currentRate) {
        i2s_std_clk_config_t clkCfg = {};
        clkCfg.sample_rate_hz = sampleRate;
        clkCfg.clk_src = I2S_CLK_SRC_DEFAULT;
        clkCfg.ext_clk_freq_hz = 0;
        clkCfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;

        esp_err_t err = i2s_channel_reconfig_std_clock(txHandle, &clkCfg);
        if (err != ESP_OK) {
            DBG_PRINTF("i2sOutStart: reconfig a %u Hz fallo (%s)\n", sampleRate,
                          esp_err_to_name(err));
            return false;
        }
        currentRate = sampleRate;
    }

    esp_err_t err = i2s_channel_enable(txHandle);
    if (err != ESP_OK) {
        DBG_PRINTF("i2sOutStart: channel_enable fallo (%s)\n", esp_err_to_name(err));
        return false;
    }
    channelEnabled = true;
    return true;
}

void i2sOutStop() {
    if (!txHandle || !channelEnabled) return;
    i2s_channel_disable(txHandle);
    channelEnabled = false;
}

void i2sOutWrite(const void *data, size_t bytes) {
    if (!txHandle || !channelEnabled) return;
    size_t written = 0;
    i2s_channel_write(txHandle, data, bytes, &written, portMAX_DELAY);
}
