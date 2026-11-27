#include "app.hpp"
#include "tinyusb.h"
#include "tusb.h"
#include "tusb_cdc_acm.h"
#include "esp_mac.h"
#include <deque>
static uint8_t report[]={TUD_HID_REPORT_DESC_CONSUMER()};
enum {CDC_CONTROL,CDC_DATA,HID_INTERFACE,INTERFACES};
static uint8_t config[]={
    TUD_CONFIG_DESCRIPTOR(1,INTERFACES,0,TUD_CONFIG_DESC_LEN+TUD_CDC_DESC_LEN+TUD_HID_DESC_LEN,0,100),
    TUD_CDC_DESCRIPTOR(CDC_CONTROL,4,0x81,8,0x02,0x82,64),
    TUD_HID_DESCRIPTOR(HID_INTERFACE,5,HID_ITF_PROTOCOL_NONE,sizeof(report),0x83,8,5)
};
static tusb_desc_device_t device={sizeof(tusb_desc_device_t),TUSB_DESC_DEVICE,0x0200,TUSB_CLASS_MISC,MISC_SUBCLASS_COMMON,MISC_PROTOCOL_IAD,64,0x303a,0x4001,0x0100,1,2,3,1};
static char serial_number[20];
static const char *strings[]={"\x09\x04","Media Controller","ESP32 Multimedia Controller",serial_number,"Controller Data","Media Keys"};
static std::deque<uint16_t> keys;
static bool release_key=false;
static mc::Parser parser;
extern void on_frame(uint8_t,const uint8_t*,size_t);
extern "C" uint8_t const *tud_hid_descriptor_report_cb(uint8_t){return report;}
extern "C" uint16_t tud_hid_get_report_cb(uint8_t,uint8_t,hid_report_type_t,uint8_t*,uint16_t){return 0;}
extern "C" void tud_hid_set_report_cb(uint8_t,uint8_t,hid_report_type_t,uint8_t const*,uint16_t){}
void hid_key(uint16_t usage){if(keys.size()<32)keys.push_back(usage);}
void send_frame(uint8_t type,const uint8_t *data,size_t n){
    static uint8_t seq=0;
    if(!tud_cdc_connected())return;
    auto v=mc::frame(type,seq++,data,n);
    tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0,v.data(),v.size());
    tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0,0);
}
void send_json(uint8_t type,const std::string&json){send_frame(type,(const uint8_t*)json.data(),json.size());}
void usb_init(){
    uint8_t mac[6];esp_read_mac(mac,ESP_MAC_WIFI_STA);snprintf(serial_number,sizeof(serial_number),"%02X%02X%02X%02X%02X%02X",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    tinyusb_config_t c{};c.device_descriptor=&device;c.string_descriptor=strings;c.string_descriptor_count=6;c.configuration_descriptor=config;ESP_ERROR_CHECK(tinyusb_driver_install(&c));
    tinyusb_config_cdcacm_t acm{};acm.usb_dev=TINYUSB_USBDEV_0;acm.cdc_port=TINYUSB_CDC_ACM_0;ESP_ERROR_CHECK(tusb_cdc_acm_init(&acm));
    parser.receive=on_frame;
}
void usb_poll(){
    uint8_t b[2048];size_t n=0;
    // Bound work per UI tick so a large cover cannot delay gestures.
    for(int i=0;i<16;i++){if(tinyusb_cdcacm_read(TINYUSB_CDC_ACM_0,b,sizeof(b),&n)!=ESP_OK||!n)break;parser.feed(b,n);}
    if(tud_hid_ready()){
        if(release_key){uint16_t zero=0;tud_hid_report(0,&zero,2);release_key=false;}
        else if(!keys.empty()){auto k=keys.front();keys.pop_front();tud_hid_report(0,&k,2);release_key=true;}
    }
}
