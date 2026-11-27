#include "app.hpp"
#include "ui_icons.hpp"
#include <ctime>
#include <cstdio>
#include <cstring>
#include <cmath>
extern void settings_changed();
LV_FONT_DECLARE(mc_font_13);
LV_FONT_DECLARE(mc_font_18);
LV_FONT_DECLARE(mc_font_30);
LV_FONT_DECLARE(mc_font_clock);
static lv_obj_t *root,*stage,*background,*cover_clip,*cover,*cover_status,*music,*heading,*title_clip,*title,*artist,*play_state,*progress,*elapsed,*remaining,*offline;
static lv_obj_t *idle_screen,*idle_sleeve,*idle_info,*idle_clock,*idle_date,*idle_hands[2];
static lv_point_precise_t idle_hand_points[2][2];
static lv_obj_t *system_page,*settings_page,*header,*clock_label,*page_name,*help,*detail,*badges,*mute_badge,*link_badge,*pager,*pager_left,*pager_right,*pager_name,*pager_tip,*guide,*hud,*hud_number,*hud_bar,*hint,*swipe_meter,*dots;
static lv_obj_t *sys_values[4],*sys_bars[4],*detail_values[4],*detail_rows[4],*brightness_value,*brightness_bar;
static constexpr int reverse_group=5,setting_groups=6;
static lv_obj_t *segments[setting_groups][3],*swipe_guide_actions[2];
static lv_obj_t *header_dots,*pager_dots;
static lv_obj_t *play_icon,*hint_icon,*hint_text,*pager_icons[2],*pager_labels[2];
static lv_image_dsc_t title_mask{};
static const uint8_t *bound[24]{};
static lv_obj_t *bound_objects[24]{};
static lv_image_dsc_t displayed[24]{};
static std::array<std::vector<uint8_t>,24> held_pixels;
static std::vector<uint8_t> title_mask_pixels;
static const char* page_names[]={"音乐","系统","设置"};
static const char* group_labels[]={"封面","背景","灵敏度","提示音","息屏","手势反转"};
static const char* group_options[setting_groups][3]={{"经典封面","沉浸封面",nullptr},{"模糊","纯色",nullptr},{"低","中","高"},{"开","关",nullptr},{"1分","5分","从不"},{"关","开",nullptr}};
static constexpr int counts[]={2,2,3,2,3,2};
static constexpr int gx[]={14,14,244,14,244,244},gy[]={114,180,180,246,246,114};
static constexpr int tile_width=222,group_left[]={42,42,56,56,42,68};
static int segment_width(int group){return (tile_width-group_left[group]-6)/counts[group];}
static uint32_t shown_tint=0x3c3f46;
static bool shown_dark=false,shown_cover=false;
static bool cover_seen=false;
static uint32_t shown_cover_tag=0;
static uint64_t cover_fade_start=0;
static constexpr uint64_t cover_fade_ms=144,cover_frame_ms=24;
static constexpr uint64_t background_reveal_ms=330,background_frame_ms=55;
static constexpr int reveal_feather=18;
static constexpr int bayer[4][4]={{0,8,2,10},{12,4,14,6},{3,11,1,9},{15,7,13,5}};
static struct {
    lv_obj_t *layer[2]{};
    lv_image_dsc_t images[2]{};
    std::vector<uint16_t> pixels[2],mixed_pixels;
    lv_image_dsc_t mixed{};
    const uint8_t* source=nullptr;
    int front=-1,incoming=-1,asset=0;
    int reveal_radius=0,center_x=134;
    uint32_t tag=0;
    uint64_t started=0;
} backdrop;
static int last_page=-1,last_cover=-1,last_play=-1;
static uint64_t scroll_start=0;static const uint8_t *last_title_data=nullptr;
static lv_color_t fg(){return lv_color_hex(shown_dark?0x161616:0xffffff);}
static lv_color_t tint(){return lv_color_hex(shown_tint);}
static void visible(lv_obj_t*o,bool show){if(show)lv_obj_remove_flag(o,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(o,LV_OBJ_FLAG_HIDDEN);}
static void position(lv_obj_t*o,int x,int y,int w,int h){lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);}
static lv_obj_t* box(lv_obj_t*parent,int x,int y,int w,int h,int radius=0,int opacity=0){
    auto o=lv_obj_create(parent);lv_obj_remove_style_all(o);position(o,x,y,w,h);lv_obj_set_style_radius(o,radius,0);lv_obj_set_style_bg_color(o,tint(),0);lv_obj_set_style_bg_opa(o,opacity,0);lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);return o;
}
static lv_obj_t* label(lv_obj_t*p,const char*text,int x,int y,int size=18){
    auto o=lv_label_create(p);lv_obj_remove_style_all(o);lv_label_set_text(o,text);lv_obj_set_pos(o,x,y);lv_obj_set_style_text_color(o,fg(),0);lv_obj_set_style_text_font(o,size==30?&mc_font_30:size==13?&mc_font_13:&mc_font_18,0);return o;
}
static void text(lv_obj_t*o,const char*v){if(strcmp(lv_label_get_text(o),v))lv_label_set_text(o,v);}
static lv_obj_t* image(lv_obj_t*p){return lv_image_create(p);}
static void set_glyph(lv_obj_t* object,const lv_image_dsc_t& glyph){
    if(lv_image_get_src(object)!=&glyph)lv_image_set_src(object,&glyph);
}
static lv_obj_t* glyph(lv_obj_t* parent,const lv_image_dsc_t& source,int x,int y){
    auto object=image(parent);
    set_glyph(object,source);
    lv_obj_set_pos(object,x,y);
    lv_obj_set_style_image_recolor(object,fg(),0);
    lv_obj_set_style_image_recolor_opa(object,LV_OPA_COVER,0);
    return object;
}
static lv_obj_t* bar(lv_obj_t*p,int x,int y,int w,int h){
    auto o=lv_bar_create(p);lv_obj_remove_style_all(o);position(o,x,y,w,h);lv_obj_set_style_bg_color(o,fg(),0);lv_obj_set_style_bg_opa(o,64,0);lv_obj_set_style_radius(o,h/2,0);lv_obj_set_style_bg_color(o,fg(),LV_PART_INDICATOR);lv_obj_set_style_bg_opa(o,255,LV_PART_INDICATOR);lv_obj_set_style_radius(o,h/2,LV_PART_INDICATOR);return o;
}
static void recolor_tree(lv_obj_t*o){
    if(o==idle_screen)return;
    lv_obj_set_style_text_color(o,fg(),0);
    if(lv_obj_check_type(o,&lv_image_class)){lv_obj_set_style_image_recolor(o,fg(),0);}
    else if(lv_obj_check_type(o,&lv_bar_class)){lv_obj_set_style_bg_color(o,fg(),0);lv_obj_set_style_bg_color(o,fg(),LV_PART_INDICATOR);}
    else if(lv_obj_get_style_bg_opa(o,0)>0)lv_obj_set_style_bg_color(o,tint(),0);
    for(uint32_t i=0;i<lv_obj_get_child_count(o);i++)recolor_tree(lv_obj_get_child(o,i));
}

