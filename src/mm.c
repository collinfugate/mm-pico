#include "mm.h"
#include <math.h>
#if MM_WIFI
#include "wifi.h"
#endif


uint8_t uart_rd_buf[32] = {0};
uint8_t spi_out_buf[SPI_BUF_LEN] = {0}; // len=3
uint8_t out_buf4[4] = {0};
uint8_t spi_in_buf[SPI_IN_BUF_LEN] = {0};

uint8_t buf_len = 2;
uint8_t spi_out[32] = {0};
uint8_t temp[2] = {0};
uint quarter_cycle_delay = 1;
static uint8_t i2c_error_code[1] = {0};
uint8_t Tx_buffer_nbChar = 0;
uint8_t Tx_buffer[32] = {0};
static int chars_rxed = 0;
int count=0;
//uint8_t io_direction_port_a = 0xff;  // register address = 0 for port a
//uint8_t output_data_port_a = 0;  // register 0x12 (a write to 0x12 automatically updates OLAT 0x14)
uint8_t cal_constants[32] = {0};

void init_PICO_GPIO(void)         // setup PICO's IO pins 
{ 
    stdio_init_all();
    
//    gpio_init(DIO_CS_b); gpio_set_dir(DIO_CS_b, GPIO_OUT);
    gpio_init(SCK);  gpio_set_dir(SCK, GPIO_OUT);
    gpio_init(MISO);  gpio_set_dir(MISO, GPIO_IN);
    gpio_init(MOSI);  gpio_set_dir(MOSI, GPIO_OUT);
    gpio_init(CS0_b);  gpio_set_dir(CS0_b, GPIO_OUT);
    gpio_init(DAC_CS_b); gpio_set_dir(DAC_CS_b, GPIO_OUT);
    gpio_init(DAC_CS_b2); gpio_set_dir(DAC_CS_b2, GPIO_OUT);
    gpio_init(DRDY_b); gpio_set_dir(DRDY_b, GPIO_IN);
    //gpio_init(ADC_MUX_1); gpio_set_dir(ADC_MUX_1, GPIO_OUT);
    //gpio_init(ADC_MUX_2); gpio_set_dir(ADC_MUX_2, GPIO_OUT);
    gpio_init(GPIO_20); gpio_set_dir(GPIO_20, GPIO_OUT);
    gpio_init(GPIO_21); gpio_set_dir(GPIO_21, GPIO_OUT);
//    gpio_init(SYNC_AB_b); gpio_set_dir(SYNC_AB_b, GPIO_OUT);
    gpio_init(KSMU_b); gpio_set_dir(KSMU_b, GPIO_OUT);
//    gpio_init(PGA1_b); gpio_set_dir(PGA1_b, GPIO_OUT);
    gpio_init(MUX_508_A0); gpio_set_dir(MUX_508_A0, GPIO_OUT);
    gpio_init(MUX_508_A1); gpio_set_dir(MUX_508_A1, GPIO_OUT);
    gpio_init(MUX_508_A2); gpio_set_dir(MUX_508_A2, GPIO_OUT);
    gpio_put(MUX_508_A0, 0);
    gpio_put(MUX_508_A1, 0);
    gpio_put(MUX_508_A2, 0);
//    gpio_put(PGA1_b, 1); // set the active-low chip select for the PGA280 to high
    gpio_put(KSMU_b, 1); // set the active-low chip select for the relays to high
//    gpio_put(DIO_CS_b, 1);  // set the active-low chip selects to high
    gpio_put(CS0_b, 1);
//    gpio_put(SYNC_AB_b, 1);
    gpio_put(DAC_CS_b, 1); 
    gpio_put(DAC_CS_b2, 1); 
    gpio_put(GPIO_20, 0); 
    gpio_put(GPIO_21, 0); 
    gpio_init(ADC_b); gpio_set_dir(ADC_b, GPIO_OUT);
    gpio_put(ADC_b,1);
    gpio_init(V_5V_b); gpio_set_dir(V_5V_b, GPIO_OUT);
    gpio_put(V_5V_b, 1);   
    
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART1_TXD, GPIO_FUNC_UART);
    gpio_set_function(UART1_RXD, GPIO_FUNC_UART);
    gpio_set_function(SCK,IO_BANK0_GPIO18_CTRL_FUNCSEL_VALUE_SPI0_SCLK);
    gpio_set_function(MOSI,IO_BANK0_GPIO19_CTRL_FUNCSEL_VALUE_SPI0_TX);
    gpio_set_function(MISO,IO_BANK0_GPIO16_CTRL_FUNCSEL_VALUE_SPI0_RX);
    gpio_set_function(SCK1,IO_BANK0_GPIO10_CTRL_FUNCSEL_VALUE_SPI1_SCLK);
    gpio_set_function(MOSI1,IO_BANK0_GPIO11_CTRL_FUNCSEL_VALUE_SPI1_TX);
    gpio_set_function(MISO1,IO_BANK0_GPIO12_CTRL_FUNCSEL_VALUE_SPI1_RX);
    gpio_set_function(PICO_DEFAULT_I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(PICO_DEFAULT_I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(PICO_DEFAULT_I2C_SDA_PIN);
    gpio_pull_up(PICO_DEFAULT_I2C_SCL_PIN);

    gpio_set_function(I2C_SDA1, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL1, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA1);
    gpio_pull_up(I2C_SCL1);
    bi_decl(bi_2pins_with_func(PICO_DEFAULT_I2C_SDA_PIN, PICO_DEFAULT_I2C_SCL_PIN, GPIO_FUNC_I2C));   // Make the I2C pins available to picotool
}


uint8_t *model(uint8_t *buf)
{
    static uint8_t model_num[1] = {MODEL_NUMBER};
    return model_num;
}

void init_PICO_I2C(void) {
    i2c_init(I2C_ID, 400000);
    i2c_init(I2C_ID1, 400000);
}

void init_PICO_SPI(void) {      
    spi_init(SPI_ID, 1000000);  // Enable SPI 0 at 1 MHz and connect to GPIOs
    spi_set_format(SPI_ID, 8, 0, 0, SPI_MSB_FIRST);
}

void init_ANALOG(void)
{
    spi_init(SPI_ID1, 1000000); // initialize spi_id1 to 1M clock
    gpio_put(GPIO_20, 1);  // voltage source for smu1
    gpio_put(GPIO_21, 1);  // voltage source for smu2
    init_9557();
    init_SMU_RELAYS();
    init_DAC8552();
}

void pico_led_init(void) {
#ifdef PICO_DEFAULT_LED_PIN
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
#endif
}

void pico_led_put(bool on) {
#if defined(PICO_DEFAULT_LED_PIN)
    gpio_put(PICO_DEFAULT_LED_PIN, on);
#elif MM_WIFI
    wifi_led_put(on);
#endif
}

void blink_it(uint16_t msec) {
    const uint fp_LED = 22;
    gpio_init(fp_LED);
    gpio_set_dir(fp_LED, GPIO_OUT);
    for (int i=0; i<8; i++) {
        pico_led_put(1);
        gpio_put(fp_LED, 1);
        sleep_ms(msec);
        gpio_put(fp_LED, 0);
        pico_led_put(0);
        sleep_ms(msec);
    }
    pico_led_put(1); // make sure it's on when you leave this function
    gpio_put(fp_LED, 1);
}

// -------------- GPIO functions -----------------

uint8_t reverse_bits_loop(uint8_t b) {
    unsigned char r = 0;
    for (int i = 0; i < 8; i++) {
        r = (r << 1) | (b & 1);
        b >>= 1;
    }
    return r;
}

void init_USER_GPIO(uint8_t *buf)
{
    uint8_t i2c_addr_7bit = 0x19;  // PCA9557 address, with A0 high, A1 and A2 low ... 
    uint8_t len = 2;
    uint8_t user_out[2];
    user_out[0] = 0x02;  // polarity port
    user_out[1] = 0x00;  // no polarity inversion
    i2c_write_blocking(I2C_ID1, i2c_addr_7bit, user_out, len, false);    
    sleep_us(200);   
}

uint8_t *read_USER_GPIO(uint8_t *buf) 
{
//    uint8_t direction = buf[0];
//    uint8_t gpio_data = buf[1];
    static int8_t user_out[1] = {0};  // read register = 0x00
    static uint8_t user_in[1] = {0};
    uint8_t addr_7bit = 0x19;  // PCA9557 address, with A0 high, A1 and A2 low
    uint8_t num_read_bytes = 1;
//    uint8_t write_byte = 0x00; // register address for input port
//    static uint8_t user_in[32]= {0};
    uint8_t len = 1;
    i2c_write_blocking(I2C_ID1, addr_7bit, user_out, len, true);  // true = no stop bit    
    i2c_error_code[0] = i2c_read_timeout_us(I2C_ID1, addr_7bit, user_in, num_read_bytes, false, 50000); // 50 msec
//    if (i2c_error_code[0] < 0) {
//        for (int i=0; i<num_read_bytes; i++) { user_in[i] = 0; }
//        user_in[0] = i2c_error_code[0]; // new
//        num_read_bytes = 1; // new
//    }
    user_in[0] = reverse_bits_loop(user_in[0]); // reverse the bits in the byte read from the 9557
    return user_in;

//    return gpio_in;
}

uint8_t write_USER_GPIO(uint8_t *buf) {
    uint8_t gpio_direction = buf[1]; // first byte is direction for the 9557
    uint8_t gpio_data = buf[2]; // second byte is the data to write to the GPIO
    uint8_t i2c_addr_7bit = 0x19;  // PCA9557 address, with A0 high, A1 and A2 low ... 
    uint8_t len = 2;
    gpio_direction = reverse_bits_loop(gpio_direction);
    gpio_data = reverse_bits_loop(gpio_data);
    uint8_t user_out[2];    // this array has only 2 elements
    user_out[0] = 0x03;  // configuration port
    user_out[1] = gpio_direction;
    i2c_write_blocking(I2C_ID1, i2c_addr_7bit, user_out, len, false);    
    sleep_us(200);   
    user_out[0] = 0x01;  // output port
    user_out[1] = gpio_data;        // all outputs
    i2c_write_blocking(I2C_ID1, i2c_addr_7bit, user_out, len, false);   
    sleep_us(200);
    return 0;
}

void write_GPIO_DIR(uint8_t *buf)
{
    uint8_t gpio_direction = buf[1]; // first byte is direction for the 9557
    uint8_t i2c_addr_7bit = 0x19;  // PCA9557 address, with A0 high, A1 and A2 low ... 
    uint8_t len = 2;
    gpio_direction = reverse_bits_loop(gpio_direction);
    uint8_t user_out[2];    // this array has only 2 elements
    user_out[0] = 0x03;  // configuration port
    user_out[1] = gpio_direction;
    i2c_write_blocking(I2C_ID1, i2c_addr_7bit, user_out, len, false);    
    sleep_us(200);
}

void write_GPIO_OUTPUTS(uint8_t *buf)
{
    uint8_t gpio_data = buf[1]; // first byte is the data to write to the GPIO
    uint8_t i2c_addr_7bit = 0x19;  // PCA9557 address, with A0 high, A1 and A2 low ... 
    uint8_t len = 2;
    gpio_data = reverse_bits_loop(gpio_data);
    uint8_t user_out[2];    // this array has only 2 elements
    user_out[0] = 0x01;  // output port
    user_out[1] = gpio_data;
    i2c_write_blocking(I2C_ID1, i2c_addr_7bit, user_out, len, false);   
    sleep_us(200);
}

//---------------------- SPI functions -----------------

void write_SPI(uint8_t *buf) {
    uint8_t port_pin = buf[0]; // first byte is the pico pin used for chip_select_bar (CS/)
    uint8_t num_data_bytes = (buf[1]-1);
    uint8_t i=0;
    uint8_t j=0; 
    uint8_t k=0;
    spi_init(SPI_ID, 1000000);
    spi_set_format(SPI_ID, 8, 0, 0, SPI_MSB_FIRST); // Mode 0
    if (num_data_bytes==1) {
        spi_out_buf[0] = buf[2];
        gpio_put(port_pin,0); // lower CS/
        spi_write_read_blocking(SPI_ID, spi_out_buf, spi_in_buf, 1); // 1 byte
        sleep_us(2);
        gpio_put(port_pin,1); // release CS/
    }
    else if (num_data_bytes==2) {
        spi_out_buf[0] = buf[2];
        spi_out_buf[1] = buf[3];
        gpio_put(port_pin,0); // lower CS/
        spi_write_read_blocking(SPI_ID, spi_out_buf, spi_in_buf, 2); // 2 bytes
        sleep_us(2);
        gpio_put(port_pin,1); // release CS/
    }
    else {
        for (k=0; k<10; k++) {
            for (i=0; i<3; i++) {// 3 bytes for each spi write, excludes port_pin and num_bytes, but includes address byte and 2 data bytes
                j = 3*k + i + 2; // i=0,k=0 gives 2 ... i=2,k=7 gives 25 
                spi_out_buf[i] = buf[j]; // spi buf is only 3 bytes, uart has 35.  
                }
            gpio_put(port_pin,0); // lower CS/
            spi_write_read_blocking(SPI_ID, spi_out_buf, spi_in_buf, 3); // 3 bytes
            sleep_us(2);
            gpio_put(port_pin,1); // release CS/
            sleep_us(2);
            if (j>=(num_data_bytes+1)) {k=10;} // don't waste time sending nothing to nowhere.
        }
    }
}

void write_SPI_Mode1(uint8_t *buf) {
    uint8_t port_pin = buf[0]; // first byte is the pico pin used for chip_select_bar (CS/)
    uint8_t num_data_bytes = (buf[1]-1);
    uint8_t i=0;
    uint8_t j=0; 
    uint8_t k=0;
    spi_init(SPI_ID, 1000000);
    spi_set_format(SPI_ID, 8, 0, 1, SPI_MSB_FIRST); // Use SPI Mode 1 for this function
    if (num_data_bytes==1) {
        spi_out_buf[0] = buf[2];
        gpio_put(port_pin,0); // lower CS/
        spi_write_read_blocking(SPI_ID, spi_out_buf, spi_in_buf, 1); // 1 byte
        sleep_us(2);
        gpio_put(port_pin,1); // release CS/
    }
    else if (num_data_bytes==2) {
        spi_out_buf[0] = buf[2];
        spi_out_buf[1] = buf[3];
        gpio_put(port_pin,0); // lower CS/
        spi_write_read_blocking(SPI_ID, spi_out_buf, spi_in_buf, 2); // 2 bytes
        sleep_us(2);
        gpio_put(port_pin,1); // release CS/
    }
    else {
        for (k=0; k<10; k++) {
            for (i=0; i<3; i++) {// 3 bytes for each spi write, excludes port_pin and num_bytes, but includes address byte and 2 data bytes
                j = 3*k + i + 2; // i=0,k=0 gives 2 ... i=2,k=7 gives 25 
                spi_out_buf[i] = buf[j]; // spi buf is only 3 bytes, uart has more.  
                }
            gpio_put(port_pin,0); // lower CS/
            spi_write_read_blocking(SPI_ID, spi_out_buf, spi_in_buf, 3); // 3 bytes
            sleep_us(2);
            gpio_put(port_pin,1); // release CS/
            sleep_us(2);
            if (j>=(num_data_bytes+1)) {k=10;} // don't waste time sending nothing to nowhere.
        }
    }
    spi_set_format(SPI_ID, 8, 0, 0, SPI_MSB_FIRST); // reset to default Mode 0
}

uint init_USER_SPI(uint8_t *buf)
{
    uint baudrate = (buf[3] << 16) | (buf[2] << 8) | buf[1]; 
    uint8_t mode = buf[4];
    uint8_t a, b = 0;
    if (mode==0) { a=0; b=0; }
    if (mode==1) { a=0; b=1; }
    if (mode==2) { a=1; b=0; }
    if (mode==3) { a=1; b=1; }
    uint baud = spi_init(SPI_ID, baudrate);
    spi_set_format(SPI_ID, 8, a, b, SPI_MSB_FIRST); // set the SPI Mode   
    float quarter_cycle = 1 / baud / 4;
    quarter_cycle_delay = quarter_cycle * 1e6;
    if (quarter_cycle_delay < 1) quarter_cycle_delay = 1;
    return quarter_cycle_delay;
}

uint8_t *init_USER_I2C(uint8_t *buf)
{
    uint baudrate = (buf[3] << 16) | (buf[2] << 8) | buf[1]; 
    i2c_error_code[0] = i2c_set_baudrate(I2C_ID, baudrate);   
    return i2c_error_code;
}

uint8_t spi_wrt_user(uint8_t port_pin, uint8_t *out_buf)
{
    spi_set_format(SPI_ID, 8, 0, 0, SPI_MSB_FIRST); // set the SPI Mode   
    gpio_put(port_pin, 0); // lower cs/
    static uint8_t return_code = 0;
    return_code = spi_write_blocking(SPI_ID, out_buf, sizeof(out_buf));
    sleep_us(quarter_cycle_delay);
    gpio_put(port_pin, 1);
    return return_code;
}

uint8_t spi_wrt(uint8_t port_pin, uint8_t *out_buf)
{
    spi_set_format(SPI_ID1, 8, 0, 0, SPI_MSB_FIRST); // set the SPI Mode   
    sleep_us(quarter_cycle_delay);
    gpio_put(port_pin, 0); // lower cs/
    static uint8_t return_code = 0;
    return_code = spi_write_blocking(SPI_ID1, out_buf, 3); //sizeof(out_buf));
    sleep_us(quarter_cycle_delay);
    gpio_put(port_pin, 1);
    return return_code;
}


uint8_t *write_USER_SPI(uint8_t *buf)
{
    uint8_t port_pin = CS0_b; //CE1_b; //uart_rd_buf[0] - 100; // 13 ... CE1_b
    uint8_t num_data_bytes = buf[1];
    uint8_t user_out[num_data_bytes];
    uint8_t user_in[num_data_bytes];
    static uint8_t return_code[1] = {0};
    for (int i=0; i<num_data_bytes; i++) {
        user_out[i] = buf[i+2]; }     // uart_rd_buf[i+2]; }
//    spi_wrt_user(port_pin, buf);   
    gpio_put(port_pin,0);
    return_code[0] = spi_write_read_blocking(SPI_ID, user_out, user_in, num_data_bytes);
    sleep_us(quarter_cycle_delay);
    gpio_put(port_pin,1);
    sleep_us(2);
    return return_code;
}

uint8_t *read_USER_SPI(uint8_t *buf)
{
    uint8_t port_pin = CS0_b; //CE1_b; //uart_rd_buf[0] - 101; // 13 ... CE1_b
    uint8_t num_data_bytes = buf[1];
    int i = 0;
    uint8_t user_out[num_data_bytes];
    static uint8_t user_in[32] = {0}; //num_data_bytes];
    for (i=0; i<num_data_bytes; i++) {
        user_out[i] = buf[i+2]; }
//    gpio_put(USER_MISO_OE_b,0);    
    gpio_put(port_pin,0);
    sleep_us(20);
    spi_write_read_blocking(SPI_ID, user_out, user_in, num_data_bytes);
    sleep_us(20);
    gpio_put(port_pin,1);
//    gpio_put(USER_MISO_OE_b,1);
    sleep_us(20);
    return user_in;
    //uart_write_blocking(UART_ID, user_in, num_data_bytes); // write back to PC via uart
}

uint8_t *write_read_USER_SPI(uint8_t *buf)
{
    uint8_t port_pin = CS0_b; // 
    uint8_t num_bytes = buf[1];
    int i = 0;
    uint8_t user_out[num_bytes];
//    static uint8_t user_in[32] = {0};
    static uint8_t user_in_buf[32] = {0};
    uint8_t user_in[num_bytes];  // was static, but if num_bytes is less than 32, then the return data will be wrong. So make it non-static and the right size.
    for (i=0; i<num_bytes; i++) {
        user_out[i] = buf[i+2]; }
//    gpio_put(USER_MISO_OE_b,0);    
    gpio_put(port_pin,0);
    spi_write_read_blocking(SPI_ID, user_out, user_in, num_bytes);
    gpio_put(port_pin,1);
//    gpio_put(USER_MISO_OE_b,1);
    sleep_us(2);
    for (i=0; i<num_bytes; i++) {
        user_in_buf[i] = user_in[i]; } // copy to static buffer for return, since user_in is not static 
    return user_in_buf;
    // uart_write_blocking(UART_ID, user_in, num_bytes); // write back to PC via uart
}

uint8_t *wr_rd_USER_SPI(uint8_t *buf)
{
    uint8_t port_pin = CS0_b; // 
//    uint8_t num_wr_bytes = buf[1];
//    uint8_t num_rd_bytes = buf[2];
    int i = 0;
    uint8_t read_offset = buf[1];
    uint8_t num_bytes = buf[1] + buf[2];//num_wr_bytes + num_rd_bytes;
    uint8_t user_out[num_bytes];
//    static uint8_t user_in[32] = {0};
    static uint8_t user_in_buf[32] = {0};
    uint8_t user_in[num_bytes];  // was static, but if num_bytes is less than 32, then the return data will be wrong. So make it non-static and the right size.
    for (i=0; i<num_bytes; i++) {
        user_out[i] = buf[i+3]; }
//    gpio_put(USER_MISO_OE_b,0);    
    gpio_put(port_pin,0);
    spi_write_read_blocking(SPI_ID, user_out, user_in, num_bytes);
    gpio_put(port_pin,1);
//    gpio_put(USER_MISO_OE_b,1);
    sleep_us(2);
    
    for (i=0; i<num_bytes+1; i++) {
        user_in_buf[i] = user_in[i+read_offset]; } // copy to static buffer for return, since user_in is not static 
    return user_in_buf;
    // uart_write_blocking(UART_ID, user_in, num_bytes); // write back to PC via uart
}

uint8_t *write_I2C(uint8_t *buf)
{
    uint8_t addr_7bit = buf[1];  //uart_rd_buf[1];
    uint8_t num_data_bytes = buf[2];
    uint8_t user_out[num_data_bytes+1];
    for (int i=0; i<num_data_bytes; i++) {
        user_out[i] = buf[i+3]; }
    i2c_error_code[0] = i2c_write_blocking(I2C_ID, addr_7bit, user_out, num_data_bytes, false);
    return i2c_error_code;
}

void write_before_read_I2C(uint8_t *buf)
{
    uint8_t addr_7bit = buf[1];
    uint8_t num_data_bytes = buf[2];
    uint8_t user_out[num_data_bytes+1];
    for (int i=0; i<num_data_bytes; i++) {
        user_out[i] = buf[i+3]; }
    i2c_write_blocking(I2C_ID, addr_7bit, user_out, num_data_bytes, true);
}

uint8_t *read_I2C(uint8_t *buf)
{
    uint8_t addr_7bit = buf[1];
    uint8_t num_read_bytes = buf[2];
    static uint8_t user_in[32]= {0};
    i2c_error_code[0] = i2c_read_timeout_us(I2C_ID, addr_7bit, user_in, num_read_bytes, false, 50000); // 50 msec, was 1000 usec
    if (i2c_error_code[0] < 0) {
        for (int i=0; i<num_read_bytes; i++) { user_in[i] = 0; }
        user_in[0] = i2c_error_code[0]; // new
        num_read_bytes = 1; // new
    }
    return user_in;
    //uart_write_blocking(UART_ID, user_in, num_read_bytes); // write back to PC via uart
}

uint8_t *get_i2c_error_code(void) 
{
    uint8_t error_code = 0;
    static uint8_t send_it[1] = {0};
    if (i2c_error_code[0] < 0) error_code = -i2c_error_code[0]; // error codes are negative, but send a uint8_t
    send_it[0] = error_code;
    return send_it;
    // uart_write_blocking(UART_ID, send_it, 1);
}

uint8_t *write_read_I2C(uint8_t *buf) {
    uint8_t addr_7bit = buf[1];
    uint8_t num_write_bytes = buf[2];
    uint8_t num_read_bytes = buf[3];    
    uint8_t user_out[num_write_bytes+1];
    static uint8_t user_in[32];  // receive data
    for (int i=0; i<num_write_bytes; i++) {
        user_out[i] = buf[i+4]; }
    i2c_write_blocking(I2C_ID, addr_7bit, user_out, num_write_bytes, true); // true to keep master control of bus
//    i2c_read_blocking(I2C_ID, addr_7bit, user_in, num_read_bytes, false);
    i2c_read_timeout_us(I2C_ID, addr_7bit, user_in, num_read_bytes, false, 1000);
    return user_in;
    // uart_write_blocking(UART_ID, user_in, num_read_bytes); // write back to PC via uart
}


uint8_t *init_USER_UART(uint8_t *buf) {
    uint baudrate = (buf[3] << 16) | (buf[2] << 8) | buf[1]; 
    uint data_bits = buf[4];   
    uint stop_bits = buf[5];   
    uint parity = buf[6];   
    static uint8_t return_code[1] = {0};
    return_code[0] = uart_init(USER_UART_ID, baudrate);
    uart_set_hw_flow(USER_UART_ID, false, false);    // Set UART flow control CTS/RTS, we don't want these, so turn them off
    uart_set_format(USER_UART_ID, data_bits, stop_bits, parity);    // Set our data format
    uart_set_fifo_enabled(USER_UART_ID, true);    // Turn on FIFO's    
    return return_code;
}

void write_TXD(uint8_t *buf) {
    uint8_t txd_out[32] = {0};
    uint8_t num_write_bytes = buf[1];
    for (int i=0; i<num_write_bytes; i++) {
        txd_out[i] = buf[i+2]; }
    uart_write_blocking(USER_UART_ID, txd_out, num_write_bytes); // write to user TXD pin on front panel
}

uint8_t *read_RXD(uint8_t *buf) {
    uint8_t ch;
    static uint8_t rxd_in[32] = {0};
    uint8_t num_read_bytes = buf[1];
    while (uart_is_readable(USER_UART_ID) && (chars_rxed < num_read_bytes))
    {
        ch = uart_getc(USER_UART_ID);
        rxd_in[chars_rxed] = ch;
        chars_rxed++;
    }
    if (chars_rxed < num_read_bytes) {
        for (int i=chars_rxed; i<num_read_bytes; i++) {
            rxd_in[i] = 0; } // fill the rest of the buffer with zeros
//        chars_rxed = num_read_bytes; // don't return more than the requested number of bytes
    }
//    rxd_in[1] = chars_rxed;
    chars_rxed = 0; // reset
    return rxd_in;
}

uint8_t *ping_it(void)
{
    static uint8_t read_buffer[4];
//    blink_it(100);
    read_buffer[0] = 0x55;
    read_buffer[1] = 0xaa;
    read_buffer[2] = 0x00;
    read_buffer[3] = 0xff;
//    uart_write_blocking(UART_ID, read_buffer, 4); // write back to PC via uart
    return read_buffer;
}

//---------------------------------------------------------------------    
void init_SMU_RELAYS(void)
{
    uint8_t buffer[2] = {0x00, 0x77}; // (10kohms each SMU))
    set_range_relays_smu1(buffer);
    set_range_relays_smu2(buffer);
}

void init_DAC8552(void)
{
    uint8_t buff[4] = {0, 16, 128, 0};
    write_DAC_SMU1(buff);
    write_DAC_SMU2(buff);
//    buff[1] = 36; buff[2] = 10; buff[3] = 61;
    buff[1] = 36; buff[2] = 204; buff[3] = 204;
    write_DAC_SMU1(buff);
    write_DAC_SMU2(buff);
}

void write_ADS1115(uint8_t *buf)
{
    // buf[1] is the 7-bit, no lsb write address
    // buf[2] is the pointer to configuration register (= 0x01)
    // buf[3] is the MSB of the config reg
    // buf[4] is the LSB of the config reg
    uint8_t addr_7bit = buf[1];  //uart_rd_buf[1];
    uint8_t num_data_bytes = buf[2];
    buf[2] = 1; // configuration register address = 1
    uint8_t user_out[num_data_bytes];
    for (int i=0; i<num_data_bytes; i++) {
        user_out[i] = buf[i+2]; }
    i2c_write_blocking(I2C_ID1, addr_7bit, user_out, num_data_bytes, false);
}

uint8_t *read_ADS1115(uint8_t *buf)
{
    uint8_t i2c_addr_7bit = buf[1];
//    static uint8_t write_buffer[1] = {0};
    static uint8_t read_buffer[2] = {0};
//    i2c_write_blocking(I2C_ID1, i2c_addr_7bit, write_buffer, 1, true);
    i2c_read_blocking(I2C_ID1, i2c_addr_7bit, read_buffer, 2, false);  // example says 0x91, MSB, LSB
    return read_buffer;
}

void start_ADS1115(uint8_t *buf)
{
    uint8_t len = 1;
    uint8_t user_out[1];  // this array has only 1 element
    user_out[0] = 0;  // the one element is zero (address of conversion register)
    uint8_t i2c_addr_7bit = buf[1];
    i2c_write_blocking(I2C_ID1, i2c_addr_7bit, user_out, len, false);       
}


void write_DAC_SMU1(uint8_t *buf)  // 8552 for SMU1
{
//    static uint8_t return_code[1] = {0};
    spi_set_format(SPI_ID1, 8, 1, 0, SPI_MSB_FIRST); // pol = 1
    spi_out[0] = buf[1]; // power-down bits are 00.
    spi_out[1] = buf[2]; //+ (buffer3 >> 2);        // ((buf[2] << 6) | (buf[3] >> 2));  //  d9, d8, d7, ... d2
    spi_out[2] = buf[3];  // the 6 lsb's are "don't care"
    gpio_put(DAC_CS_S1_b, 0);  // lower chip select on dac
    spi_write_blocking(SPI_ID1, spi_out, 3); // 3 bytes
    sleep_us(2); // space to ensure clock and CS/ edges don't everlap
    gpio_put(DAC_CS_S1_b, 1); // release chip select
    spi_set_format(SPI_ID1, 8, 0, 0, SPI_MSB_FIRST); // pol = 0
//    return return_code;
}

void write_DAC_SMU2(uint8_t *buf)  // 8552 for SMU2
{
    //static uint8_t return_code[1] = {0};
    spi_set_format(SPI_ID1, 8, 1, 0, SPI_MSB_FIRST); // pol = 1
    spi_out[0] = buf[1]; // power-down bits are 00.
    spi_out[1] = buf[2]; //+ (buffer3 >> 2);        // ((buf[2] << 6) | (buf[3] >> 2));  //  d9, d8, d7, ... d2
    spi_out[2] = buf[3];  // the 6 lsb's are "don't care"
    gpio_put(DAC_CS_S2_b, 0);  // lower chip select on dac
    spi_write_blocking(SPI_ID1, spi_out, 3); // 3 bytes
    sleep_us(2); // space to ensure clock and CS/ edges don't everlap
    gpio_put(DAC_CS_S2_b, 1); // release chip select
    spi_set_format(SPI_ID1, 8, 0, 0, SPI_MSB_FIRST); // pol = 0
//    return return_code;
}

void set_mux_508(uint8_t *buf)
{
    uint8_t channel_address = buf[1] & 0x07; // 3 bits of buf[1] are input addresses to the dg508, Model 612
    uint8_t bit_0 = channel_address & 0x01;
    gpio_put(MUX_508_A0, bit_0);
    uint8_t bit_1 = (channel_address & 0x02) >> 1;  
    gpio_put(MUX_508_A1, bit_1);
    uint8_t bit_2 = (channel_address & 0x04) >> 2;  
    gpio_put(MUX_508_A2, bit_2);
}


void set_force_clamp_SMU1(uint8_t *buf)
{
    if (buf[1] == 0) {
        gpio_put(GPIO_20, 1);    
    }
    else {
        gpio_put(GPIO_20, 0);    
    }
}

void set_force_clamp_SMU2(uint8_t *buf)
{
    if (buf[1] == 0) {
        gpio_put(GPIO_21, 1);    
    }
    else {
        gpio_put(GPIO_21, 0);    
    }
}

void init_9557(void)
{
    uint8_t len = 2;
    uint8_t user_out[2];    // this array has only 2 elements
    user_out[0] = 0x03;  // configuration port
    user_out[1] = 0x00;        // all outputs
    uint8_t i2c_addr_7bit = 0x1A;  // PCA9557 address, with A1 high, A0 and A2 low ... 
    i2c_write_blocking(I2C_ID1, i2c_addr_7bit, user_out, len, false);    
    sleep_us(200);   
    i2c_addr_7bit = 0x1B;
    user_out[0] = 0x03;  // configuration port
    user_out[1] = 0x00;        // all outputs
    i2c_write_blocking(I2C_ID1, i2c_addr_7bit, user_out, len, false);   
    sleep_us(200);
}

void set_range_relays_smu1(uint8_t *buf)
{
    uint8_t len = 2;
    uint8_t user_out[2];    // this array has only 2 elements
    user_out[0] = 0x01;  // configuration register is the first byte (after the address byte), and 0x01 is the output port
    user_out[1] = buf[1];        // data
    uint8_t i2c_addr_7bit = 0x1A;  // PCA9557 address, with A1 high, A0 and A2 low ... 001 1010 = 0x1A (no r/w/ bit)
    i2c_write_blocking(I2C_ID1, i2c_addr_7bit, user_out, len, false);       
}

void set_range_relays_smu2(uint8_t *buf)
{
    uint8_t len = 2;
    uint8_t user_out[2];    // this array has only 2 elements
    user_out[0] = 0x01;  // configuration register is the first byte (after the address byte), and 0x01 is the output port
    user_out[1] = buf[1];        // data
    uint8_t i2c_addr_7bit = 0x1B;  // PCA9557 address, with A1 high, A0 and A2 low ... 001 1011 = 0x1B     (no r/w/ bit)
    i2c_write_blocking(I2C_ID1, i2c_addr_7bit, user_out, len, false);       
}

void set_dio_9557(uint8_t *buf)
{
    uint8_t len = 2;
    uint8_t user_out[2];    // this array has only 2 elements
    user_out[0] = 0x01;  // output port
    user_out[1] = buf[1];        // data
    uint8_t i2c_addr_7bit = 0x19;  // PCA9557 address, with A0 high ... 001 1001 = 0x19 (no r/w/ bit)
    i2c_write_blocking(I2C_ID1, i2c_addr_7bit, user_out, len, false);       
}

//uint8_t *get_cal_constants(uint8_t *buf)
//{
    // Implementation for getting calibration constants
//    return NULL;
//}

//---------------------------------------------------------------------    

void init_PICO_UART(void)
{
   // need to set :   pico_enable_stdin_uart = 0; CMake?????
    uart_init(UART_ID, BAUD_RATE);
    uart_set_hw_flow(UART_ID, false, false);    // Set UART flow control CTS/RTS, we don't want these, so turn them off
    uart_set_format(UART_ID, DATA_BITS, STOP_BITS, PARITY);    // Set our data format
//    uart_set_fifo_enabled(UART_ID, true);    // Turn on FIFO's
//    irq_set_exclusive_handler(UART0_IRQ_NUMBER, RxIntHndlr); //on_uart_rx); // set up and enable the interrupt handlers
//    uart_set_irq_enables(UART_ID, true, false);     // Now enable the UART to send interrupts - RX only
//    irq_set_enabled(UART0_IRQ_NUMBER, true);
}

void set_VCC(uint8_t *buf)
{
    gpio_put(V_5V_b, buf[1] ? 1 : 0); // 
}

uint8_t *get_cal_constants(int8_t *destination_buf) {
    // first byte is lsb, 2nd byte is msb.
    // In Python code, divide by 32767 to get the actual value, e.g. 0.032 = 1048/32767
    // Hidden entirely inside the function, persists for the whole program
    static int8_t hidden_buf[64] = {0}; // Adjust the size as needed
    static int8_t is_initialized = 0;
    static int16_t temp = 0;
    // Emulates "set_constants" automatically on the first run
    if (!is_initialized) {
        temp = round(32767 * 0.032); // V1 offset  // for 0.032, temp is 1048
        hidden_buf[0] = (int8_t)(S1_VFO & 0xFF);
        hidden_buf[1] = (int8_t)((S1_VFO >> 8) & 0xFF);   
        temp = round(32767 * 0.00029); // V1 gain - 1
        hidden_buf[2] = (int8_t)(temp & 0xFF); 
        hidden_buf[3] = (int8_t)((temp >> 8) & 0xFF);   
        temp = round(32767 * -0.0345); // V2 offset
        hidden_buf[4] = (int8_t)(temp & 0xFF); 
        hidden_buf[5] = (int8_t)((temp >> 8) & 0xFF);   
        temp = round(32767 * 0.0006); // V2 gain - 1
        hidden_buf[6] = (int8_t)(temp & 0xFF); 
        hidden_buf[7] = (int8_t)((temp >> 8) & 0xFF);   
        temp = round(32767 * 0.0); // I1 offset
        hidden_buf[8] = (int8_t)(temp & 0xFF);
        hidden_buf[9] = (int8_t)((temp >> 8) & 0xFF);   
        temp = round(32767 * 0.0); // I1 gain - 1, so 0.002 means 1.002, -0.002 is 0.998
        hidden_buf[10] = (int8_t)(temp & 0xFF); 
        hidden_buf[11] = (int8_t)((temp >> 8) & 0xFF);   
        temp = round(32767 * 0.0); // I2 offset
        hidden_buf[12] = (int8_t)(temp & 0xFF); 
        hidden_buf[13] = (int8_t)((temp >> 8) & 0xFF);   
        temp = round(32767 * 0.1); // I2 gain - 1
        hidden_buf[14] = (int8_t)(temp & 0xFF); 
        hidden_buf[15] = (int8_t)((temp >> 8) & 0xFF);   

        temp = round(32767 * 0.013); // S1_VMO
        hidden_buf[16] = (int8_t)(temp & 0xFF); 
        hidden_buf[17] = (int8_t)((temp >> 8) & 0xFF);
        temp = round(32767 * 0.9987); // S1_VMG
        hidden_buf[18] = (int8_t)(temp & 0xFF); 
        hidden_buf[19] = (int8_t)((temp >> 8) & 0xFF);
        temp = round(32767 * 0.013); // S2_VMO
        hidden_buf[20] = (int8_t)(temp & 0xFF); 
        hidden_buf[21] = (int8_t)((temp >> 8) & 0xFF);
        temp = round(32767 * 0.9987); // S2_VMG
        hidden_buf[22] = (int8_t)(temp & 0xFF); 
        hidden_buf[23] = (int8_t)((temp >> 8) & 0xFF);        

        temp = round(32767 * 0.079); // smu1 1 ohm offset, e.g. 1 - 1.001 = -0.001
        hidden_buf[24] = (int8_t)(temp & 0xFF);
        hidden_buf[25] = (int8_t)((temp >> 8) & 0xFF);   
        temp = round(32767 * 0.0); // smu1 10 ohm offset, e.g. 10 - 10.001 = -0.001
        hidden_buf[26] = (int8_t)(temp & 0xFF); 
        hidden_buf[27] = (int8_t)((temp >> 8) & 0xFF);   
        temp = round(32767 * 0.0); // smu1 100 ohm offset
        hidden_buf[28] = (int8_t)(temp & 0xFF); 
        hidden_buf[29] = (int8_t)((temp >> 8) & 0xFF);   
        temp = round(32767 * 0.0); // smu1 1k offset
        hidden_buf[30] = (int8_t)(temp & 0xFF); 
        hidden_buf[31] = (int8_t)((temp >> 8) & 0xFF);   
        temp = round(32767 * 0.0); // smu1 10k offset
        hidden_buf[32] = (int8_t)(temp & 0xFF); 
        hidden_buf[33] = (int8_t)((temp >> 8) & 0xFF);   
        temp = round(32767 * 0.079); // smu2 1 ohm offset, e.g. -0.001 means 1 - 1.001 = -0.001
        hidden_buf[34] = (int8_t)(temp & 0xFF);
        hidden_buf[35] = (int8_t)((temp >> 8) & 0xFF);   
        temp = round(32767 * 0.0); // smu2 10 ohm offset, e.g. 10 - 10.001 = -0.001
        hidden_buf[36] = (int8_t)(temp & 0xFF); 
        hidden_buf[37] = (int8_t)((temp >> 8) & 0xFF);   
        temp = round(32767 * 0.0); // smu2 100 ohm offset
        hidden_buf[38] = (int8_t)(temp & 0xFF); 
        hidden_buf[39] = (int8_t)((temp >> 8) & 0xFF);   
        temp = round(32767 * 0.0); // smu2 1k offset
        hidden_buf[40] = (int8_t)(temp & 0xFF); 
        hidden_buf[41] = (int8_t)((temp >> 8) & 0xFF);   
        temp = round(32767 * 0.0); // smu2 10k offset
        hidden_buf[42] = (int8_t)(temp & 0xFF); 
        hidden_buf[43] = (int8_t)((temp >> 8) & 0xFF);



        // ...
        is_initialized = 1;
    }

    // Copy data out
    for (int i = 0; i < 64; i++) {
        destination_buf[i] = hidden_buf[i];
    }
    return (uint8_t *)destination_buf;
}
