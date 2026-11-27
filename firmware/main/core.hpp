#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>
#ifdef ESP_PLATFORM
#include "esp_rom_crc.h"
#endif

namespace mc {
constexpr size_t MAX_PAYLOAD=16384;
inline uint16_t crc16(const uint8_t *p,size_t n) {
    uint16_t c=0xffff;
    while(n--) { c^=uint16_t(*p++)<<8; for(int i=0;i<8;i++) c=(c&0x8000)?(c<<1)^0x1021:c<<1; }
    return c;
}
inline uint32_t crc32(const uint8_t *p,size_t n) {
#ifdef ESP_PLATFORM
    return esp_rom_crc32_le(0,p,n);
#else
    uint32_t c=~0u;
    while(n--) { c^=*p++; for(int i=0;i<8;i++) c=(c>>1)^((0u-(c&1))&0xedb88320); }
    return ~c;
#endif
}
inline uint32_t u32(const uint8_t *p) {return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
inline uint16_t u16(const uint8_t *p) {return uint16_t(p[0])|(uint16_t(p[1])<<8);}
inline void put32(std::vector<uint8_t>&v,uint32_t x) {for(int i=0;i<4;i++)v.push_back(x>>(8*i));}
inline std::vector<uint8_t> frame(uint8_t type,uint8_t seq,const uint8_t*p,size_t n) {
    std::vector<uint8_t> v{0xa5,0x5a,type,seq}; put32(v,n);
    if(n)v.insert(v.end(),p,p+n);
    auto c=crc16(v.data()+2,v.size()-2);v.push_back(c);v.push_back(c>>8);return v;
}
class Parser {
    std::vector<uint8_t> buf;
public:
    std::function<void(uint8_t,const uint8_t*,size_t)> receive;
    void feed(const uint8_t*p,size_t n) {
        buf.insert(buf.end(),p,p+n);
        while(buf.size()>=2) {
            if(buf[0]!=0xa5||buf[1]!=0x5a){buf.erase(buf.begin());continue;}
            if(buf.size()<8)return;
            size_t len=u32(buf.data()+4);
            if(len>MAX_PAYLOAD){buf.erase(buf.begin());continue;}
            if(buf.size()<len+10)return;
            if(crc16(buf.data()+2,len+6)!=u16(buf.data()+8+len)){buf.erase(buf.begin());continue;}
            receive(buf[2],buf.data()+8,len);buf.erase(buf.begin(),buf.begin()+len+10);
        }
    }
};
struct Point {float x=0,y=0;int id=-1;};
inline bool elapsed(uint64_t now,uint64_t since,uint64_t delay){return now>=since&&now-since>=delay;}
// FT6336 stores two contact records even when one finger has just lifted.
// Ignore UP/NONE records rather than taking the first TD_STATUS records blindly.
inline int decode_touch(const uint8_t* data,size_t size,Point* points){
    if(size<13)return -1;
    int reported=data[0]&15;if(reported>2)return -1;if(!reported)return 0;
    int count=0;
    for(int i=0;i<2&&count<reported;i++){
        const uint8_t*r=data+1+i*6;int event=r[0]>>6;
        int x=((r[0]&15)<<8)|r[1],y=((r[2]&15)<<8)|r[3];
        if((event==0||event==2)&&x<320&&y<480)points[count++]={float(y),float(319-x),r[2]>>4};
    }
    return count?count:-1;
}
enum class Action {Begin,TwoBegin,Tap,Long,Pager,PageEnd,Volume,VolumeEnd,Drag,Track,Spring,Mute,Control,ControlEnd,CancelControl};
struct Gesture {
    std::function<void(Action,float,float)> emit;
    int max=0,axis=0,previous=0;
    bool active=false,long_on=false,cancel_long=false,frozen=false,all_up=false;
    uint64_t start=0,two_start=0,last_two=0,released=0,single_due=0,double_due=0,switch_until=0;
    bool split_allowed=false,split_tap=false,reverse_gestures=false;
    uint64_t last_single=0;
    uint32_t staggered_taps=0;
    Point origin{},filtered{};
    Point pair_last[2]{},pair_anchor[2]{};
    float dx=0,dy=0,movement=0;
    float pair_dx=0,pair_smoothed_dy=0;
    float finger_travel=0;
    static constexpr uint64_t RELEASE_MS=60,SINGLE_WAIT_MS=220,DOUBLE_TAP_MS=300;
    static constexpr uint64_t TWO_CONTACT_TAP_MS=420,TWO_TAP_TOTAL_MS=800;
    static constexpr float TWO_TAP_MOVEMENT=30,TWO_TAP_FINGER_TRAVEL=56;
    static constexpr uint64_t SPLIT_GAP_MS=110,SPLIT_PRESS_MS=240;
    bool can_wait_for_partner()const{
        return split_allowed&&max==1&&!long_on&&movement<10&&released-start<=SPLIT_PRESS_MS;
    }
    uint64_t release_delay()const{return can_wait_for_partner()?SPLIT_GAP_MS:RELEASE_MS;}
    bool follows_first_tap(uint64_t now,Point next)const{
        if(!split_allowed||max!=1||long_on||movement>=10)return false;
        if(all_up){
            if(!can_wait_for_partner()||now-released>SPLIT_GAP_MS)return false;
        }else if(origin.id<0||origin.id>=15||next.id<0||next.id>=15||next.id==origin.id||
                 now-last_single>SPLIT_GAP_MS||now-start>SPLIT_PRESS_MS)return false;
        return std::hypot(next.x-origin.x,next.y-origin.y)>=24;
    }
    void begin_split_tap(uint64_t now,Point next){
        split_tap=true;all_up=false;
        max=2;two_start=last_two=now;origin=next;dx=dy=movement=0;
        emit(Action::CancelControl,0,0);
        emit(Action::TwoBegin,0,0);
    }
    bool two_tap_finished(uint64_t up)const{
        if(split_tap)return up-two_start<=SPLIT_PRESS_MS&&movement<14;
        return last_two-two_start<=TWO_CONTACT_TAP_MS&&up-start<=TWO_TAP_TOTAL_MS&&
               movement<TWO_TAP_MOVEMENT&&finger_travel<TWO_TAP_FINGER_TRAVEL;
    }
    static float distance_squared(Point a,Point b){
        const float x=a.x-b.x,y=a.y-b.y;
        return x*x+y*y;
    }
    bool pair_is_swapped(const Point*p)const{
        // IDs stay with the contacts even if the controller swaps record slots.
        if(p[0].id!=p[1].id){
            if(pair_last[0].id>=0&&pair_last[0].id<15){
                if(pair_last[0].id==p[0].id)return false;
                if(pair_last[0].id==p[1].id)return true;
            }
            if(pair_last[1].id>=0&&pair_last[1].id<15){
                if(pair_last[1].id==p[1].id)return false;
                if(pair_last[1].id==p[0].id)return true;
            }
        }
        return distance_squared(p[1],pair_last[0])+distance_squared(p[0],pair_last[1])<
               distance_squared(p[0],pair_last[0])+distance_squared(p[1],pair_last[1]);
    }
    static float shared_axis(float a,float b){
        return a*b>0?std::min(std::abs(a),std::abs(b)):0.f;
    }
    bool volume_swipe()const{return axis==(reverse_gestures?2:1);}
    bool track_swipe()const{return axis==(reverse_gestures?1:2);}
    float volume_travel()const{return reverse_gestures?-pair_smoothed_dy:dx;}
    float track_travel()const{return reverse_gestures?-pair_dx:dy;}
    void finish(uint64_t up){
        if(long_on)emit(Action::PageEnd,dx,0);
        else if(max==2){
            if(volume_swipe())emit(Action::VolumeEnd,volume_travel(),0);
            else if(track_swipe()){
                const float travel=track_travel();
                if(std::abs(travel)>=60&&up>=switch_until){emit(Action::Track,travel>0?1:-1,0);switch_until=up+300;}
                else emit(Action::Spring,0,0);
            }else if(two_tap_finished(up)){
                if(split_tap)++staggered_taps;
                if(double_due&&start<=double_due){
                    double_due=0;
                    emit(Action::Mute,0,0);
                }else double_due=up+DOUBLE_TAP_MS;
            }else double_due=0;
        }else{
            double_due=0;
            if(up-start<300&&movement<10){
                emit(Action::ControlEnd,origin.x,origin.y);
                single_due=up+SINGLE_WAIT_MS;
            }else emit(Action::ControlEnd,origin.x+dx,origin.y+dy);
        }
        active=false;all_up=false;max=previous=0;
    }
    void tick(uint64_t now) {
        if(active&&all_up&&elapsed(now,released,release_delay()))finish(released);
        if(single_due&&now>=single_due&&!active){single_due=0;emit(Action::Tap,0,0);}
        if(double_due&&now>=double_due&&!active)double_due=0;
        if(active&&!all_up&&max==1&&!long_on&&!cancel_long&&elapsed(now,start,500)){
            long_on=true;double_due=0;emit(Action::CancelControl,0,0);emit(Action::Long,0,0);
        }
    }
    void sample(uint64_t now,const Point*p,int n,bool allow_split=true,bool reverse=false) {
        if(active&&n==1&&follows_first_tap(now,p[0]))begin_split_tap(now,p[0]);
        // A brief all-up report is contact bounce, not a second press.
        if(active&&all_up&&elapsed(now,released,n==1?RELEASE_MS:release_delay()))finish(released);
        if(!active&&n) {
            active=true;start=now;origin=p[0];max=1;axis=0;dx=dy=movement=0;
            reverse_gestures=reverse;
            split_allowed=allow_split;split_tap=false;last_single=now;
            long_on=cancel_long=frozen=all_up=false;single_due=0;
            emit(Action::Begin,0,0);
            emit(Action::Control,p[0].x,p[0].y);
        }
        if(!active){tick(now);return;}
        if(!n){if(!all_up){all_up=true;released=now;}previous=0;tick(now);return;}
        bool bounced=all_up;all_up=false;
        if(n>=2) {
            Point pair[2]{p[0],p[1]};
            const bool initial_pair=max<2||split_tap;
            if(!initial_pair&&pair_is_swapped(p))std::swap(pair[0],pair[1]);
            const Point center{(pair[0].x+pair[1].x)/2,(pair[0].y+pair[1].y)/2};
            last_two=now;
            if(initial_pair){
                const bool first_dual=max<2;
                filtered=center;two_start=now;max=2;dx=dy=movement=finger_travel=0;
                for(int i=0;i<2;i++)pair_anchor[i]=pair[i];
                split_tap=false;
                if(first_dual){emit(Action::CancelControl,0,0);emit(Action::TwoBegin,0,0);}
            }else if(frozen||bounced||previous!=2){
                // Keep each finger's displacement across a missing report.
                for(int i=0;i<2;i++){
                    pair_anchor[i].x+=pair[i].x-pair_last[i].x;
                    pair_anchor[i].y+=pair[i].y-pair_last[i].y;
                }
                filtered=center;
            }else{
                filtered.x+=.6f*(center.x-filtered.x);filtered.y+=.6f*(center.y-filtered.y);
            }
            frozen=false;
            const Point a{pair[0].x-pair_anchor[0].x,pair[0].y-pair_anchor[0].y};
            const Point b{pair[1].x-pair_anchor[1].x,pair[1].y-pair_anchor[1].y};
            for(int i=0;i<2;i++)pair_last[i]=pair[i];
            const float raw_dx=(a.x+b.x)/2,raw_dy=(a.y+b.y)/2;
            dx=raw_dx+filtered.x-center.x;dy=raw_dy;
            pair_dx=raw_dx;pair_smoothed_dy=raw_dy+filtered.y-center.y;
            // Measure motion in the direction both contacts moved. The angle of
            // the line between the fingers is irrelevant to tap recognition.
            const float distance=std::hypot(raw_dx,raw_dy);
            if(distance>0){
                const float a_forward=(a.x*raw_dx+a.y*raw_dy)/distance;
                const float b_forward=(b.x*raw_dx+b.y*raw_dy)/distance;
                movement=std::max(movement,std::min(a_forward,b_forward));
            }
            finger_travel=std::max({finger_travel,std::hypot(a.x,a.y),std::hypot(b.x,b.y)});
            if(!axis&&elapsed(now,two_start,30)){
                const float x=std::abs(raw_dx),y=std::abs(raw_dy);
                if(x>=.75f*y&&x>=24&&shared_axis(a.x,b.x)>=16)axis=1;
                else if(y>1.35f*x&&y>=28&&shared_axis(a.y,b.y)>=18)axis=2;
                if(axis)double_due=0;
            }
            if(volume_swipe())emit(Action::Volume,volume_travel(),0);
            if(track_swipe()&&now>=switch_until)emit(Action::Drag,track_travel(),0);
            cancel_long=true;
            if(long_on){long_on=false;emit(Action::PageEnd,0,1);}
        }else {
            if(split_tap){
                movement=std::max(movement,std::hypot(p[0].x-origin.x,p[0].y-origin.y));
            }else if(max==2)frozen=true;
            else {
                last_single=now;
                dx=p[0].x-origin.x;dy=p[0].y-origin.y;
                movement=std::max(movement,std::hypot(dx,dy));
                if(!long_on&&movement>10)cancel_long=true;
                if(long_on)emit(Action::Pager,dx,0);
                else emit(Action::Control,p[0].x,p[0].y);
            }
        }
        previous=n;tick(now);
    }
    void reset(){active=all_up=long_on=frozen=split_tap=false;max=previous=0;single_due=double_due=0;}
};
}