template <int Width,int Height>
static void clock_gradient(lv_obj_t* object,uint16_t (&pixels)[Height][Width],
                           lv_image_dsc_t& texture,uint32_t from,uint32_t to,bool vertical){
    // A four-pixel repeating strip avoids large RGB565 bands with little memory.
    for(int y=0;y<Height;y++){
        for(int x=0;x<Width;x++){
            const float t=vertical?float(y)/(Height-1):float(x)/(Width-1);
            const float threshold=(bayer[y%4][x%4]+0.5f)/16;
            auto channel=[&](int shift,int levels){
                const int a=(from>>shift)&255,b=(to>>shift)&255;
                return int((a+(b-a)*t)*levels/255+threshold);
            };
            pixels[y][x]=(channel(16,31)<<11)|(channel(8,63)<<5)|channel(0,31);
        }
    }
    texture.header.magic=LV_IMAGE_HEADER_MAGIC;
    texture.header.cf=LV_COLOR_FORMAT_RGB565;
    texture.header.w=Width;
    texture.header.h=Height;
    texture.header.stride=Width*2;
    texture.data=reinterpret_cast<const uint8_t*>(pixels);
    texture.data_size=sizeof(pixels);
    lv_obj_set_style_bg_image_src(object,&texture,0);
    lv_obj_set_style_bg_image_tiled(object,true,0);
}

static void init_idle_screen(){
    static uint16_t background_pixels[4][480],sleeve_pixels[216][4];
    static lv_image_dsc_t background_texture{},sleeve_texture{};
    idle_screen=box(root,0,0,480,320,0,LV_OPA_COVER);
    clock_gradient(idle_screen,background_pixels,background_texture,0x282e40,0x151923,false);

    idle_sleeve=box(idle_screen,26,52,216,216,14,LV_OPA_COVER);
    lv_obj_set_style_clip_corner(idle_sleeve,true,0);
    clock_gradient(idle_sleeve,sleeve_pixels,sleeve_texture,0x536983,0x292f46,true);
    auto record=box(idle_sleeve,24,24,168,168,84,LV_OPA_COVER);
    lv_obj_set_style_bg_color(record,lv_color_hex(0x202a3c),0);
    lv_obj_set_style_border_color(record,lv_color_hex(0x647991),0);
    lv_obj_set_style_border_width(record,1,0);
    for(int radius:{75,69,32}){
        auto groove=box(idle_sleeve,108-radius,108-radius,2*radius,2*radius,radius);
        lv_obj_set_style_border_color(groove,lv_color_hex(0x34445b),0);
        lv_obj_set_style_border_width(groove,1,0);
    }
    for(int i=0;i<12;i++){
        const float angle=i*0.5235988f;
        const int size=i%3==0?4:2;
        int x=108+std::lround(std::sin(angle)*59)-size/2;
        int y=108-std::lround(std::cos(angle)*59)-size/2;
        auto mark=box(idle_sleeve,x,y,size,size,size/2,LV_OPA_COVER);
        lv_obj_set_style_bg_color(mark,lv_color_hex(i%3==0?0x9cacc1:0x677992),0);
    }
    for(int i=0;i<2;i++){
        idle_hands[i]=lv_line_create(idle_sleeve);
        lv_obj_remove_style_all(idle_hands[i]);
        position(idle_hands[i],0,0,216,216);
        lv_obj_set_style_line_width(idle_hands[i],i==0?4:2,0);
        lv_obj_set_style_line_rounded(idle_hands[i],true,0);
        lv_obj_set_style_line_color(idle_hands[i],lv_color_hex(i==0?0xe8eef8:0xa5c9eb),0);
    }
    auto spindle=box(idle_sleeve,104,104,8,8,4,LV_OPA_COVER);
    lv_obj_set_style_bg_color(spindle,lv_color_hex(0xd8e7f6),0);

    idle_info=box(idle_screen,266,85,190,145);
    auto eyebrow=label(idle_info,"本地时间",0,0,13);
    lv_obj_set_style_text_color(eyebrow,lv_color_hex(0xa6b4c8),0);
    idle_clock=label(idle_info,"--:--",0,20);
    lv_obj_set_style_text_font(idle_clock,&mc_font_clock,0);
    lv_obj_set_style_text_color(idle_clock,lv_color_hex(0xf1f5fb),0);
    auto divider=box(idle_info,0,92,190,1,0,LV_OPA_COVER);
    lv_obj_set_style_bg_color(divider,lv_color_hex(0x414b60),0);
    idle_date=label(idle_info,"",0,103);
    lv_obj_set_style_text_color(idle_date,lv_color_hex(0xb7c4d7),0);
    visible(idle_screen,false);
}

