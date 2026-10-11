#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/irq.h"
#include "pico/binary_info.h"
#include "hardware/spi.h"
#include "hardware/i2c.h"

#define UART_ID uart0
#define USER_UART_ID uart1
#define UART0_IRQ_NUMBER 20

#define BAUD_RATE 115200
#define DATA_BITS 8
#define STOP_BITS 1
#define PARITY    UART_PARITY_NONE
#define SPI_BUF_LEN 4
#define SPI_IN_BUF_LEN 4 
#define UART_TX_PIN 0
#define UART_RX_PIN 1
#define SPI_ID spi0
#define SPI_ID1 spi1
#define I2C_ID i2c0
#define I2C_ID1 i2c1

#define MODEL_NUMBER 5

//const uint8_t MODEL_NUMBER = 15; // Model 6215
//#define MODEL_NUMBER 15 // Model 6215
//static const unsigned USER_MISO_OE_b = 11;  // pico pin 15
//static const unsigned DATA_CE_b = 14;       // pico pin 19
//static const unsigned CE1_b = 13;            // pico pin 17, CE1 SPI
static const unsigned RD_GPIO_INPUTS_b = 21; // pico pin 27
//static const unsigned DIO_CS_b = 14; // pico pin 19
static const unsigned MOSI = 19; // pico pin 25
static const unsigned MOSI1 = 11; // pico pin 15
static const unsigned MISO = 16; // pico pin 21
static const unsigned MISO1 = 12; // pico pin 16
static const unsigned DRDY_b = 20;
static const unsigned ADC_MUX_1 = 20;
static const unsigned ADC_MUX_2 = 21;
static const unsigned GPIO_20 = 20;
static const unsigned GPIO_21 = 21;
static const unsigned SCK = 18;  // pico pin 24
static const unsigned SCK1 = 10;  // pico pin 14
static const unsigned UART1_TXD = 8; // pico pin 11
static const unsigned UART1_RXD = 9; // pico pin 12
// static const unsigned powerLED = 22; // pico pin 29
//static const unsigned SYNC_AB_b = 6; // pico pin 9
static const unsigned ADC_b = 15; // gp15, pico pin 20
static const unsigned DAC_CS_8552_b = 15; // for the v6 and later boards, the ADC is I2C.
static const unsigned DAC_CS_S1_b = 15; // pico pin 20
static const unsigned DAC_CS_S2_b = 13; // pico pin 17
// static const unsigned V_3V3 = 26; // pico pin 31
// static const unsigned V_5V = 27; // pico pin 32
static const unsigned CS0_b = 17; // pico pin 22
static const unsigned DAC_CS_b2 = 13;
static const unsigned DAC_CS_b = 15;
static const unsigned I2C_SDA1 = 2; 
static const unsigned I2C_SCL1 = 3;
static const unsigned KSMU_b = 3; // pico pin 5, controls relays
static const unsigned PGA1_b = 28; // pico pin 34
static const unsigned MUX_508_A0 = 6; // pico pin 9     was //12; // pico pin 16
static const unsigned MUX_508_A1 = 14; // pico pin 19
static const unsigned MUX_508_A2 = 28; // pico pin 34
static const unsigned I_Vb_S1 = 20;
static const unsigned I_Vb_S2 = 21;
static const unsigned V_5V_b = 7;  // pico pin 10

static const unsigned MODEL = 100;
static const unsigned INIT_SMU_RELAYS = 101;
static const unsigned INIT_DAC80004 = 102;
static const unsigned INIT_ADS1259_ADC = 103;
static const unsigned INIT_PGA280 = 104;
static const unsigned WRITE_SMUA_RELAY = 105;
static const unsigned WRITE_SMUB_RELAY = 106;
static const unsigned WRITE_DAC80004 = 107;
static const unsigned WRITE_PGA280 = 108;
static const unsigned CONVERT_ADS1259 = 109;
static const unsigned READ_ADS1259 = 110;
static const unsigned INIT_USER_SPI = 112;
static const unsigned WRITE_USER_SPI = 113;
static const unsigned READ_USER_SPI = 114;
static const unsigned I2C_INIT = 115;
static const unsigned I2C_WRITE = 116;
static const unsigned I2C_READ = 117;
static const unsigned I2C_WRITE_READ = 118;
static const unsigned I2C_WRITE_BEFORE_READ = 119;
static const unsigned WRITE_TXD = 120;
static const unsigned READ_RXD = 121;
static const unsigned INIT_USER_UART = 122;
static const unsigned RW_USER_SPI = 123;
static const unsigned RD_OD_BIT8 = 126;
static const unsigned PING = 128;
static const unsigned I2C_ERROR = 130;
static const unsigned DEINIT_USER_UART = 132;
static const unsigned INIT_23S17 = 139;
static const unsigned INIT_USER_GPIO = 140;
static const unsigned WR_GPIO_DIR = 141;
static const unsigned WR_GPIO_OUTPUTS = 142;
static const unsigned RD_USER_GPIO_INPUTS = 143;
static const unsigned WR_USER_GPIO = 144;
static const unsigned SET_MUX_508 = 150;
static const unsigned SET_FORCE_CLAMP_SMU1 = 151;
static const unsigned SET_FORCE_CLAMP_SMU2 = 152;
static const unsigned SET_RANGE_RELAYS_SMU1 = 153;
static const unsigned SET_RANGE_RELAYS_SMU2 = 154;
static const unsigned SET_DIO_9557 = 155;
static const unsigned INIT_9557 = 156;
static const unsigned SET_VCC = 177;
static const unsigned INIT_ANALOG = 201;
static const unsigned DAC80004 = 202;
static const unsigned GET_CAL_CONSTANTS = 203;
static const unsigned SET_MUX = 212;
static const unsigned SET_MUX_3157 = 213;
//static const unsigned WRITE_DAC_8552 = 223;
static const unsigned WRITE_DAC_SMU1 = 224;
static const unsigned WRITE_DAC_SMU2 = 225;
static const unsigned WRITE_ADS1115 = 250;
static const unsigned CONVERT_ADS1115 = 252;
static const unsigned READ_ADS1115 = 255;
static int S1_VFO = 1049; // 0.032 * 32k
static int S1_VFG = 1;

