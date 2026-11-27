#pragma once
#include "core.hpp"
#include "transition.hpp"
#include "lvgl.h"
#include <string>
#include <array>
struct Settings {int brightness=80,cover=0,bg=0,sens=1,sound=1,sleep=5,guided=0,reverse_gestures=0;};
struct ImageSlot {
    uint8_t *front=nullptr,*back=nullptr;
    size_t front_capacity=0,back_capacity=0,total=0,received=0;
    uint16_t w=0,h=0;uint8_t fmt=0,flags=0;uint32_t tag=0,front_tag=0;
    uint64_t started=0;
    bool loading=false;
    lv_image_dsc_t image{};
};
struct State {
    Settings settings;
    bool linked=false,playing=false,muted=false,asleep=false,guide=false,detail=false,pager=false;
    bool has_track=false,dark=false,art_current=false,cover_current=false;
    uint8_t background_progress=255;
    int page=0,vol=40,pos=0,dur=0,drag=0,pager_dx=0;
    uint32_t track_tag=0,tint=0x3c3f46;
    uint8_t cpu=255,mem=255,gpu=255;
    uint32_t net=0;
    uint64_t last_rx=0,last_sys=0,last_touch=0,playback_at=0,save_due=0,hud_until=0,hint_until=0,dots_until=0;
    int clock_offset=0,image_failures=0,offline_delta=0;
    std::string hint;
    std::array<ImageSlot,24> blobs;
    mc::TrackTransition transition;
    uint32_t ui_frames=0,ui_render_ms=0,ui_render_max_ms=0;
    uint32_t transition_frames=0,transition_render_max_ms=0,drag_render_max_ms=0;
    uint32_t mute_gestures=0;
};
extern State state;
uint64_t now_ms();
void send_frame(uint8_t type,const uint8_t *data=nullptr,size_t n=0);
void send_json(uint8_t type,const std::string&json);
void usb_init();
void usb_poll();
void hid_key(uint16_t usage);
void board_init();
void board_brightness(int value);
int board_touch(mc::Point *points);
bool board_power_pressed();
void board_shutdown();
void board_beep();
void board_rtc(uint32_t unix_seconds,int offset);
void ui_init();
void ui_update();
void ui_animate_track(int direction);
void ui_track_received(uint32_t tag);
void ui_spring();
void ui_setting(int x,int y,bool release,bool cancel=false);
