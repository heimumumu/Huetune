#ifdef NDEBUG
#undef NDEBUG
#endif
#include "app.hpp"
#include <cstdio>
#include <fstream>
#include <cassert>
#include <filesystem>
State state;
static uint64_t tick=10000;
uint64_t now_ms(){return tick;}
void send_frame(uint8_t,const uint8_t*,size_t){}
void board_brightness(int){}
void settings_changed(){}
static uint16_t pixels[480*320];
static uint64_t flushed_pixels=0;
static uint64_t largest_frame=0;
static void flush(lv_display_t*d,const lv_area_t*a,uint8_t*data){
    flushed_pixels+=uint64_t(a->x2-a->x1+1)*(a->y2-a->y1+1);
    auto p=(uint16_t*)data;
    for(int y=a->y1;y<=a->y2;y++)for(int x=a->x1;x<=a->x2;x++)pixels[y*480+x]=*p++;
    lv_display_flush_ready(d);
}
static void write_frame(const char*name){
    std::ofstream f(name,std::ios::binary);f<<"P6\n480 320\n255\n";
    for(auto p:pixels){unsigned char rgb[]={uint8_t(((p>>11)&31)*255/31),uint8_t(((p>>5)&63)*255/63),uint8_t((p&31)*255/31)};f.write((char*)rgb,3);}
}
static void step(int ms,const char*name=nullptr){
    tick+=ms;lv_tick_inc(ms);auto before=flushed_pixels;ui_update();lv_timer_handler();lv_refr_now(lv_display_get_default());
    largest_frame=std::max(largest_frame,flushed_pixels-before);
    if(name)write_frame(name);
}
static void save(const char*name){step(0);step(700,name);}

static void load_assets(const char*prefix,uint32_t tag){
    for(int id:{1,2,3,10,11,12,13,20,21,22,23}){
        char path[120];snprintf(path,sizeof(path),"build/%sblob-%d.bin",prefix,id);std::ifstream f(path,std::ios::binary);assert(f);
        uint16_t w,h;uint8_t fmt,flags;f.read((char*)&w,2);f.read((char*)&h,2);f.read((char*)&fmt,1);f.read((char*)&flags,1);
        auto&s=state.blobs[id];std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),{});s.front=(uint8_t*)malloc(data.size());memcpy(s.front,data.data(),data.size());s.flags=flags;s.front_tag=tag;
        s.image={};s.image.header.magic=LV_IMAGE_HEADER_MAGIC;s.image.header.w=w;s.image.header.h=h;s.image.header.cf=fmt==1?LV_COLOR_FORMAT_RGB565:LV_COLOR_FORMAT_A4;s.image.header.stride=fmt==1?w*2:(w+1)/2;s.image.data=s.front;s.image.data_size=data.size();
    }
}

static void check_cue_layout(){
    auto screen=lv_screen_active();
    auto music=lv_obj_get_child(lv_obj_get_child(screen,1),1);
    auto progress=lv_obj_get_child(music,3);
    auto hint=lv_obj_get_child(screen,10);
    auto meter=lv_obj_get_child(screen,11);
    auto separated=[](const lv_area_t& a,const lv_area_t& b){
        return a.x2<b.x1||b.x2<a.x1||a.y2<b.y1||b.y2<a.y1;
    };
    for(int mode:{0,1}){
        state.settings.cover=mode;
        for(int direction:{-1,1}){
            state.drag=direction*40;
            state.hint=direction<0?"松手：上一首":"松手：下一首";
            state.hint_until=tick+100;
            step(16,mode&&direction<0?"build/cue-previous.ppm":nullptr);
            assert(!lv_obj_has_flag(hint,LV_OBJ_FLAG_HIDDEN)&&!lv_obj_has_flag(meter,LV_OBJ_FLAG_HIDDEN));
            lv_area_t capsule{},stripe{},timeline{};
            lv_obj_get_coords(hint,&capsule);
            lv_obj_get_coords(meter,&stripe);
            lv_obj_get_coords(progress,&timeline);
            assert(separated(capsule,stripe));
            assert(separated(timeline,capsule));
            assert(separated(timeline,stripe));
        }
    }
    ui_spring();
    state.hint.clear();
}