uint8_t *model(uint8_t *buf);
//void init_RELAYS(void);
void init_PICO_GPIO(void);
void init_PICO_I2C(void);
void init_PICO_SPI(void);
void blink_it(uint16_t msec);
void write_SPI(uint8_t *buf);
void write_SPI_Mode1(uint8_t *buf);
//uint8_t *init_23S17(uint8_t *buf);
uint init_USER_SPI(uint8_t *buf);
uint8_t *init_USER_I2C(uint8_t *buf);
void init_USER_GPIO(uint8_t *buf);
uint8_t *write_USER_SPI(uint8_t *buf);
uint8_t *read_USER_SPI(uint8_t *buf);
uint8_t *write_read_USER_SPI(uint8_t *buf);
uint8_t *wr_rd_USER_SPI(uint8_t *buf);
uint8_t *write_I2C(uint8_t *buf);
uint8_t *write_I2C(uint8_t *buf);
void write_before_read_I2C(uint8_t *buf);
uint8_t *read_I2C(uint8_t *buf);
uint8_t *get_i2c_error_code(void);
uint8_t *write_read_I2C(uint8_t *buf);
uint8_t write_USER_GPIO(uint8_t *buf);
void write_GPIO_DIR(uint8_t *buf);
void write_GPIO_OUTPUTS(uint8_t *buf);
uint8_t *read_USER_GPIO(uint8_t *buf);
uint8_t *init_USER_UART(uint8_t *buf);
void write_TXD(uint8_t *buf);
uint8_t *read_RXD(uint8_t *buf);
uint8_t *ping_it(void);
void init_PICO_UART(void);
void init_ANALOG(void);  //uint8_t *buf);
//void write_SMUA_RELAY(uint8_t *buf); 
//void write_SMUB_RELAY(uint8_t *buf); 
void write_DAC80004(uint8_t *buf);
void init_DAC8552(void);
//void write_DAC_1(uint8_t *buf); // was commented out
//void write_DAC_2(uint8_t *buf); // was commented out
//uint8_t *init_DAC8411(void);
//uint8_t *init_ADS1115(void);
//uint8_t *write_DAC_8411(uint8_t *buf);
//uint8_t write_DAC_8552(uint8_t *buf);
void write_DAC_SMU1(uint8_t *buf);
void write_DAC_SMU2(uint8_t *buf);
void write_ADS1115(uint8_t *buf);
void start_ADS1115(uint8_t *buf);
// void set_adc_multiplexer(uint8_t *buf);
uint8_t *read_ADS1115(uint8_t *buf);
// void start_conversion_ADC(void); 
//void set_mux(uint8_t *buf);
void set_mux_508(uint8_t *buf);
//void set_mux_3157(uint8_t *buf);
void set_force_clamp_SMU1(uint8_t *buf);
void set_force_clamp_SMU2(uint8_t *buf);
//uint8_t *read_ADC(void); 
//---------------------------------------------------------------------    
//static const unsigned INIT_PGA = 50;
//void init_DAC80004(void);
void init_SMU_RELAYS(void);
//void init_DAC8562_DAC(void); // was commented out
//void init_ADS1259_ADC(uint8_t *buf);
//void write_280(uint8_t reg_addr, uint8_t data);
//void init_PGA280(uint8_t *buf);
//void write_PGA280(uint8_t *buf);
//void convert_ADS1259(uint8_t *buf);
//uint8_t *read_ADS1259(uint8_t *buf);
void set_range_relays_smu1(uint8_t *buf);
void set_range_relays_smu2(uint8_t *buf);
void set_dio_9557(uint8_t *buf);
void init_9557(void);
void set_VCC(uint8_t *buf);
uint8_t *get_cal_constants(int8_t *destination_buf);