static void update_idle_screen(bool classic,bool valid,const struct tm& local,const char* clock){
    lv_obj_set_x(idle_sleeve,classic?26:0);
    lv_obj_set_x(idle_info,classic?266:236);
    text(idle_clock,clock);
    if(valid){
        static const char* weekdays[]={"周日","周一","周二","周三","周四","周五","周六"};
        char date[48];
        snprintf(date,sizeof(date),"%d月%d日  %s",local.tm_mon+1,local.tm_mday,weekdays[local.tm_wday]);
        text(idle_date,date);
    }else text(idle_date,"");

    static int displayed_minute=-2;
    const int minute=valid?local.tm_hour*60+local.tm_min:-1;
    if(displayed_minute==minute)return;
    displayed_minute=minute;
    for(int i=0;i<2;i++){
        visible(idle_hands[i],valid);
        if(!valid)continue;
        const float angle=(i==0?minute%720/720.f:local.tm_min/60.f)*6.2831853f;
        const int length=i==0?33:49;
        idle_hand_points[i][0]={108,108};
        idle_hand_points[i][1]={
            static_cast<lv_value_precise_t>(108+std::lround(std::sin(angle)*length)),
            static_cast<lv_value_precise_t>(108-std::lround(std::cos(angle)*length))
        };
        lv_line_set_points(idle_hands[i],idle_hand_points[i],2);
    }
}
static void bind(lv_obj_t*o,int id){
    auto&s=state.blobs[id];
    visible(o,s.image.data!=nullptr);
    if(s.image.data&&(bound[id]!=s.image.data||bound_objects[id]!=o)){
        // The front descriptor can be replaced by CDC while the old content
        // fades out. Keep each displayed descriptor stable until rebinding.
        lv_image_cache_drop(&displayed[id]);displayed[id]=s.image;lv_image_set_src(o,&displayed[id]);lv_obj_invalidate(o);bound[id]=s.image.data;bound_objects[id]=o;
    }
    if(id>=10){
        if(lv_obj_get_style_image_recolor_opa(o,0)!=255)lv_obj_set_style_image_recolor_opa(o,255,0);
        if(!lv_color_eq(lv_obj_get_style_image_recolor(o,0),fg()))lv_obj_set_style_image_recolor(o,fg(),0);
    }
}
static uint32_t red_blue_lanes(uint16_t pixel){
    // Split red and blue by 16 bits so the two products cannot carry into each other.
    return (uint32_t(pixel&0xf800)<<5)|(pixel&31);
}
struct RowSpan {int left,right;};
static RowSpan reveal_span(int radius,int center_x,int dy_squared,int width){
    const int within=radius*radius-dy_squared;
    if(radius<=0||within<0)return {center_x,center_x};
    const int reach=int(std::sqrt(float(within)));
    return {std::max(0,center_x-reach),std::min(width,center_x+reach+1)};
}
static void reveal_background(int progress){
    if(!progress||state.background_progress==progress)return;
    const int index=backdrop.incoming;
    const auto& incoming=backdrop.pixels[index];
    const auto* previous=backdrop.front>=0?backdrop.pixels[backdrop.front].data():nullptr;
    auto& mixed=backdrop.mixed_pixels;
    if(!backdrop.reveal_radius){
        if(previous)mixed.assign(previous,previous+incoming.size());
        else mixed.assign(incoming.size(),lv_color_to_u16(tint()));
    }
    const int w=backdrop.images[index].header.w,h=backdrop.images[index].header.h;
    const int cx=backdrop.center_x,cy=h/2;
    const int extent=int(std::ceil(std::hypot(float(std::max(cx,w-1-cx)),float(cy))))+reveal_feather;
    const int radius=extent*progress/255;
    const int inner=std::max(0,radius-reveal_feather),outer=radius+reveal_feather;
    const int inner_before=std::max(0,backdrop.reveal_radius-reveal_feather);
    const int outer_squared=outer*outer;
    const int feather_scale=(256<<16)/(outer_squared-inner*inner);
    const uint16_t base=lv_color_to_u16(tint());
    for(int y=std::max(0,cy-outer);y<std::min(h,cy+outer+1);y++){
        const int dy_squared=(y-cy)*(y-cy);
        const auto reach=reveal_span(outer,cx,dy_squared,w);
        const auto filled=reveal_span(inner,cx,dy_squared,w);
        const auto filled_before=reveal_span(inner_before,cx,dy_squared,w);
        const int row=y*w;
        // The interior already revealed in a previous frame is unchanged.
        std::copy_n(incoming.data()+row+filled.left,filled_before.left-filled.left,mixed.data()+row+filled.left);
        std::copy_n(incoming.data()+row+filled_before.right,filled.right-filled_before.right,mixed.data()+row+filled_before.right);
        const auto& thresholds=bayer[(y+1)&3];
        for(int x=reach.left;x<reach.right;x++){
            if(x==filled.left&&filled.left<filled.right){x=filled.right-1;continue;}
            const int offset=row+x;
            const int dx=x-cx;
            const int opacity=((outer_squared-dy_squared-dx*dx)*feather_scale)>>16;
            const uint16_t a=previous?previous[offset]:base,b=incoming[offset];
            const uint32_t bias=thresholds[(x+2)&3]*16+8;
            const uint32_t rb=(red_blue_lanes(a)*(256-opacity)+red_blue_lanes(b)*opacity+bias*0x10001u)>>8;
            const uint32_t green=(((a>>5)&63)*(256-opacity)+((b>>5)&63)*opacity+bias)>>8;
            mixed[offset]=((rb>>5)&0xf800)|(green<<5)|(rb&31);
        }
    }
    auto object=backdrop.layer[index];
    lv_image_cache_drop(&backdrop.mixed);
    backdrop.mixed=backdrop.images[index];
    backdrop.mixed.data=reinterpret_cast<const uint8_t*>(mixed.data());
    if(!backdrop.reveal_radius){
        lv_image_set_src(object,&backdrop.mixed);
        if(backdrop.front>=0)visible(backdrop.layer[backdrop.front],false);
        visible(object,true);
    }else{
        lv_area_t dirty{std::max(0,cx-outer),std::max(0,cy-outer),std::min(w-1,cx+outer),std::min(h-1,cy+outer)};
        lv_obj_invalidate_area(object,&dirty);
    }
    backdrop.reveal_radius=radius;
    state.background_progress=progress;
}
static void commit_background(){
    if(backdrop.front>=0)visible(backdrop.layer[backdrop.front],false);
    backdrop.front=backdrop.incoming;
    backdrop.incoming=-1;
    auto object=backdrop.layer[backdrop.front];
    lv_image_set_src(object,&backdrop.images[backdrop.front]);
    visible(object,true);
    state.background_progress=255;
}
static void update_background(int id,bool ready,bool animate,bool advance,uint64_t now){
    if(!state.linked||state.image_failures>=3){
        for(auto layer:backdrop.layer)visible(layer,false);
        backdrop.front=backdrop.incoming=-1;
        backdrop.source=nullptr;
        state.background_progress=255;
        return;
    }
    if(backdrop.incoming>=0){
        if(animate&&!advance)return;
        const auto age=now-backdrop.started;
        if(age>=background_reveal_ms||!animate)commit_background();
        else{
            const int left=int(background_reveal_ms-age/background_frame_ms*background_frame_ms);
            reveal_background(255-255*left*left/int(background_reveal_ms*background_reveal_ms));
            return;
        }
    }
    if(!ready)return;
    const auto& slot=state.blobs[id];
    if(backdrop.front>=0&&backdrop.asset==id&&backdrop.tag==slot.front_tag){
        if(backdrop.source==slot.image.data)return;
        const auto& shown=backdrop.images[backdrop.front];
        if(shown.data_size==slot.image.data_size&&!memcmp(shown.data,slot.image.data,shown.data_size)){
            backdrop.source=slot.image.data;
            return;
        }
    }

    const int index=backdrop.front==0?1:0;
    auto object=backdrop.layer[index];
    auto& image=backdrop.images[index];
    auto& pixels=backdrop.pixels[index];
    // Keep both frames stable while USB reuses its image buffers.
    lv_image_cache_drop(&image);
    pixels.resize(slot.image.data_size/sizeof(uint16_t));
    memcpy(pixels.data(),slot.image.data,slot.image.data_size);
    image=slot.image;
    image.data=reinterpret_cast<const uint8_t*>(pixels.data());
    lv_obj_move_foreground(object);
    backdrop.source=slot.image.data;
    backdrop.asset=id;
    backdrop.tag=slot.front_tag;
    backdrop.incoming=index;
    backdrop.started=now;
    backdrop.reveal_radius=0;
    // Classic cover bounds are (26,52) + 216x216; the immersive artwork occupies the left 236 pixels.
    backdrop.center_x=id==1?118:134;
    state.background_progress=animate?0:255;
    if(!animate)commit_background();
}
static void hold_music_pixels(){
    // USB may refill the old PSRAM back buffers while the current song stays
    // visible. Copy only its small displayed content, never redraw a snapshot.
    for(int id:{3,state.settings.cover?10:11,state.settings.cover?12:13}){
        auto object=bound_objects[id];auto&image=displayed[id];
        if(!object||!image.data||lv_image_get_src(object)!=&image)continue;
        auto&pixels=held_pixels[id];if(image.data==pixels.data())continue;
        pixels.assign(image.data,image.data+image.data_size);
        lv_image_cache_drop(&image);image.data=pixels.data();bound[id]=nullptr;
        lv_image_set_src(object,&image);
    }
}
static void fade(lv_obj_t*o,int duration){lv_obj_fade_in(o,duration,0);}
static void dots_at(lv_obj_t*p,int x,int y){for(int i=0;i<3;i++){auto d=box(p,x,y,i==state.page?16:6,6,3,i==state.page?255:77);lv_obj_set_style_bg_color(d,fg(),0);x+=(i==state.page?16:6)+6;}}
static void update_dots(){lv_obj_clean(dots);dots_at(dots,12,7);}
void ui_spring(){state.drag=0;}
void ui_animate_track(int direction){
    state.transition_frames=state.transition_render_max_ms=0;
    hold_music_pixels();
    state.transition.request(now_ms(),direction);
    state.hint=direction>0?"下一首":"上一首";state.hint_until=now_ms()+1200;
    state.drag=0;
}
void ui_track_received(uint32_t tag){
    if(state.transition.phase!=mc::TrackTransition::Holding){
        state.transition_frames=state.transition_render_max_ms=0;hold_music_pixels();
    }
    if(tag!=state.track_tag)cover_seen=false;
    state.transition.received(now_ms(),tag);state.drag=0;
}