static void check_transition(int dir){
    using T=mc::TrackTransition;
    state.settings.cover=0;state.track_tag=0;load_assets("",0);state.transition.cancel();state.hint="";save("build/transition-before.ppm");
    step(330);
    std::filesystem::create_directories("build/animation");
    state.drag=dir*40;step(20);
    auto content=lv_obj_get_child(lv_screen_active(),1);
    auto cover=lv_obj_get_child(lv_obj_get_child(content,0),0);
    auto cover_status=lv_obj_get_child(lv_screen_active(),2);
    auto heading=lv_obj_get_child(lv_obj_get_child(content,1),0);
    int drag_y=lv_obj_get_y(content);
    uint64_t cue_pixels=0;
    for(int i=1;i<=10;i++){
        state.drag=dir*i*3;state.hint=dir>0?"下一首":"上一首";state.hint_until=tick+100;
        auto before=flushed_pixels;step(16);cue_pixels=std::max(cue_pixels,flushed_pixels-before);
        assert(lv_obj_get_y(content)==52);
    }
    assert(cue_pixels<20000);
    auto starts=state.transition.starts;
    ui_animate_track(dir);step(0,"build/animation/00-release.ppm");
    assert(lv_obj_get_y(content)==drag_y); // Commit never snaps back to the center.
    largest_frame=0;
    int frame=0;
    auto movie_frame=[&](int ms){char name[128];snprintf(name,sizeof(name),"build/animation/%s-%02d.ppm",dir>0?"direct-next":"direct-previous",frame++);step(ms,name);};
    movie_frame(0);
    movie_frame(100);
    assert(lv_obj_get_y(content)==drag_y);
    step(0,"build/animation/02-wait.ppm");assert(state.transition.phase==T::Holding&&state.transition.offset(tick)==0);
    step(500,"build/animation/03-delayed-reply.ppm");assert(state.transition.phase==T::Holding&&lv_obj_get_y(content)==drag_y);
    movie_frame(0);
    ui_track_received(1);state.track_tag=1;
    for(int id=10;id<24;id++)state.blobs[id].image.data=nullptr;
    step(50);assert(state.transition.phase==T::Holding&&state.transition.starts==starts+1);
    load_assets("next-",1);
    state.blobs[1].front_tag=state.blobs[2].front_tag=state.blobs[3].front_tag=0;
    state.blobs[13].front_tag=0; // Wait for matching title and artist.
    step(16);assert(state.transition.phase==T::Holding&&!state.art_current);
    state.blobs[13].front_tag=1;
    step(0,"build/animation/04-ready.ppm");assert(state.transition.phase==T::Entering);
    assert(!state.art_current);
    assert(!lv_obj_has_flag(cover_status,LV_OBJ_FLAG_HIDDEN));
    assert(!strcmp(lv_label_get_text(cover_status),"封面同步中"));
    int y=lv_obj_get_y(heading);
    assert(y==-dir*20);
    largest_frame=0;
    int drawn=0;
    uint64_t slide_pixels=0;
    for(int i=0;i<11;i++){
        auto before=flushed_pixels;
        if(i==2&&dir>0)state.blobs[2].front_tag=state.blobs[3].front_tag=1;
        movie_frame(16);
        if(i<9){
            assert(!state.art_current); // Artwork cannot interrupt the slide.
            if(flushed_pixels>before)++drawn;
            slide_pixels=std::max(slide_pixels,flushed_pixels-before);
        }
        int next_y=lv_obj_get_y(heading);
        assert(std::abs(next_y)<=std::abs(y)&&(next_y==0||next_y*dir<0));
        y=next_y;
    }
    assert(drawn>=5&&slide_pixels<45000);
    step(0,"build/animation/06-complete.ppm");
    assert(state.transition.phase==T::Idle&&y==0&&lv_obj_get_y(content)==52&&state.transition.starts==starts+1);
    assert(state.cover_current==(dir>0)&&!state.art_current);
    if(dir<0){
        state.blobs[3].front_tag=1;
        step(0);
    }
    const auto initial_opacity=lv_obj_get_style_image_opa(cover,0);
    assert(state.cover_current&&!state.art_current&&initial_opacity>0&&initial_opacity<128);
    assert(lv_obj_has_flag(cover_status,LV_OBJ_FLAG_HIDDEN));
    const auto before_fade=flushed_pixels;
    step(48,"build/animation/cover-fade.ppm");
    const auto fade_pixels=flushed_pixels-before_fade;
    assert(fade_pixels>0&&fade_pixels<52000);
    const auto middle_opacity=lv_obj_get_style_image_opa(cover,0);
    assert(middle_opacity>initial_opacity&&middle_opacity<255);
    if(dir<0)state.blobs[2].front_tag=1;
    step(24);
    assert(state.cover_current&&!state.art_current);
    step(144);
    assert(!state.art_current&&lv_obj_get_style_image_opa(cover,0)==255);
    auto backdrop=lv_obj_get_child(lv_screen_active(),0);
    auto incoming=lv_obj_get_child(backdrop,1);
    assert(state.background_progress==0);
    step(110,"build/animation/background-fade.ppm");
    assert(state.background_progress==142);
    assert(!lv_obj_has_flag(incoming,LV_OBJ_FLAG_HIDDEN));
    step(220);
    assert(state.art_current&&lv_obj_get_style_image_opa(cover,0)==255);
    assert(state.background_progress==255);
    assert(lv_obj_has_flag(lv_obj_get_child(backdrop,0),LV_OBJ_FLAG_HIDDEN));
    printf("Heading slide %d: %d frames, max %llu pixels/frame, cue max %llu, cover fade %llu pixels/frame\n",dir,drawn,(unsigned long long)slide_pixels,(unsigned long long)cue_pixels,(unsigned long long)fade_pixels);
    // A late cover/album refresh with the same song must stay settled.
    load_assets("next-",1);step(500,"build/animation/07-late-artwork.ppm");assert(state.art_current&&state.transition.phase==T::Idle&&state.transition.starts==starts+1);
    assert(lv_obj_has_flag(cover_status,LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_get_style_image_opa(cover,0)==255);
    state.drag=dir*20;step(20);ui_spring();step(150);int middle=lv_obj_get_y(content);step(150);
    assert((dir>0?middle>=52:middle<=52)&&lv_obj_get_y(content)==52);
}

static void check_cover_changed_during_fade(){
    state.track_tag=10;load_assets("",10);state.transition.cancel();step(0);step(48);
    auto content=lv_obj_get_child(lv_screen_active(),1);
    auto clip=lv_obj_get_child(content,0);
    auto cover=lv_obj_get_child(clip,0);
    assert(state.cover_current&&lv_obj_get_style_image_opa(cover,0)<255);
    ui_track_received(11);state.track_tag=11;
    load_assets("next-",11);
    state.blobs[3].front_tag=10;
    step(0);step(160);
    assert(!state.cover_current&&lv_obj_has_flag(clip,LV_OBJ_FLAG_HIDDEN));
    state.blobs[3].front_tag=11;
    step(0);
    assert(state.cover_current&&lv_obj_get_style_image_opa(cover,0)==36);
    step(144);
    step(330);
    assert(state.art_current&&lv_obj_get_style_image_opa(cover,0)==255);
}

static void check_background_reveal(){
    constexpr uint16_t old_color=0x3186,new_color=0xb5b6;
    auto load_solid_background=[](uint32_t tag,uint16_t color){
        state.track_tag=tag;
        load_assets("",tag);
        auto& slot=state.blobs[2];
        for(size_t i=0;i<slot.image.data_size;i+=2){slot.front[i]=color&255;slot.front[i+1]=color>>8;}
        step(0);step(144);
    };
    load_solid_background(20,old_color);step(330);
    load_solid_background(21,new_color);step(55);
    auto backdrop=lv_obj_get_child(lv_screen_active(),0);
    auto source=static_cast<const lv_image_dsc_t*>(lv_image_get_src(lv_obj_get_child(backdrop,1)));
    assert(state.background_progress==78&&source->header.w==480&&source->header.h==320);
    auto pixel=[&](int x,int y){
        const size_t offset=y*source->header.stride+x*2;
        return uint16_t(source->data[offset]|(uint16_t(source->data[offset+1])<<8));
    };
    assert(pixel(134,160)==new_color&&pixel(134,0)==old_color&&pixel(479,160)==old_color);
    assert(pixel(255,160)!=old_color&&pixel(255,160)!=new_color);
    const auto before=flushed_pixels;
    step(55,"build/animation/background-reveal.ppm");
    assert(pixel(134,0)==new_color&&pixel(479,160)==old_color);
    assert(flushed_pixels-before<480*320);
    step(220);
    assert(state.art_current&&state.background_progress==255);
}
int main(){
    lv_init();auto d=lv_display_create(480,320);static uint16_t buffer[480*40];
    lv_display_set_color_format(d,LV_COLOR_FORMAT_RGB565);lv_display_set_buffers(d,buffer,nullptr,sizeof(buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(d,flush);
    lv_display_add_event_cb(d,[](lv_event_t*){state.transition.presented(now_ms());},LV_EVENT_RENDER_READY,nullptr);
    ui_init();save("build/offline.ppm");
    state.linked=true;state.clock_offset=480;save("build/idle-clock.ppm");
    auto idle=lv_obj_get_child(lv_screen_active(),4);
    auto sleeve=lv_obj_get_child(idle,0);
    auto info=lv_obj_get_child(idle,1);
    auto clock=lv_obj_get_child(info,1);
    assert(!lv_obj_has_flag(idle,LV_OBJ_FLAG_HIDDEN)&&strcmp(lv_label_get_text(clock),"--:--"));
    assert(lv_obj_get_x(sleeve)==26&&lv_obj_get_x(info)==266);
    auto idle_pixels=flushed_pixels;
    step(20);
    assert(flushed_pixels==idle_pixels);
    state.settings.cover=1;save("build/idle-clock-immersive.ppm");
    assert(lv_obj_get_x(sleeve)==0&&lv_obj_get_x(info)==236);
    state.settings.cover=0;
    state.linked=true;state.page=2;save("build/settings.ppm");
    assert(state.settings.reverse_gestures==0);
    ui_setting(420,143,false);ui_setting(420,143,true);
    assert(state.settings.reverse_gestures==1);
    save("build/settings-reversed.ppm");
    ui_setting(376,25,false);ui_setting(376,25,true);
    assert(state.guide);
    save("build/guide-reversed.ppm");
    state.guide=false;
    ui_setting(345,143,false);ui_setting(345,143,true);
    assert(state.settings.reverse_gestures==0);
    // Leave the production UI unchanged for 40 updates. Style assignments must
    // not invalidate the full background on every touch-sampling iteration.
    for(int i=0;i<5;i++){tick+=20;lv_tick_inc(20);ui_update();lv_timer_handler();lv_refr_now(d);}
    auto before=flushed_pixels;
    for(int i=0;i<40;i++){tick+=10;lv_tick_inc(10);ui_update();lv_timer_handler();lv_refr_now(d);}
    printf("Idle redraw pixels over 40 updates: %llu\n",(unsigned long long)(flushed_pixels-before));
    if(flushed_pixels-before>=480*320*2){fputs("Idle UI repaints the full background repeatedly\n",stderr);return 1;}
    state.page=1;state.last_sys=tick;state.cpu=42;state.mem=65;state.gpu=23;state.net=3600000;save("build/system.ppm");
    state.guide=true;save("build/guide.ppm");state.guide=false;state.pager=true;save("build/pager.ppm");
    state.pager=false;state.page=0;state.has_track=true;
    for(int id:{1,2,3,10,11,12,13,20,21,22,23}){
        char path[80];snprintf(path,sizeof(path),"build/blob-%d.bin",id);std::ifstream f(path,std::ios::binary);if(!f)continue;
        uint16_t w,h;uint8_t fmt,flags;f.read((char*)&w,2);f.read((char*)&h,2);f.read((char*)&fmt,1);f.read((char*)&flags,1);
        auto&s=state.blobs[id];std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),{});s.front=(uint8_t*)malloc(data.size());memcpy(s.front,data.data(),data.size());s.flags=flags;
        s.image.header.magic=LV_IMAGE_HEADER_MAGIC;s.image.header.w=w;s.image.header.h=h;s.image.header.cf=fmt==1?LV_COLOR_FORMAT_RGB565:LV_COLOR_FORMAT_A4;s.image.header.stride=fmt==1?w*2:(w+1)/2;s.image.data=s.front;s.image.data_size=data.size();
    }
    state.playing=true;state.dur=228;state.pos=72;state.playback_at=tick;save("build/music.ppm");
    assert(lv_obj_has_flag(idle,LV_OBJ_FLAG_HIDDEN));
    state.detail=true;save("build/detail.ppm");state.detail=false;state.settings.cover=1;save("build/immersive.ppm");
    check_cue_layout();
    check_transition(-1);check_transition(1);
    check_cover_changed_during_fade();
    check_background_reveal();
    puts("LVGL screenshots and delayed-reply animation checks passed");
}
