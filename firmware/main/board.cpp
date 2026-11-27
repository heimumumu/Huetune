// Pin routing and power rails from the Waveshare reference BSP (Apache-2.0).
#include "app.hpp"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"
#include "driver/i2s_std.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_st7796.h"
#include "esp_lvgl_port.h"
#include "esp_io_expander_tca9554.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "XPowersAXP2101.tpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <ctime>
#include <cmath>

static i2c_master_bus_handle_t bus;
static i2c_master_dev_handle_t pmu,touch,rtc;
static esp_io_expander_handle_t expander;
static XPowersAXP2101 power;
static esp_codec_dev_handle_t codec;
static QueueHandle_t beeps;
static void render_event(lv_event_t* event){
    static uint64_t began=0;
    if(lv_event_get_code(event)==LV_EVENT_RENDER_START)began=now_ms();
    else if(lv_event_get_code(event)==LV_EVENT_RENDER_READY){
        state.ui_frames++;state.ui_render_ms=now_ms()-began;
        state.ui_render_max_ms=std::max(state.ui_render_max_ms,state.ui_render_ms);
        if(state.transition.phase==mc::TrackTransition::Entering){
            state.transition_frames++;
            state.transition_render_max_ms=std::max(state.transition_render_max_ms,state.ui_render_ms);
        }
        state.transition.presented(now_ms());
        if(state.drag)state.drag_render_max_ms=std::max(state.drag_render_max_ms,state.ui_render_ms);
    }
}
static int reg_read(uint8_t,uint8_t reg,uint8_t*data,uint8_t n){return i2c_master_transmit_receive(pmu,&reg,1,data,n,100)==ESP_OK?0:-1;}
static int reg_write(uint8_t,uint8_t reg,uint8_t*data,uint8_t n){uint8_t b[256];b[0]=reg;memcpy(b+1,data,n);return i2c_master_transmit(pmu,b,n+1,100)==ESP_OK?0:-1;}
static i2c_master_dev_handle_t device(uint8_t address){i2c_master_dev_handle_t dev; i2c_device_config_t c{};c.dev_addr_length=I2C_ADDR_BIT_LEN_7;c.device_address=address;c.scl_speed_hz=400000;ESP_ERROR_CHECK(i2c_master_bus_add_device(bus,&c,&dev));return dev;}