void ui_init(){
    root=lv_screen_active();lv_obj_remove_style_all(root);lv_obj_set_style_bg_opa(root,255,0);lv_obj_set_style_bg_color(root,tint(),0);
    background=box(root,0,0,480,320);
    for(auto& layer:backdrop.layer){layer=image(background);visible(layer,false);}
    stage=box(root,26,52,430,216);
    cover_clip=box(stage,0,0,216,216,14);lv_obj_set_style_clip_corner(cover_clip,true,0);
    cover=image(cover_clip);
    cover_status=label(root,"封面同步中",26,148);
    lv_obj_set_style_text_align(cover_status,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_set_style_text_opa(cover_status,LV_OPA_80,0);
    music=box(stage,240,0,190,216);
    heading=box(music,0,0,190,102);
    title_clip=box(heading,0,0,190,72);title=image(title_clip);artist=image(heading);
    play_icon=glyph(music,icons::pause,0,169);
    play_state=label(music,"已暂停",17,167,13);
    progress=bar(music,0,188,190,4);elapsed=label(music,"0:00",0,198,13);remaining=label(music,"-0:00",139,198,13);
    offline=box(root,40,110,440,110);label(offline,"未连接配套程序",0,0,30);label(offline,"手势控制仍可用，歌曲信息需要电脑端程序",0,48,13);
    init_idle_screen();
    system_page=box(root,0,48,480,272);
    const char*names[]={"处理器","内存","显卡","网络"};
    for(int i=0;i<4;i++){int x=18+(i%2)*238,y=53+(i/2)*104;label(system_page,names[i],x,y,13);sys_values[i]=label(system_page,"--",x,y+20,30);sys_bars[i]=bar(system_page,x,y+61,206,4);lv_obj_set_style_anim_duration(sys_bars[i],600,0);}
    settings_page=box(root,0,0,480,320);
    auto bright=box(settings_page,14,58,452,48,12,140);lv_obj_set_style_clip_corner(bright,true,0);lv_obj_set_style_border_width(bright,1,0);lv_obj_set_style_border_color(bright,fg(),0);lv_obj_set_style_border_opa(bright,64,0);brightness_bar=bar(bright,0,0,452,48);lv_obj_set_style_radius(brightness_bar,0,0);lv_obj_set_style_radius(brightness_bar,0,LV_PART_INDICATOR);lv_obj_set_style_bg_opa(brightness_bar,0,0);lv_obj_set_style_bg_opa(brightness_bar,56,LV_PART_INDICATOR);label(bright,"亮度",14,13);brightness_value=label(bright,"80%",376,13);
    for(int i=0;i<setting_groups;i++){
        auto tile=box(settings_page,gx[i],gy[i],tile_width,58,12,140);lv_obj_set_style_border_width(tile,1,0);lv_obj_set_style_border_color(tile,fg(),0);lv_obj_set_style_border_opa(tile,64,0);label(tile,group_labels[i],8,20,13);
        int left=group_left[i],width=segment_width(i);
        auto group=box(tile,left,7,width*counts[i],44,10);lv_obj_set_style_clip_corner(group,true,0);lv_obj_set_style_border_width(group,1,0);lv_obj_set_style_border_color(group,fg(),0);
        for(int j=0;j<counts[i];j++){
            auto seg=box(group,j*width,0,width,44,0,0);lv_obj_set_style_border_width(seg,j<counts[i]-1?1:0,0);lv_obj_set_style_border_side(seg,LV_BORDER_SIDE_RIGHT,0);lv_obj_set_style_border_color(seg,fg(),0);
            auto l=label(seg,group_options[i][j],0,0);lv_obj_center(l);segments[i][j]=seg;
        }
    }
    header=box(root,0,4,480,44);page_name=label(header,"系统",18,12);clock_label=label(header,"--:--",402,12);
    header_dots=box(header,64,19,60,6);
    help=box(header,354,0,44,44,22);lv_obj_set_style_border_width(help,2,0);lv_obj_set_style_border_color(help,fg(),0);
    lv_obj_center(glyph(help,icons::help,0,0));
    detail=box(root,250,0,230,320,0,209);
    const char *dn[]={"专辑","出处","时长","曲目"};
    for(int i=0;i<4;i++){detail_rows[i]=box(detail,16,52+i*43,200,43);label(detail_rows[i],dn[i],0,12,13);detail_values[i]=image(detail_rows[i]);lv_obj_set_pos(detail_values[i],46,9);}
    label(detail,"轻点任意处关闭",16,240,13);
    badges=box(root,12,12,140,24);mute_badge=box(badges,0,0,48,24,12,224);label(mute_badge,"静音",8,3,13);link_badge=box(badges,54,0,68,24,12,224);label(link_badge,"未连接",8,3,13);
    hint=box(root,0,138,0,0,32,230);
    lv_obj_set_style_bg_color(hint,lv_color_hex(0x14161b),0);
    hint_text=label(hint,"",20,8);
    lv_obj_set_style_text_color(hint_text,lv_color_white(),0);
    hint_icon=glyph(hint,icons::play,20,0);
    lv_obj_set_style_image_recolor(hint_icon,lv_color_white(),0);
    swipe_meter=bar(root,180,82,120,3);lv_obj_set_style_bg_color(swipe_meter,lv_color_hex(0xffffff),0);lv_obj_set_style_bg_color(swipe_meter,lv_color_hex(0xffffff),LV_PART_INDICATOR);
    lv_bar_set_mode(swipe_meter,LV_BAR_MODE_RANGE);
    hud=box(root,90,112,300,100,12,240);label(hud,"音量",20,20);hud_number=label(hud,"40%",170,16,30);hud_bar=bar(hud,20,66,260,10);
    dots=box(root,200,288,80,20,10,204);
    pager=box(root,0,0,480,320,0,184);pager_name=label(pager,"音乐",0,94,30);pager_tip=label(pager,"按住往左或往右拖\n不拖直接松手：查看歌曲详情",110,175,13);
    lv_obj_set_width(pager_tip,260);lv_obj_set_style_text_align(pager_tip,LV_TEXT_ALIGN_CENTER,0);
    pager_dots=box(pager,217,146,60,6);
    pager_left=box(pager,14,128,96,64,12);lv_obj_set_style_border_width(pager_left,2,0);
    pager_labels[0]=label(pager_left,"设置",31,21);
    pager_icons[0]=glyph(pager_left,icons::chevron_left,13,25);
    pager_right=box(pager,370,128,96,64,12);lv_obj_set_style_border_width(pager_right,2,0);
    pager_labels[1]=label(pager_right,"系统",17,21);
    pager_icons[1]=glyph(pager_right,icons::chevron_right,65,25);
    guide=box(root,0,0,480,320,0,224);label(guide,"手势说明",60,27);
    auto rule=box(guide,60,53,360,2,0,255);lv_obj_set_style_bg_color(rule,fg(),0);
    const char* gestures[]={"双指","双指","单指 点一下","双指 点两下","长按后","长按后 松手"};
    const char* actions[]={"调音量","下一首 / 上一首","暂停 / 播放","静音","切换页面","歌曲详情"};
    for(int i=0;i<6;i++){label(guide,gestures[i],60,60+i*33);auto l=label(guide,actions[i],0,60+i*33);lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_RIGHT,0);position(l,240,60+i*33,180,24);if(i<2)swipe_guide_actions[i]=l;auto line=box(guide,60,88+i*33,360,1,0,64);lv_obj_set_style_bg_color(line,fg(),0);}
    glyph(guide,icons::arrow_left,104,65);
    glyph(guide,icons::arrow_right,123,65);
    glyph(guide,icons::arrow_down,104,98);
    label(guide,"/",121,93);
    glyph(guide,icons::arrow_up,133,98);
    glyph(guide,icons::arrow_left,123,197);
    glyph(guide,icons::arrow_right,142,197);
    label(guide,"点任意处关闭",188,274,13);
}

