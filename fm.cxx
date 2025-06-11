/*
 -- Multicore --
 !! FM synth is being run by interpolator0/1 !!
 and sequenced by a thread on core1
 it could also be sequenced by an interrupt

 */

#include "hardware/gpio.h"
#include "hardware/timer.h"
#include <hardware/irq.h>
#include <hardware/adc.h>
#include <hardware/pwm.h>
#include "hardware/interp.h"

#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "stdio.h"
#include <string.h>
#include <pico/multicore.h>
#include <pico/divider.h>
#include "hardware/sync.h"
#include "stdlib.h"
#include "math.h"

// ==========================================
// === set up interpolator
// ==========================================
// accum0 is DDS phase
// base0 is DDS increment
// base2 is pointer to sine_table start
// Interpolator0 (FM modulation frequency) setup to:
//  add accum0 + base0 amd store in accum0 (result0 to accum0) (add raw -- no shift)
//  right-shift accum0 23 bits and mask to bits 8:1 (zero low bit for short pointer)
//  add shifted/masked accum0 to base2 and read result2 as table position output to interp1
// Interpolator1 (main oscillator) setup to:
//  add accum0 + base0 amd store in accum0 (result0 to accum0) (add raw -- no shift)
//  right-shift accum0 23 bits and mask to bits 8:1 (zero low bit for short pointer)
//  add shifted/masked accum0 to base2 and read result2 as table position output to PWM

void interpolator_for_dds(void){
  // --- interp0
  interp_config cfg0 = interp_default_config();
  interp_set_config(interp0, 0, &cfg0);
  // base0 + accum0 -> accum0
  //full 32-bit integer add back to accum0
  interp_config_set_add_raw (&cfg0, true) ;
  // (accum0>>23) & 0x1fe
  // shift and mask for index gen assuming
  // word-aligned, short, sine table
  // actual sine-table pointer will be read from 
  // result2 = ((accum0>>23) & 0x1fe) + base2
  interp_config_set_shift (&cfg0, 23) ;
  interp_config_set_mask(&cfg0, 1, 8);
  // update config
  interp_set_config(interp0, 0, &cfg0);
  // --- interp1
  interp_config cfg1 = interp_default_config();
  interp_set_config(interp1, 0, &cfg1);
  // base0 + accum0 -> accum0
  //full 32-bit integer add back to accum0
  interp_config_set_add_raw (&cfg1, true) ;
  // (accum0>>23) & 0x1fe
  // shift and mask for index gen assuming
  // word-aligned, short, sine table
  // actual sine-table pointer will be read from 
  // result2 = ((accum0>>23) & 0x1fe) + base2
  interp_config_set_shift (&cfg1, 23) ;
  interp_config_set_mask(&cfg1, 1, 8);
  // update config
  interp_set_config(interp1, 0, &cfg1);
}

// ==========================================
// === set up pwm
// ==========================================
uint pwm_slice_num,  pwm_chan_num ;
void pwm_setup(int pwm_pin){
  gpio_init(pwm_pin) ;
  gpio_set_function(pwm_pin, GPIO_FUNC_PWM) ;
  pwm_slice_num = pwm_gpio_to_slice_num(pwm_pin) ;
  // full speed clock => 256 counts in 2 uSec
  pwm_set_clkdiv(pwm_slice_num, 1.0f);
  pwm_set_clkdiv_mode(pwm_slice_num, PWM_DIV_FREE_RUNNING) ;
  // max count 256
  pwm_set_wrap(pwm_slice_num, 256);
  // initial for testing to half
  pwm_chan_num = pwm_gpio_to_channel(pwm_pin);
  pwm_set_chan_level(pwm_slice_num, pwm_chan_num , 0) ;
  pwm_set_enabled(pwm_slice_num, 1);
}

volatile int alarm_period = 10 ;
// DDS variables
unsigned int increment0, increment1, fm_depth ;
short sine_table[256], mod_sine_table[256] ;
float Fs, Fout, Fmod ;

// ==========================================
// === set up timer ISR NOT used in this pgm
// ==========================================
// === timer alarm ========================
// !! modifiying alarm zero trashes the cpu 
//        and causes LED  4 long - 4 short
// !! DO NOT USE alarm 0
/*
#define ALARM_NUM 1
#define ALARM_IRQ TIMER_IRQ_1
// ISR interval will be 10 uSec
//
// the actual ISR
static void alarm_irq(void) {
    // mark ISR entry
    //gpio_put(2,1);
    // Clear the alarm irq
    hw_clear_bits(&timer_hw->intr, 1u << ALARM_NUM);
    // arm the next interrupt
    // Write the lower 32 bits of the target time to the alarm to arm it
    timer_hw->alarm[ALARM_NUM] = timer_hw->timerawl + alarm_period ;
    //
    // === do DDS the usual way; ISR time 700 nS 
    //accum += increment ;
    //pwm_set_chan_level(pwm_slice_num, pwm_chan_num , sine_table[accum>>24]) ;

    // === interpolator version ; ISR time 600 nS
    // POP2 contents of pointer in result2 to pwm period 
    // Reading the 'pop' registers also clock the state of the interpolator
    pwm_set_chan_level(pwm_slice_num, pwm_chan_num , *(short*)(interp0->pop[2])) ;

    // mark ISR exit
    //gpio_put(2,0);
}
// set up the timer alarm ISR
static void alarm_in_us(uint32_t delay_us) {
    // Enable the interrupt for our alarm (the timer outputs 4 alarm irqs)
    hw_set_bits(&timer_hw->inte, 1u << ALARM_NUM);
    // Set irq handler for alarm irq
    irq_set_exclusive_handler(ALARM_IRQ, alarm_irq);
    // Enable the alarm irq
    irq_set_enabled(ALARM_IRQ, true);
    // Enable interrupt in block and at processor
    // Alarm is only 32 bits 
    uint64_t target = timer_hw->timerawl + delay_us;
    // Write the lower 32 bits of the target time to the alarm which
    // will arm it
    timer_hw->alarm[ALARM_NUM] = (uint32_t) target;   
}
*/