void board_brightness(int value){ledc_set_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_1,value*1023/100);ledc_update_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_1);}
bool board_power_pressed(){uint32_t v=0;return esp_io_expander_get_level(expander,1<<6,&v)==ESP_OK&&(v&(1<<6));}
void board_shutdown(){power.shutdown();}
int board_touch(mc::Point *points){
    uint8_t reg=2,data[13];
    if(i2c_master_transmit_receive(touch,&reg,1,data,sizeof(data),20)!=ESP_OK)return -1;
    return mc::decode_touch(data,sizeof(data),points);
}
void board_rtc(uint32_t seconds,int offset){
    time_t t=seconds+offset*60;struct tm tm{};gmtime_r(&t,&tm);
    auto bcd=[](int v){return uint8_t((v/10)*16+v%10);};
    uint8_t data[]={4,bcd(tm.tm_sec),bcd(tm.tm_min),bcd(tm.tm_hour),bcd(tm.tm_mday),bcd(tm.tm_wday),bcd(tm.tm_mon+1),bcd((tm.tm_year+1900)%100)};
    i2c_master_transmit(rtc,data,sizeof(data),100);
}
static void audio_task(void*){
    uint8_t event;int16_t pcm[960];
    for(int i=0;i<480;i++){int16_t v=1800*std::sin(2*3.14159265*1800*i/16000)*(1.f-i/480.f);pcm[2*i]=pcm[2*i+1]=v;}
    while(true)if(xQueueReceive(beeps,&event,portMAX_DELAY))esp_codec_dev_write(codec,pcm,sizeof(pcm));
}
static void init_audio(){
    i2s_chan_handle_t tx,rx;i2s_chan_config_t channel=I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0,I2S_ROLE_MASTER);
    if(i2s_new_channel(&channel,&tx,&rx)!=ESP_OK)return;
    i2s_std_config_t c{};c.clk_cfg=I2S_STD_CLK_DEFAULT_CONFIG(16000);c.slot_cfg=I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,I2S_SLOT_MODE_STEREO);
    c.gpio_cfg.mclk=GPIO_NUM_12;c.gpio_cfg.bclk=GPIO_NUM_13;c.gpio_cfg.ws=GPIO_NUM_15;c.gpio_cfg.dout=GPIO_NUM_16;c.gpio_cfg.din=GPIO_NUM_14;
    if(i2s_channel_init_std_mode(tx,&c)!=ESP_OK||i2s_channel_init_std_mode(rx,&c)!=ESP_OK)return;
    i2s_channel_enable(tx);i2s_channel_enable(rx);
    audio_codec_i2s_cfg_t data{};data.port=I2S_NUM_0;data.tx_handle=tx;data.rx_handle=rx;
    audio_codec_i2c_cfg_t control{};control.port=I2C_NUM_0;control.addr=ES8311_CODEC_DEFAULT_ADDR;control.bus_handle=bus;
    es8311_codec_cfg_t chip{};chip.codec_mode=ESP_CODEC_DEV_WORK_MODE_BOTH;chip.ctrl_if=audio_codec_new_i2c_ctrl(&control);chip.gpio_if=audio_codec_new_gpio();chip.pa_pin=GPIO_NUM_NC;chip.use_mclk=true;
    chip.hw_gain.pa_voltage=5;chip.hw_gain.codec_dac_voltage=3.3;
    esp_codec_dev_cfg_t dev{};dev.dev_type=ESP_CODEC_DEV_TYPE_IN_OUT;dev.codec_if=es8311_codec_new(&chip);dev.data_if=audio_codec_new_i2s_data(&data);
    codec=esp_codec_dev_new(&dev);if(!codec)return;
    esp_codec_dev_sample_info_t info{};info.sample_rate=16000;info.channel=2;info.bits_per_sample=16;
    if(esp_codec_dev_open(codec,&info)!=ESP_OK){codec=nullptr;return;}esp_codec_dev_set_out_vol(codec,45);
    beeps=xQueueCreate(4,1);xTaskCreate(audio_task,"beeps",4096,nullptr,3,nullptr);
}
void board_beep(){if(codec&&state.settings.sound){uint8_t e=1;xQueueSend(beeps,&e,0);}}

