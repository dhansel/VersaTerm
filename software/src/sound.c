// -----------------------------------------------------------------------------
// VersaTerm - A versatile serial terminal
// Copyright (C) 2022 David Hansel
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation,
// Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
// -----------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "pico/time.h"
#include "pins.h"
#include "config.h"

#define TIMER_ALARM 0
#define TIMER_IRQ   TIMER_IRQ_0

static uint32_t halfwave_len_us = 100;
static uint32_t halfwave_count = 0;
static bool     halfwave = false;
static uint     slice_num = 0;


// defined in main.c
void run_tasks(bool processInput);


static void sound_irq_fn(void)
{
  // clear interrupt flag
  hw_clear_bits(&timer_hw->intr, 1u << TIMER_ALARM);

  if( --halfwave_count == 0 )
    {
      irq_set_enabled(TIMER_IRQ, false);
      pwm_set_enabled(pwm_gpio_to_slice_num(PIN_BUZZER), false);
      gpio_set_function(PIN_BUZZER, GPIO_FUNC_SIO);
    }
  else
    {
      // alternate pin mode between PWM and SIO (off) for each half-wave
      gpio_set_function(PIN_BUZZER, halfwave ? GPIO_FUNC_PWM : GPIO_FUNC_SIO);
      
      // schedule next interrupt
      timer_hw->alarm[TIMER_ALARM] += halfwave_len_us;
      halfwave = !halfwave;
    }
}


bool sound_playing()
{
  return halfwave_count>0;
}


void sound_play_tone(uint16_t frequency, uint16_t duration_ms, uint8_t volume, bool wait)
{
  if( volume>0 )
    {
      // disable timer interrupt
      irq_set_enabled(TIMER_IRQ, false);
  
      uint32_t hwlen = 1000000u / (frequency * 2);
      if( halfwave_count==0 || halfwave_len_us!=hwlen )
        {
          halfwave = false;
          halfwave_len_us = hwlen;
          pwm_set_enabled(pwm_gpio_to_slice_num(PIN_BUZZER), true);
          timer_hw->alarm[TIMER_ALARM] = timer_hw->timerawl + halfwave_len_us;
        }

      // PWM counter goes from 0-100, set the level (i.e. on->off switch point)
      // to 1-101 according to volume setting
      pwm_set_chan_level(slice_num, PWM_CHAN_B, MIN(volume+1, 101));
  
      // set beep length
      halfwave_count = (frequency * 2 * duration_ms) / 1000;
  
      // enable timer interrupt
      irq_set_enabled(TIMER_IRQ, true);
    }

  if( wait ) { while( sound_playing() ) run_tasks(false); }
}


void sound_init()
{
  slice_num = pwm_gpio_to_slice_num(PIN_BUZZER);

  // initialize pin to output 0 when set to SIO mode
  gpio_init(PIN_BUZZER);
  gpio_set_dir(PIN_BUZZER, true); // output
  gpio_put(PIN_BUZZER, false);

  // set PWM prescaler such that PWM counter runs at 10MHz
  // wrap PWM counter over at 100 => 100kHz PWM frequency
  pwm_config config = pwm_get_default_config();
  pwm_config_set_clkdiv(&config, clock_get_hz(clk_sys)/10000000);
  pwm_init(slice_num, &config, false);
  pwm_set_wrap(slice_num, 100);

  // prepare timer for alarm
  hw_set_bits(&timer_hw->inte, 1u << TIMER_ALARM);
  irq_set_exclusive_handler(TIMER_IRQ, sound_irq_fn);
  irq_set_enabled(TIMER_IRQ, false);
}