// ==========================================
// === protothreads globals
// ==========================================
// protothreads header
#include "pt_cornell_rp2040_v1_1_1.h"

// ==================================================
// === user's serial input thread
// ==================================================
// 
static PT_THREAD (protothread_serial(struct pt *pt))
{
  PT_BEGIN(pt);
    
    // DDS frequenies
    Fout = 440 ;
    Fmod = 2 ;
    // convert alarm period in uSEc to rate
    Fs = 1.0/((float)alarm_period*1e-6) ;
    // increment = Fout/Fs * 2^32
    // the main center freq
    increment1 = Fout * pow(2,32)/ Fs ;

    // the modulation freq
    increment0 = Fmod * pow(2,32)/ Fs ;

    // data structure for interval timer
    PT_INTERVAL_INIT() ;
    //
    
    while(1) {
      
      // == Fout and Fmod are in Hz
      // == fm_depth is an integer: 0 for VERY little modulation, range up to around 20
      sprintf(pt_serial_out_buffer, "\n\rinput main, mod frequency, fm_depth: ");
      serial_write ;
      serial_read ;
      // convert input string to number
      sscanf(pt_serial_in_buffer,"%f %f %d", &Fout, &Fmod, &fm_depth) ;
      // increment = Fout/Fs * 2^32
      increment1 = Fout * pow(2,32 )/ Fs ;
      increment0 = Fmod * pow(2,32 )/ Fs ;

      //PT_YIELD_INTERVAL(100) ;
      // NEVER exit while
    } // END WHILE(1)
  PT_END(pt);
} // timer thread

// ==================================================
// === toggle25 thread 
// ==================================================
// the on-board LED blinks
static PT_THREAD (protothread_toggle25(struct pt *pt))
{
  PT_BEGIN(pt);
    static bool LED_state = false ;
    
    // set up LED p25 to blink
    gpio_init(25) ;	
    gpio_set_dir(25, GPIO_OUT) ;
    gpio_put(25, true);
    // data structure for interval timer
    PT_INTERVAL_INIT() ;
     
    while(1) {
      // yield time 0.1 second
      PT_YIELD_INTERVAL(200000) ;

      // toggle the LED on PICO
      LED_state = LED_state? false : true ;
      gpio_put(25, LED_state);
      //
      // NEVER exit while
    } // END WHILE(1)
  PT_END(pt);
} // blink thread


// ==================================================
// === fast_ddds thread -- RUNNING on core 1
// ==================================================
// 
static PT_THREAD (protothread_fast_dds(struct pt *pt))
{
  PT_BEGIN(pt);
    
    // data structure for interval timer
    PT_INTERVAL_INIT() ;

    // init interpolator
    interpolator_for_dds();
    // set accum0 0 the DDS phase
    interp0->accum[0] = 0;
    interp1->accum[0] = 0;

    // set base2 to sine_table start addresses
    interp0->base[2] =(int) mod_sine_table ;
    interp1->base[2] =(int) sine_table ;
    //
    // start pwm
    #define pwm_dds 3
    pwm_setup(pwm_dds);

    gpio_init(2) ;	
    gpio_set_dir(2, GPIO_OUT) ;
    gpio_put(2, true);

    while(1) {
      //
     // set rate to 100 khz
     PT_YIELD_INTERVAL(alarm_period) ;
     
      // dds modulation freq
      interp0->base[0] = increment0 ;
      // ddds main freq
      interp1->base[0] = increment1 + (((int)*(short*)(interp0->pop[2]))<<fm_depth) ;
      // read final result
      pwm_set_chan_level(pwm_slice_num, pwm_chan_num , *(short*)(interp1->pop[2])) ;

      // NEVER exit while
    } // END WHILE(1)
  PT_END(pt);
} // blink thread


// ========================================
// === core 1 main -- started in main below
// ========================================
void core1_main(){ 
  //  === add threads  ====================
  // for core 1
  pt_add_thread(protothread_fast_dds);
  //
  // === initalize the scheduler ==========
  pt_schedule_start ;
  // NEVER exits
  // ======================================
}

// ========================================
// === core 0 main
// ========================================
int main(){
  // set the clock
  //set_sys_clock_khz(250000, true); // 171us

  // dds table
  int i = 0 ;
  while (i<256) {
    sine_table[i] = (short) (127 * sin(2*3.1416*i/256) + 128) ;
    // modulation has no offset
    mod_sine_table[i] = (short) (127 * sin(2*3.1416*i/256)) ;
    i++ ;
  }

  //start timer alarm IRQ
  // In THIS version, core1 takes the place of n ISR
  // toggle a pin for timing
  //gpio_init(2) ;	
  //gpio_set_dir(2, GPIO_OUT) ;
  //alarm_in_us(alarm_period);

  // start the serial i/o
  stdio_init_all() ;
  // announce the threader version on system reset
  printf("\n\rProtothreads RP2040 v1.11 two-core\n\r");
     
  // === add threads ======================
  // start core 1 threads
  multicore_reset_core1();
  multicore_launch_core1(&core1_main);

  // === config threads ===================
  // for core 0
  pt_add_thread(protothread_serial);
  pt_add_thread(protothread_toggle25);
  //
  // === initalize the scheduler ===============
  pt_schedule_start ;
  // NEVER exits
  // ===========================================
} // end main
///////////
// end ////
///////////