void board_init(){
    ledc_timer_config_t timer{};timer.speed_mode=LEDC_LOW_SPEED_MODE;timer.timer_num=LEDC_TIMER_1;timer.duty_resolution=LEDC_TIMER_10_BIT;timer.freq_hz=5000;timer.clk_cfg=LEDC_AUTO_CLK;ESP_ERROR_CHECK(ledc_timer_config(&timer));
    ledc_channel_config_t ch{};ch.gpio_num=6;ch.speed_mode=LEDC_LOW_SPEED_MODE;ch.channel=LEDC_CHANNEL_1;ch.timer_sel=LEDC_TIMER_1;ESP_ERROR_CHECK(ledc_channel_config(&ch));
    i2c_master_bus_config_t i{};i.i2c_port=I2C_NUM_0;i.sda_io_num=GPIO_NUM_8;i.scl_io_num=GPIO_NUM_7;i.clk_source=I2C_CLK_SRC_DEFAULT;i.glitch_ignore_cnt=7;i.flags.enable_internal_pullup=true;ESP_ERROR_CHECK(i2c_new_master_bus(&i,&bus));
    ESP_ERROR_CHECK(esp_io_expander_new_i2c_tca9554(bus,ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000,&expander));
    ESP_ERROR_CHECK(esp_io_expander_set_dir(expander,(1<<1)|(1<<7),IO_EXPANDER_OUTPUT));
    ESP_ERROR_CHECK(esp_io_expander_set_dir(expander,1<<6,IO_EXPANDER_INPUT));
    esp_io_expander_set_level(expander,1<<1,0);vTaskDelay(pdMS_TO_TICKS(100));esp_io_expander_set_level(expander,(1<<1)|(1<<7),1);vTaskDelay(pdMS_TO_TICKS(100));
    pmu=device(0x34);touch=device(0x38);rtc=device(0x51);
    ESP_ERROR_CHECK(power.begin(0x34,reg_read,reg_write)?ESP_OK:ESP_FAIL);
    power.setDC1Voltage(3300);power.setDC2Voltage(1000);power.setDC3Voltage(3300);power.setDC4Voltage(1000);power.setDC5Voltage(3300);
    power.setALDO1Voltage(3300);power.setALDO2Voltage(3300);power.setALDO3Voltage(3300);power.setALDO4Voltage(3300);
    power.enableALDO1();power.enableALDO2();power.enableALDO3();power.enableALDO4();power.enableDC2();power.enableDC3();power.enableDC4();power.enableDC5();
    power.setBLDO1Voltage(1500);power.setBLDO2Voltage(2800);power.setCPUSLDOVoltage(1000);power.setDLDO1Voltage(3300);
    power.enableBLDO1();power.enableBLDO2();power.enableCPUSLDO();power.enableDLDO1();power.enableDLDO2();
    power.disableTSPinMeasure();power.disableWatchdog();power.settDC1WorkModeToPwm(true);
    power.setPowerKeyPressOffTime(XPOWERS_POWEROFF_4S); // AXP2101 hardware choices are 4/6/8/10 s; app enforces 3 s.
    uint8_t normal[]={0xd0,0};i2c_master_transmit(touch,normal,2,100);
    spi_bus_config_t spi{};spi.mosi_io_num=1;spi.miso_io_num=-1;spi.sclk_io_num=5;spi.quadwp_io_num=-1;spi.quadhd_io_num=-1;spi.max_transfer_sz=480*40*2;ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST,&spi,SPI_DMA_CH_AUTO));
    esp_lcd_panel_io_spi_config_t io{};io.dc_gpio_num=3;io.cs_gpio_num=-1;io.pclk_hz=80000000;io.lcd_cmd_bits=8;io.lcd_param_bits=8;io.trans_queue_depth=10;
    esp_lcd_panel_io_handle_t lcd_io;ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST,&io,&lcd_io));
    esp_lcd_panel_dev_config_t cfg{};cfg.reset_gpio_num=-1;cfg.rgb_ele_order=LCD_RGB_ELEMENT_ORDER_BGR;cfg.bits_per_pixel=16;
    esp_lcd_panel_handle_t panel;ESP_ERROR_CHECK(esp_lcd_new_panel_st7796(lcd_io,&cfg,&panel));ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    esp_lcd_panel_invert_color(panel,true);esp_lcd_panel_swap_xy(panel,true);esp_lcd_panel_mirror(panel,false,false);esp_lcd_panel_disp_on_off(panel,true);
    lvgl_port_cfg_t port=ESP_LVGL_PORT_INIT_CONFIG();port.task_stack=12288;port.task_max_sleep_ms=10;port.task_affinity=1;ESP_ERROR_CHECK(lvgl_port_init(&port));
    lvgl_port_display_cfg_t disp{};disp.io_handle=lcd_io;disp.panel_handle=panel;disp.buffer_size=480*40;disp.double_buffer=true;disp.hres=480;disp.vres=320;disp.color_format=LV_COLOR_FORMAT_RGB565;disp.rotation.swap_xy=true;disp.flags.buff_dma=true;disp.flags.swap_bytes=true;
    auto display=lvgl_port_add_disp(&disp);ESP_ERROR_CHECK(display?ESP_OK:ESP_FAIL);
    lvgl_port_lock(0);lv_timer_set_period(lv_display_get_refr_timer(display),16);lv_display_add_event_cb(display,render_event,LV_EVENT_RENDER_START,nullptr);lv_display_add_event_cb(display,render_event,LV_EVENT_RENDER_READY,nullptr);lvgl_port_unlock();
    init_audio();
}