static int selected(int group){
    auto&s=state.settings;
    switch(group){
    case 0:return s.cover;
    case 1:return s.bg;
    case 2:return s.sens;
    case 3:return !s.sound;
    case reverse_group:return s.reverse_gestures;
    default:return s.sleep==1?0:s.sleep==5?1:2;
    }
}
static void segment_style(){
    static std::array<int,8> previous{-1,-1,-1,-1,-1,-1,-1,-1};
    auto&s=state;std::array<int,8> current{s.settings.cover,s.settings.bg,s.settings.sens,s.settings.sound,s.settings.sleep,s.settings.reverse_gestures,int(shown_tint),shown_dark};
    if(current==previous)return;previous=current;
    for(int i=0;i<setting_groups;i++)for(int j=0;j<counts[i];j++){
    auto seg=segments[i][j];bool on=selected(i)==j;lv_obj_set_style_bg_color(seg,on?fg():tint(),0);lv_obj_set_style_bg_opa(seg,on?255:0,0);lv_obj_set_style_border_color(seg,fg(),0);lv_obj_set_style_text_color(lv_obj_get_child(seg,0),on?tint():fg(),0);
}}
void ui_setting(int x,int y,bool release,bool cancel){
    static bool active=false,sliding=false,blocked=false;static Settings before;
    if(cancel){if(active&&sliding){state.settings.brightness=before.brightness;board_brightness(before.brightness);}active=sliding=false;blocked=true;return;}
    if(state.page!=2||state.pager||state.guide)return;
    if(!active&&!release){before=state.settings;active=true;blocked=false;sliding=x>=14&&x<466&&y>=58&&y<106;}
    if(sliding&&!blocked){int value=std::clamp((x-14)*100/452,10,100);if(value!=state.settings.brightness){state.settings.brightness=value;board_brightness(value);settings_changed();}}
    if(!release)return;
    active=sliding=false;if(blocked){blocked=false;return;}
    if(x>=354&&x<398&&y>=4&&y<48){state.guide=true;return;}
    for(int i=0;i<setting_groups;i++)if(x>=gx[i]&&x<gx[i]+tile_width&&y>=gy[i]+7&&y<gy[i]+51){
        int local=x-gx[i]-group_left[i],width=segment_width(i);if(local<0)continue;
        int option=std::min(local/width,counts[i]-1);auto&s=state.settings;
        if(i==0)s.cover=option;if(i==1){s.bg=option;uint8_t mode=option;send_frame(0x92,&mode,1);}if(i==2)s.sens=option;if(i==3)s.sound=!option;if(i==4)s.sleep=option==0?1:option==1?5:0;
        if(i==reverse_group)s.reverse_gestures=option;
        settings_changed();segment_style();break;
    }
}
void ui_update(){
    const auto now=now_ms();auto&s=state;
    if(!s.linked){s.transition.cancel();s.drag=0;}
    int title_id=s.settings.cover?10:11;
    int artist_id=s.settings.cover?12:13;
    auto current_image=[&](int id){return s.blobs[id].image.data&&s.blobs[id].front_tag==s.track_tag;};
    int background_id=s.page==0&&s.settings.cover?1:2;
    bool classic=!s.settings.cover;
    bool was_holding=s.transition.phase==mc::TrackTransition::Holding;
    bool heading_ready=current_image(title_id)&&current_image(artist_id)&&s.transition.target==s.track_tag;
    s.transition.tick(now,heading_ready);
    bool frozen=s.transition.phase==mc::TrackTransition::Holding;
    if(was_holding&&!frozen&&(s.hint=="下一首"||s.hint=="上一首"))s.hint.clear();
    bool block=s.pager||s.guide;
    if(s.transition.phase==mc::TrackTransition::Committing&&(s.page!=0||block||s.detail||!s.has_track))s.transition.cancel();
    bool sliding=s.transition.phase==mc::TrackTransition::Committing||s.transition.phase==mc::TrackTransition::Entering;
    bool cover_fading=false;
    if(!frozen){
        shown_cover=s.linked&&s.has_track&&!sliding&&s.page==0&&classic&&current_image(3);
        if(!s.linked)cover_seen=false;
        if(shown_cover){
            if(!cover_seen||shown_cover_tag!=s.track_tag){
                shown_cover_tag=s.track_tag;
                cover_seen=true;
                cover_fade_start=now;
            }
            const uint64_t age=std::min(now-cover_fade_start,cover_fade_ms);
            cover_fading=age<cover_fade_ms;
            const int remaining=int(cover_fade_ms-age/cover_frame_ms*cover_frame_ms);
            const int opacity=255-219*remaining*remaining/int(cover_fade_ms*cover_fade_ms);
            if(lv_obj_get_style_image_opa(cover,0)!=opacity)lv_obj_set_style_image_opa(cover,opacity,0);
        }
    }
    // Start the large blend after the cover animation so it can stay responsive.
    const bool advance_background=!frozen&&!sliding&&!cover_fading&&!s.drag;
    const bool background_ready=advance_background&&current_image(background_id);
    update_background(background_id,background_ready,s.linked&&s.has_track&&s.page==0&&!block&&!s.detail,advance_background,now);
    const bool current_background=backdrop.source&&backdrop.asset==background_id&&backdrop.tag==s.track_tag;
    const bool settled_background=current_background&&backdrop.incoming<0;
    s.cover_current=!frozen&&shown_cover;
    s.art_current=!frozen&&settled_background&&(s.page!=0||!classic||shown_cover);
    bool changed_theme=!frozen&&settled_background&&(shown_tint!=s.tint||shown_dark!=s.dark);
    if(changed_theme){shown_tint=s.tint;shown_dark=s.dark;recolor_tree(root);segment_style();}
    lv_color_t root_color=s.linked?tint():lv_color_hex(0x42454c);
    if(!lv_color_eq(lv_obj_get_style_bg_color(root,0),root_color))lv_obj_set_style_bg_color(root,root_color,0);
    int width=classic?190:226,bottom=classic?216:266;
    position(stage,classic?26:236,classic?52:36,classic?430:226,classic?216:266);
    position(music,classic?240:0,0,width,bottom);
    if(!frozen){
        if(shown_cover)bind(cover,3);else visible(cover,false);
    }
    visible(music,s.linked&&s.has_track&&s.page==0&&!block&&!s.detail);
    visible(cover_clip,s.linked&&s.has_track&&s.page==0&&classic&&shown_cover);
    const bool show_cover_status=s.linked&&s.has_track&&s.page==0&&!block&&!s.detail&&!(classic?shown_cover:current_background)&&!frozen;
    visible(cover_status,show_cover_status);
    if(show_cover_status){
        lv_obj_set_x(cover_status,classic?26:0);
        lv_obj_set_width(cover_status,classic?216:236);
        text(cover_status,s.image_failures>=3?"封面同步失败":"封面同步中");
    }
    lv_obj_set_y(heading,s.transition.offset(now));
    visible(offline,!s.linked&&s.page==0&&!block);
    const bool show_idle=s.linked&&!s.has_track&&s.page==0&&!block;
    visible(idle_screen,show_idle);
    visible(system_page,s.page==1&&!block);visible(settings_page,s.page==2&&!block);visible(header,s.page!=0&&!block);visible(help,s.page==2);
    if(last_page!=s.page||changed_theme){if(last_page!=s.page)fade(s.page==1?system_page:s.page==2?settings_page:music,150);update_dots();lv_obj_clean(header_dots);dots_at(header_dots,0,0);lv_obj_clean(pager_dots);dots_at(pager_dots,0,0);last_page=s.page;}
    if(!frozen&&(last_cover!=s.settings.cover||last_play!=s.playing)){
        last_cover=s.settings.cover;last_play=s.playing;
        lv_image_set_pivot(cover,108,108);
        lv_anim_t a;lv_anim_init(&a);lv_anim_set_var(&a,cover);lv_anim_set_values(&a,lv_image_get_scale(cover),s.playing?256:225);lv_anim_set_duration(&a,250);
        auto scale=[](void*o,int32_t v){lv_image_set_scale((lv_obj_t*)o,v);};
        if(s.transition.phase==mc::TrackTransition::Committing){
            lv_anim_delete(cover,scale);scale(cover,s.playing?256:225);
        }else{lv_anim_set_exec_cb(&a,scale);lv_anim_start(&a);}
    }
    if(!frozen){
    if(s.blobs[title_id].image.data!=last_title_data){last_title_data=s.blobs[title_id].image.data;scroll_start=now;}
    bind(title,title_id);bind(artist,artist_id);
    auto&ti=s.blobs[title_id];int title_height=ti.image.data?ti.image.header.h:36;
    position(heading,0,s.transition.offset(now),width,title_height+30);
    position(title_clip,0,0,width,title_height);lv_obj_set_pos(artist,0,title_height+6);
    if(ti.flags&1){
        if(title_mask.header.w!=width||title_mask.header.h!=title_height){
            title_mask_pixels.resize(width*title_height);
            for(int yy=0;yy<title_height;yy++)for(int xx=0;xx<width;xx++)title_mask_pixels[yy*width+xx]=std::clamp(int((width-1-xx)/(width*.12)*255),0,255);
            title_mask={};title_mask.header.magic=LV_IMAGE_HEADER_MAGIC;title_mask.header.cf=LV_COLOR_FORMAT_A8;title_mask.header.w=width;title_mask.header.h=title_height;title_mask.header.stride=width;title_mask.data=title_mask_pixels.data();title_mask.data_size=title_mask_pixels.size();
        }
        if(lv_obj_get_style_bitmap_mask_src(title_clip,0)!=&title_mask)lv_obj_set_style_bitmap_mask_src(title_clip,&title_mask,0);
    }else if(lv_obj_get_style_bitmap_mask_src(title_clip,0))lv_obj_set_style_bitmap_mask_src(title_clip,nullptr,0);
    if((ti.flags&1)&&ti.image.data){
        float d=std::max(0,int(ti.image.header.w)-width+24),move=d/30*1000,cycle=2*(1200+move),t=fmod(float(now-scroll_start),cycle),v=0;
        auto ease=[](float p){return .5f-std::cos(3.14159265f*p)/2;};
        if(t>=1200&&t<1200+move)v=ease((t-1200)/move)*d;
        else if(t>=1200+move&&t<2400+move)v=d;
        else if(t>=2400+move)v=(1-ease((t-2400-move)/move))*d;
        lv_obj_set_x(title,-v);
    }else lv_obj_set_x(title,0);
    lv_obj_set_pos(play_state,17,bottom-49);
    lv_obj_set_y(play_icon,bottom-47);
    set_glyph(play_icon,s.playing?icons::play:icons::pause);
    text(play_state,s.playing?"播放中":"已暂停");
    position(progress,0,bottom-28,width,4);
    int pos=std::min(s.dur,s.pos+int(s.playing&&s.linked?(now-s.playback_at)/1000:0));
    lv_bar_set_value(progress,s.dur?pos*100/s.dur:0,LV_ANIM_OFF);
    char b[80];snprintf(b,sizeof(b),"%d:%02d",pos/60,pos%60);text(elapsed,b);lv_obj_set_pos(elapsed,0,bottom-18);
    snprintf(b,sizeof(b),"-%d:%02d",std::max(0,s.dur-pos)/60,std::max(0,s.dur-pos)%60);text(remaining,b);lv_obj_set_pos(remaining,width-lv_obj_get_width(remaining),bottom-18);
    }
    char b[80];
    text(page_name,page_names[s.page]);
    const time_t utc=time(nullptr);
    const time_t tt=utc+s.clock_offset*60;
    struct tm tm{};
#ifdef _WIN32
    gmtime_s(&tm,&tt);
#else
    gmtime_r(&tt,&tm);
#endif
    const bool clock_valid=utc>=1577836800;
    if(clock_valid)snprintf(b,sizeof(b),"%d:%02d",tm.tm_hour,tm.tm_min);
    else snprintf(b,sizeof(b),"--:--");
    text(clock_label,b);
    if(show_idle)update_idle_screen(classic,clock_valid,tm,b);
    lv_obj_set_x(clock_label,462-lv_obj_get_width(clock_label));
    uint8_t values[]={s.cpu,s.mem,s.gpu};bool stale=!s.linked||now-s.last_sys>5000;
    for(int i=0;i<4;i++){
        bool missing=stale||(i<3&&values[i]==255);
        if(missing)snprintf(b,sizeof(b),"--");else if(i==3)snprintf(b,sizeof(b),"%.1f MB/s",s.net/1000000.);else snprintf(b,sizeof(b),"%d%%",values[i]);
        text(sys_values[i],b);lv_bar_set_value(sys_bars[i],missing?0:i==3?std::min(100,int(s.net/100000)):values[i],LV_ANIM_ON);
    }
    snprintf(b,sizeof(b),"%d%%",s.settings.brightness);text(brightness_value,b);lv_bar_set_value(brightness_bar,s.settings.brightness,LV_ANIM_OFF);segment_style();
    visible(detail,s.detail&&s.page==0&&!block&&s.linked);lv_obj_set_x(detail,classic?250:220);lv_obj_set_width(detail,classic?230:260);
    int row=0;for(int i=0;i<4;i++){
        auto&v=s.blobs[20+i];bool any=false;if(v.image.data)for(size_t j=0;j<v.image.data_size;j++)if(v.image.data[j]){any=true;break;}
        visible(detail_rows[i],any);if(any){bind(detail_values[i],20+i);lv_obj_set_y(detail_rows[i],(classic?52:50)+(row++)*43);}
    }
    visible(mute_badge,s.linked&&s.muted);visible(link_badge,!s.linked&&s.page!=0);visible(badges,!block);lv_obj_set_pos(badges,s.page?340:12,s.page?282:12);
    if(changed_theme)for(auto badge:{mute_badge,link_badge}){lv_obj_set_style_bg_color(badge,lv_color_hex(0x14161b),0);lv_obj_set_style_text_color(lv_obj_get_child(badge,0),lv_color_white(),0);}
    visible(hud,now<s.hud_until&&!block);
    if(s.linked){if(s.muted)snprintf(b,sizeof(b),"静音");else snprintf(b,sizeof(b),"%d%%",s.vol);}else snprintf(b,sizeof(b),"音量 %s",s.offline_delta<0?"-":"+");
    text(hud_number,b);lv_bar_set_value(hud_bar,s.linked?s.vol:std::min(100,std::abs(s.offline_delta)),LV_ANIM_OFF);
    const bool track_hint=s.drag||s.hint=="下一首"||s.hint=="上一首";
    const bool lower_hint=s.page==0&&(s.drag<0||(!s.drag&&s.hint=="上一首"));
    const int hint_y=track_hint?(lower_hint?240:48):138;
    // The lower cue sits over the artwork area, clear of the song timeline.
    const int hint_center=lower_hint?(classic?134:118):240;
    if(changed_theme){
        lv_obj_set_style_bg_color(hint,lv_color_hex(0x14161b),0);
        lv_obj_set_style_text_color(hint_text,lv_color_white(),0);
        lv_obj_set_style_image_recolor(hint_icon,lv_color_white(),0);
        lv_obj_set_style_bg_color(swipe_meter,lv_color_white(),0);
        lv_obj_set_style_bg_color(swipe_meter,lv_color_white(),LV_PART_INDICATOR);
    }
    bool waiting_cue=frozen&&!s.drag&&(s.hint=="下一首"||s.hint=="上一首");
    bool show_hint=(waiting_cue||now<s.hint_until)&&!s.hint.empty()&&!block;
    visible(hint,show_hint);
    if(show_hint){
        const lv_image_dsc_t* symbol=nullptr;
        if(s.drag){
            if(s.settings.reverse_gestures)symbol=s.drag>0?&icons::arrow_left:&icons::arrow_right;
            else symbol=s.drag>0?&icons::arrow_down:&icons::arrow_up;
        }
        else if(s.hint=="继续播放")symbol=&icons::play;
        else if(s.hint=="已暂停")symbol=&icons::pause;
        visible(hint_icon,symbol!=nullptr);
        text(hint_text,s.hint.c_str());
        lv_obj_update_layout(hint_text);
        int text_width=lv_obj_get_width(hint_text);
        int height=lv_obj_get_height(hint_text)+16;
        int icon_space=symbol?symbol->header.w+6:0;
        int width=text_width+icon_space+40;
        lv_obj_set_x(hint_text,20+(s.drag?0:icon_space));
        if(symbol){
            set_glyph(hint_icon,*symbol);
            lv_obj_set_pos(hint_icon,s.drag?26+text_width:20,(height-symbol->header.h)/2);
        }
        position(hint,hint_center-width/2,hint_y,width,height);
        lv_obj_set_pos(swipe_meter,hint_center-60,lower_hint?hint_y-10:hint_y+height+7);
    }
    visible(swipe_meter,show_hint&&(waiting_cue||s.drag));
    int sweep=waiting_cue?int((now-s.transition.started)%880)/5:0;
    int start=waiting_cue?std::min(sweep,175-sweep):0;
    int end=waiting_cue?start+12:std::min(100,std::abs(s.drag)*100/30);
    if(end<lv_bar_get_start_value(swipe_meter))lv_bar_set_start_value(swipe_meter,start,LV_ANIM_OFF);
    lv_bar_set_value(swipe_meter,end,LV_ANIM_OFF);
    lv_bar_set_start_value(swipe_meter,start,LV_ANIM_OFF);
    visible(dots,now<s.dots_until&&!block);
    visible(pager,s.pager);visible(guide,s.guide);
    if(s.guide){
        text(swipe_guide_actions[0],s.settings.reverse_gestures?"下一首 / 上一首":"调音量");
        text(swipe_guide_actions[1],s.settings.reverse_gestures?"减小 / 增大音量":"下一首 / 上一首");
    }
    if(s.pager){
        text(pager_name,page_names[s.page]);lv_obj_update_layout(pager_name);lv_obj_set_x(pager_name,(480-lv_obj_get_width(pager_name))/2);
        text(pager_labels[0],page_names[(s.page+2)%3]);
        text(pager_labels[1],page_names[(s.page+1)%3]);
        text(pager_tip,s.page==0&&s.linked?"按住往左或往右拖\n不拖直接松手：查看歌曲详情":"按住往左或往右拖\n不拖直接松手：取消");
        for(int i=0;i<2;i++){
            auto side=i?pager_right:pager_left;
            bool hit=i?s.pager_dx>=60:s.pager_dx<=-60;
            lv_obj_set_style_bg_color(side,fg(),0);
            lv_obj_set_style_bg_opa(side,hit?255:0,0);
            lv_obj_set_style_border_color(side,fg(),0);
            lv_obj_set_style_text_color(pager_labels[i],hit?tint():fg(),0);
            lv_obj_set_style_image_recolor(pager_icons[i],hit?tint():fg(),0);
        }
    }
}
