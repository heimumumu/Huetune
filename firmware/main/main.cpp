#include "app.hpp"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_lvgl_port.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_system.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/soc.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <cstddef>
#include <sys/time.h>

State state;
static mc::Gesture gesture;
static nvs_handle_t storage;
uint64_t now_ms(){return esp_timer_get_time()/1000;}

struct TouchSample {
    uint64_t at;
    int count;
    mc::Point points[2];
};
static QueueHandle_t touch_samples;

static void read_touch(void*){
    for(;;){
        TouchSample sample{};
        sample.count=board_touch(sample.points);
        sample.at=now_ms();
        if(sample.count>=0)xQueueSend(touch_samples,&sample,0);
        vTaskDelay(pdMS_TO_TICKS(8));
    }
}
static void publish_settings(){
    auto&s=state.settings;char b[256];snprintf(b,sizeof(b),"{\"brightness\":%d,\"cover\":\"%s\",\"bg\":\"%s\",\"sens\":%.1f,\"sound\":%d,\"sleep\":%d,\"page\":%d,\"reverse_gestures\":%s}",s.brightness,s.cover?"immersive":"classic",s.bg?"color":"blur",.5*(s.sens+1),s.sound,s.sleep,state.page,s.reverse_gestures?"true":"false");send_json(0x93,b);
}
void settings_changed(){state.save_due=now_ms()+1000;publish_settings();}
static void request_cover(){uint8_t bg=state.settings.bg;send_frame(0x92,&bg,1);}
static void image_failed(ImageSlot&slot){
    slot.loading=false;
    if(++state.image_failures>=3){for(int i=1;i<=3;i++)state.blobs[i].image.data=nullptr;}
    request_cover();
}
static bool valid_blob(uint8_t id,int w,int h,int fmt,size_t total){
    if(id==1||id==2)return w==480&&h==320&&fmt==1&&total==480*320*2;
    if(id==3)return w==216&&h==216&&fmt==1&&total==216*216*2;
    if(id==10||id==11)return w>0&&w<=8192&&(h==36||h==72)&&fmt==2&&total==size_t((w+1)/2)*h;
    if(id==12||id==13||id>=20&&id<=23)return w>0&&w<=226&&h==24&&fmt==2&&total==size_t((w+1)/2)*24;
    return false;
}
void on_frame(uint8_t type,const uint8_t*p,size_t n){
    auto now=now_ms();
    if(type==0x03&&n==0){
        char b[768];snprintf(b,sizeof(b),"{\"linked\":%s,\"fw\":\"0.2.18\",\"uptime_ms\":%llu,\"rx_age_ms\":%llu,\"track\":%s,\"image_failures\":%d,\"psram_free\":%u,\"gesture_fingers\":%d,\"gesture_axis\":%d,\"mute_gestures\":%u,\"staggered_taps\":%u,\"vol\":%d,\"muted\":%s,\"transition\":%d,\"transition_starts\":%u,\"ui_frames\":%u,\"render_ms\":%u,\"render_max_ms\":%u,\"transition_frames\":%u,\"transition_render_max_ms\":%u,\"drag_render_max_ms\":%u,\"track_reply_ms\":%u,\"track_title_ms\":%u,\"art_current\":%s,\"cover_current\":%s,\"background_progress\":%u,\"lcd_mhz\":80}",state.linked?"true":"false",(unsigned long long)now,(unsigned long long)(now>=state.last_rx?now-state.last_rx:0),state.has_track?"true":"false",state.image_failures,(unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),gesture.max,gesture.axis,(unsigned)state.mute_gestures,(unsigned)gesture.staggered_taps,state.vol,state.muted?"true":"false",int(state.transition.phase),(unsigned)state.transition.starts,(unsigned)state.ui_frames,(unsigned)state.ui_render_ms,(unsigned)state.ui_render_max_ms,(unsigned)state.transition_frames,(unsigned)state.transition_render_max_ms,(unsigned)state.drag_render_max_ms,(unsigned)state.transition.reply_ms,(unsigned)state.transition.heading_ms,state.art_current?"true":"false",state.cover_current?"true":"false",unsigned(state.background_progress));
        send_json(0x83,b);return;
    }
    if(type==0x01){
        auto j=cJSON_ParseWithLength((const char*)p,n);auto proto=j?cJSON_GetObjectItem(j,"proto"):nullptr;
        if(cJSON_IsNumber(proto)&&proto->valueint==1){state.linked=true;state.last_rx=now;publish_settings();}
        cJSON_Delete(j);return;
    }
    if(!state.linked)return;
    state.last_rx=now;
    if(type==0x04&&n==4&&!memcmp(p,"BOOT",4)){
        REG_SET_BIT(RTC_CNTL_OPTION1_REG,RTC_CNTL_FORCE_DOWNLOAD_BOOT);
        esp_restart();
    }
    if(type==0x10){
        auto j=cJSON_ParseWithLength((const char*)p,n);auto id=j?cJSON_GetObjectItem(j,"id"):nullptr;
        if(cJSON_IsString(id)){
            uint32_t tag=mc::crc32((const uint8_t*)id->valuestring,strlen(id->valuestring));
            if(tag!=state.track_tag){
                if(state.has_track)ui_track_received(tag);
                state.track_tag=tag;state.detail=false;state.image_failures=0;
                for(auto&s:state.blobs)s.loading=false;
                for(int i=10;i<24;i++)state.blobs[i].image.data=nullptr;
            }
            state.has_track=true;
        }cJSON_Delete(j);
    }else if(type==0x11&&n==5){state.playing=p[0];state.pos=mc::u16(p+1);state.dur=mc::u16(p+3);state.playback_at=now;}
    else if(type==0x12&&n==2){state.vol=std::min(100,int(p[0]));state.muted=p[1];}
    else if(type==0x13&&n==7){state.dark=p[0];state.tint=(p[1]<<16)|(p[2]<<8)|p[3];}
    else if(type==0x14&&n==7){state.cpu=p[0];state.mem=p[1];state.gpu=p[2];state.net=mc::u32(p+3);state.last_sys=now;}
    else if(type==0x15&&n==6){struct timeval t{(time_t)mc::u32(p),0};settimeofday(&t,nullptr);state.clock_offset=int16_t(mc::u16(p+4));board_rtc(mc::u32(p),state.clock_offset);}
    else if(type==0x20&&n==15){
        uint8_t id=p[0];if(id>=24)return;
        auto&s=state.blobs[id];s.loading=false;
        uint32_t tag=mc::u32(p+1);auto total=mc::u32(p+11);int w=mc::u16(p+5),h=mc::u16(p+7),fmt=p[9];
        if(tag!=state.track_tag||!valid_blob(id,w,h,fmt,total))return;
        if(s.back_capacity<total){
            auto next=(uint8_t*)heap_caps_realloc(s.back,total,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
            if(!next){image_failed(s);return;}s.back=next;s.back_capacity=total;
        }
        s.tag=tag;s.w=w;s.h=h;s.fmt=fmt;s.flags=p[10];s.total=total;s.received=0;s.started=now;s.loading=true;
    }else if(type==0x21&&n>=5){
        if(p[0]>=24)return;auto&s=state.blobs[p[0]];
        if(!s.loading||s.tag!=state.track_tag)return;
        if(mc::u32(p+1)!=s.received||n-5>s.total-s.received){image_failed(s);return;}
        memcpy(s.back+s.received,p+5,n-5);s.received+=n-5;s.started=now;
    }else if(type==0x22&&n==5){
        if(p[0]>=24)return;auto&s=state.blobs[p[0]];
        if(!s.loading||s.tag!=state.track_tag)return;
        if(s.received!=s.total||mc::crc32(s.back,s.total)!=mc::u32(p+1)){image_failed(s);return;}
        // All receive and display mutations hold the LVGL lock. Invalidate its
        // image cache before reusing the old front buffer as the next back buffer.
        lv_image_cache_drop(&s.image);
        std::swap(s.front,s.back);std::swap(s.front_capacity,s.back_capacity);
        s.image={};s.image.header.magic=LV_IMAGE_HEADER_MAGIC;s.image.header.w=s.w;s.image.header.h=s.h;
        s.image.header.cf=s.fmt==1?LV_COLOR_FORMAT_RGB565:LV_COLOR_FORMAT_A4;
        s.image.header.stride=s.fmt==1?s.w*2:(s.w+1)/2;s.image.data=s.front;s.image.data_size=s.total;s.front_tag=s.tag;s.loading=false;
    }
}
static void beep(){board_beep();}
static void play(){hid_key(0xcd);beep();state.hint=state.playing?"已暂停":"继续播放";state.hint_until=now_ms()+900;}
static int base_volume=40,last_volume=40,desired_volume=40;static bool volume_pending=false;static uint64_t volume_sent=0,hid_sent=0;static float offline_pending=0,offline_last=0;
static void volume_tick(uint64_t now,bool /*force*/=false){
    if(state.linked){
        if(volume_pending&&now-volume_sent>=50){
            uint8_t v=desired_volume;send_frame(0x90,&v,1);last_volume=desired_volume;volume_pending=false;volume_sent=now;
        }
    }else if(std::abs(offline_pending)>=4&&now-hid_sent>=30){
        int direction=offline_pending>0?1:-1;hid_key(direction>0?0xe9:0xea);offline_pending-=direction*4;hid_sent=now;
    }
}
static void action(mc::Action a,float x,float y){
    auto now=now_ms();
    switch(a){
    case mc::Action::Begin:
    case mc::Action::TwoBegin:base_volume=state.vol;last_volume=state.vol;offline_pending=offline_last=0;break;
    case mc::Action::Tap:if(state.page==0){if(state.detail)state.detail=false;else play();}break;
    case mc::Action::Mute:
        ++state.mute_gestures;
        if(state.linked){uint8_t m=!state.muted;send_frame(0x91,&m,1);state.muted=m;}
        else{hid_key(0xe2);state.hint="静音切换";state.hint_until=now+1200;}
        state.hud_until=now+1200;beep();break;
    case mc::Action::Long:state.pager=true;state.pager_dx=0;beep();break;
    case mc::Action::Pager:state.pager_dx=x;break;
    case mc::Action::PageEnd:
        state.pager=false;
        if(y)break;
        if(std::abs(x)>=60){state.page=(state.page+(x>0?1:2))%3;state.detail=false;state.dots_until=now+1200;publish_settings();}
        else if(state.page==0&&state.linked&&state.has_track)state.detail=true;
        break;
    case mc::Action::Volume:{
        const float swipe_range=gesture.reverse_gestures?320.f:480.f;
        float delta=x/swipe_range*100*(.5*(state.settings.sens+1));
        if(state.linked){state.vol=std::clamp(int(std::lround(base_volume+delta)),0,100);desired_volume=state.vol;volume_pending=desired_volume!=last_volume;state.muted=false;}
        else{offline_pending+=delta-offline_last;offline_last=delta;state.offline_delta=std::lround(delta);}
        state.hud_until=now+1200;volume_tick(now);break;
    }
    case mc::Action::VolumeEnd:state.hud_until=now+1200;volume_tick(now,true);break;
    case mc::Action::Drag:state.drag=x*.5;state.hint=std::abs(x)>=60?(x>0?"松手：下一首":"松手：上一首"):(x>0?"下一首":"上一首");state.hint_until=now+100;break;
    case mc::Action::Track:hid_key(x>0?0xb5:0xb6);state.hint="";ui_animate_track(x);beep();state.drag=0;break;
    case mc::Action::Spring:ui_spring();state.hint="";break;
    case mc::Action::Control:ui_setting(x,y,false);break;
    case mc::Action::ControlEnd:ui_setting(x,y,true);if(state.page==2)gesture.single_due=0;break;
    case mc::Action::CancelControl:ui_setting(0,0,false,true);break;
    }
}
extern "C" void app_main(){
    auto result=nvs_flash_init();if(result==ESP_ERR_NVS_NO_FREE_PAGES||result==ESP_ERR_NVS_NEW_VERSION_FOUND){ESP_ERROR_CHECK(nvs_flash_erase());ESP_ERROR_CHECK(nvs_flash_init());}
    ESP_ERROR_CHECK(nvs_open("controller",NVS_READWRITE,&storage));
    Settings saved;size_t size=sizeof(saved);
    if(nvs_get_blob(storage,"settings",&saved,&size)==ESP_OK&&
       (size==sizeof(saved)||size==offsetof(Settings,reverse_gestures))&&
       saved.brightness>=10&&saved.brightness<=100&&saved.cover>=0&&saved.cover<=1&&
       saved.bg>=0&&saved.bg<=1&&saved.sens>=0&&saved.sens<=2&&
       (saved.sleep==0||saved.sleep==1||saved.sleep==5)&&
       (saved.reverse_gestures==0||saved.reverse_gestures==1))state.settings=saved;
    state.guide=!state.settings.guided;state.settings.guided=1;state.save_due=now_ms()+1000;
    board_init();usb_init();gesture.emit=action;
    touch_samples=xQueueCreate(48,sizeof(TouchSample));
    configASSERT(touch_samples);
    const auto touch_task=xTaskCreate(read_touch,"touch",3072,nullptr,3,nullptr);
    configASSERT(touch_task==pdPASS);
    lvgl_port_lock(0);ui_init();ui_update();lvgl_port_unlock();board_brightness(state.settings.brightness);
    uint64_t hello=0,ping=0,pwr_start=0;bool pwr_before=false,wake_only=false,guide_only=false;int previous_points=0;
    state.last_touch=now_ms();
    for(;;){
        auto now=now_ms();lvgl_port_lock(0);usb_poll();now=now_ms();
        if(!state.linked&&mc::elapsed(now,hello,1000)){send_json(0x81,"{\"proto\":1,\"fw\":\"0.2.18\",\"w\":480,\"h\":320}");hello=now;}
        if(now-ping>=1000){send_frame(0x82);ping=now;}
        if(state.linked&&mc::elapsed(now,state.last_rx,3000)){state.linked=false;state.muted=false;state.pager=false;state.detail=false;gesture.reset();}
        for(auto&s:state.blobs)if(s.loading&&mc::elapsed(now,s.started,2000))image_failed(s);
        TouchSample sample{};
        while(xQueueReceive(touch_samples,&sample,0)==pdTRUE){
            const int n=sample.count;
            if(n){
                state.last_touch=sample.at;
                if(state.asleep){state.asleep=false;board_brightness(state.settings.brightness);wake_only=true;gesture.reset();}
                if(state.guide&&!previous_points){state.guide=false;guide_only=true;gesture.reset();}
            }
            if(!wake_only&&!guide_only)gesture.sample(sample.at,sample.points,n,state.page!=2&&!state.detail&&!state.pager,state.settings.reverse_gestures);
            if(!n){wake_only=guide_only=false;}previous_points=n;
        }
        now=now_ms();
        volume_tick(now);
        bool pressed=board_power_pressed();
        if(pressed&&!pwr_before)pwr_start=now;
        if(pressed&&now-pwr_start>=3000)board_shutdown();
        if(!pressed&&pwr_before&&now-pwr_start>=30&&now-pwr_start<3000){state.asleep=!state.asleep;state.last_touch=now;board_brightness(state.asleep?0:state.settings.brightness);gesture.reset();}
        pwr_before=pressed;
        if(!state.asleep&&state.settings.sleep&&now-state.last_touch>=state.settings.sleep*60000ull){state.asleep=true;board_brightness(0);}
        if(state.save_due&&now>=state.save_due){nvs_set_blob(storage,"settings",&state.settings,sizeof(state.settings));nvs_commit(storage);state.save_due=0;}
        ui_update();lvgl_port_unlock();vTaskDelay(pdMS_TO_TICKS(10));
    }
}
