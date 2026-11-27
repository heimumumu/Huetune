#pragma once
#include <algorithm>
#include <cstdint>

namespace mc {
// Wait for new text, then slide the heading independently of artwork.
struct TrackTransition {
    enum Phase {Idle, Committing, Entering=3, Holding};
    static constexpr uint64_t duration=160;
    Phase phase=Idle;
    uint64_t started=0,deadline=0;
    uint32_t reply_ms=0, heading_ms=0;
    uint32_t target=0,starts=0;
    bool awaiting=false,has_target=false;
    int direction=1;
    static float ease(float t){t=std::clamp(t,0.f,1.f);return 1-(1-t)*(1-t)*(1-t);}
    void hold(uint64_t now){phase=Holding;started=now;reply_ms=heading_ms=0;++starts;}
    void request(uint64_t now,int dir){
        direction=dir>0?1:-1;awaiting=true;has_target=false;deadline=now+3000;hold(now);
    }
    void received(uint64_t now,uint32_t tag){
        if(phase!=Holding){hold(now);direction=1;}
        reply_ms=uint32_t(now-started);awaiting=false;has_target=true;target=tag;
    }
    void tick(uint64_t now,bool ready){
        if(phase==Holding){
            if(has_target&&ready){heading_ms=uint32_t(now-started);phase=Committing;has_target=false;}
            else if(awaiting&&now>=deadline){phase=Idle;awaiting=false;}
        }
        if(phase==Entering&&now-started>=duration)phase=Idle;
    }
    void presented(uint64_t now){if(phase==Committing){phase=Entering;started=now;}}
    int offset(uint64_t now)const{
        if(phase==Committing)return -direction*20;
        if(phase!=Entering)return 0;
        return -direction*int(20*(1-ease(float(now-started)/duration))+.5f);
    }
    void cancel(){phase=Idle;awaiting=has_target=false;}
};
}
