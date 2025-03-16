#include <array>
#include <cstdint>
#include <math.h>
#include <stdio.h>

#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "hardware/pll.h"
#include "hardware/structs/clocks.h"

#include "pico/audio.h"

#include "quadrature_encoder.pio.h"

#include "Oscillator.cpp"
#include "Envelope.cpp"
#include "Wavetable.hpp"
#include "i2s_init.hpp"

uint vol = 100;

// Double output buffer
std::array<int16_t, 1156> out1 = {};
std::array<int16_t, 1156> out2 = {};
bool write_flag = 0;
bool buff = 0;

int main() {
    // Set up system clock for better audio
    pll_init(pll_usb, 1, 1536 * MHZ, 4, 4);
    clock_configure(clk_usb, 0, CLOCKS_CLK_USB_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB,
                    96 * MHZ, 48 * MHZ);
    clock_configure(clk_sys, CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLKSRC_CLK_SYS_AUX,
                    CLOCKS_CLK_SYS_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB, 96 * MHZ,
                    96 * MHZ);
    clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
                    96 * MHZ, 96 * MHZ);
    stdio_init_all();

    // Enable less noise in audio output
    gpio_init(PIN_DCDC_PSM_CTRL);
    gpio_set_dir(PIN_DCDC_PSM_CTRL, GPIO_OUT);
    gpio_put(PIN_DCDC_PSM_CTRL, 1);

    Oscillator osc1 = Oscillator(Square, 440);
    ADSREnvelope env1 = ADSREnvelope(2., 0.5, 0.4, 1., osc1.get_output(), 0.);
    // Initialize I2S audio output
    ap = i2s_audio_init(44100);

    while (true) {

        
        int c = getchar_timeout_us(0);
        if (c >= 0) {
            if (c == '-' && vol) vol--;
            if ((c == '=' || c == '+') && vol < 256) vol++;
            if (c == 's') env1.set_trigger(5.0);
            if (c == 'p') env1.set_trigger(0.0);
            if (c == 'q') break;
            printf("Yo\n\r");
        }
        if (write_flag) {
            if (buff) {
                osc1.out();
                env1.out();
                out1 = env1.get_output();
                write_flag = 0;
            } 
            else {
                osc1.out();
                env1.out();
                out2 = env1.get_output();
                write_flag = 0;
            }
        }

        // else {
        //     // without this print the thing does not work,
        //     // main loop runs too fast atm, no time for interrupt
        //     // is what Im assuming
        //     // thats a good thing at least
        //     printf("Not writing anything\n");
        //     // __wfe(); // Wait for event (low power waiting)
        // }
    }

    return 0;
}

void decode() {
    audio_buffer_t *buffer = take_audio_buffer(ap, false);
    if (buffer == NULL) {
        return;
    }
    int32_t *samples = (int32_t *)buffer->buffer->bytes;
    std::array<int16_t, 1156>& out = (buff) ? out2 : out1;
    for (uint i = 0; i < buffer->max_sample_count; i++) {
        int32_t value0 = (vol * out[i]) << 8u;
        int32_t value1 = (vol * out[i]) << 8u;
        // use 32bit full scale
        samples[i * 2 + 0] = value0 + (value0 >> 16u); // L
        samples[i * 2 + 1] = value1 + (value1 >> 16u); // R
    }
    buff = !buff;
    write_flag = 1;
    buffer->sample_count = buffer->max_sample_count;
    give_audio_buffer(ap, buffer);
    return;
}

extern "C" {
// callback from:
//   void __isr __time_critical_func(audio_i2s_dma_irq_handler)()
//   defined at my_pico_audio_i2s/audio_i2s.c
//   where i2s_callback_func() is declared with __attribute__((weak))
void i2s_callback_func() {
    if (decode_flg) {
        decode();
    }
}
}
