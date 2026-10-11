/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 *                    sekigon-gonnoc
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "hardware/clocks.h"
#include "tusb.h"
#include "usb_descriptors.h"
#include "mm.h"

#define MAX_MESSAGE_LENGTH 32
const uint fp_LED = 7;  // was 22;
const uint ledPin = 25;
 const uint fp3v3Pin = 26;
static char message[MAX_MESSAGE_LENGTH] = {0x55};
static unsigned int message_pos = 0;

/*------------- MAIN -------------*/

int main(void) {
  // default 125MHz is not appropriate. Sysclock should be multiple of 12MHz.
  set_sys_clock_khz(120000, true);
  sleep_ms(10);
  gpio_init(ledPin);
  gpio_set_dir(ledPin, GPIO_OUT);
  gpio_init(fp_LED);
  gpio_set_dir(fp_LED, GPIO_OUT);
  //gpio_init(fp3v3Pin);
  //gpio_set_dir(fp3v3Pin, GPIO_IN);
  gpio_put(ledPin, true); // turn on led
  gpio_put(fp_LED, true);
  tud_init(0);   // init device stack on native usb (roothub port0)

  init_PICO_GPIO();
  init_PICO_SPI();
  init_PICO_I2C();
  init_ANALOG();
  blink_it(100);
  
  while (true) {
    tud_task(); // tinyusb device task
    tud_cdc_write_flush();
  }

  return 0;
}

//--------------------------------------------------------------------+
// Device CDC
//--------------------------------------------------------------------+

// Invoked when CDC interface received data from host
void tud_cdc_rx_cb(uint8_t itf)
{
  (void) itf;
  char buf[64];
  uint8_t *prb;
  uint8_t *cal[64];
  uint32_t count = tud_cdc_read(buf, sizeof(buf));
  if (count > 0)
  {
    switch (buf[0])
    {
      case MODEL: { prb = model(buf); tud_cdc_write(prb, sizeof(prb)); break; }
      case INIT_USER_GPIO: { init_USER_GPIO(buf); break; }
//      case INIT_23S17: { prb = init_23S17(buf); tud_cdc_write(prb, sizeof(prb)); break;}
      case WR_USER_GPIO: { write_USER_GPIO(buf); break;}
      case WR_GPIO_DIR: { write_GPIO_DIR(buf); break; }
      case WR_GPIO_OUTPUTS: { write_GPIO_OUTPUTS(buf); break; }
      case RD_USER_GPIO_INPUTS: { prb = read_USER_GPIO(buf); tud_cdc_write(prb, sizeof(prb)); break; }
      case INIT_USER_SPI: { init_USER_SPI(buf); break; }
      case WRITE_USER_SPI: { prb = write_USER_SPI(buf); tud_cdc_write(prb, sizeof(prb)); break; }
      case READ_USER_SPI: { prb = read_USER_SPI(buf); sleep_ms(2); tud_cdc_write(prb, sizeof(prb)); break; }
//      case RW_USER_SPI: { prb = write_read_USER_SPI(buf); tud_cdc_write(prb, sizeof(prb)); break; }
      case RW_USER_SPI: { prb = wr_rd_USER_SPI(buf); tud_cdc_write(prb, sizeof(prb)); break; }
      case I2C_INIT: { prb = init_USER_I2C(buf); tud_cdc_write(prb, sizeof(prb)); break; }
      case I2C_WRITE: { prb = write_I2C(buf); tud_cdc_write(prb, sizeof(prb)); break; }
      case I2C_READ: { prb = read_I2C(buf); tud_cdc_write(prb, sizeof(prb)); break; }
      case I2C_WRITE_READ: { prb = write_read_I2C(buf); tud_cdc_write(prb, sizeof(prb)); break; }
      case I2C_WRITE_BEFORE_READ: { write_before_read_I2C(buf); break; }        // was commented out
      case I2C_ERROR: { prb = get_i2c_error_code(); tud_cdc_write(prb, sizeof(prb)); break; }      
      case INIT_USER_UART: { prb = init_USER_UART(buf); tud_cdc_write(prb, sizeof(prb)); break; }
      case DEINIT_USER_UART: { uart_deinit(USER_UART_ID); break;}  // returns inline void
      case WRITE_TXD: { write_TXD(buf); break; }  // returns inline void
      case READ_RXD: { prb = read_RXD(buf); tud_cdc_write(prb, sizeof(prb)); break; }
      case INIT_ANALOG: { init_ANALOG(); break; }
      case INIT_SMU_RELAYS: { init_SMU_RELAYS(); break; }
      case WRITE_ADS1115: { write_ADS1115(buf); break; }
      case WRITE_DAC_SMU1: { write_DAC_SMU1(buf); break; }
      case WRITE_DAC_SMU2: { write_DAC_SMU2(buf); break; }
      case SET_MUX_508: { set_mux_508(buf); break; }
      case CONVERT_ADS1115: { start_ADS1115(buf); break; }
      case READ_ADS1115: { prb = read_ADS1115(buf); tud_cdc_write(prb, sizeof(prb)); break; }
      case INIT_9557: { init_9557(); break; }
      case SET_RANGE_RELAYS_SMU1: { set_range_relays_smu1(buf); break; }
      case SET_RANGE_RELAYS_SMU2: { set_range_relays_smu2(buf); break; }
      case SET_DIO_9557: { set_dio_9557(buf); break; }
      case SET_FORCE_CLAMP_SMU1: { set_force_clamp_SMU1(buf); break; }
      case SET_FORCE_CLAMP_SMU2: { set_force_clamp_SMU2(buf); break; }
      case GET_CAL_CONSTANTS: {
        static uint8_t cal_buf[64];
        get_cal_constants((int8_t *)cal_buf);
        tud_cdc_write(cal_buf, 64);
        break;
      }
      case SET_VCC: { set_VCC(buf); break; }
      case PING: { prb = ping_it(); tud_cdc_write(prb, 4); break; }
      default: { break; }
    }
  }
//  (void) count;
//    tud_cdc_write (buf, sizeof(buf));

}
