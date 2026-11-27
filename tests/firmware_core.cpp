#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../firmware/main/core.hpp"
#include "../firmware/main/transition.hpp"
#include <cassert>
#include <iostream>
#include <tuple>
using mc::Action;
using Event=std::tuple<Action,float,float>;
struct Harness {
    mc::Gesture g;
    std::vector<Event> events;
    bool reverse=false;
    Harness(){g.emit=[this](Action a,float x,float y){events.emplace_back(a,x,y);};}
    void sample(uint64_t t,std::initializer_list<mc::Point> p){std::vector<mc::Point> v(p);g.sample(t,v.data(),v.size(),true,reverse);}
    int count(Action a){int n=0;for(auto&e:events)if(std::get<0>(e)==a)n++;return n;}
    float last(Action a){for(auto i=events.rbegin();i!=events.rend();++i)if(std::get<0>(*i)==a)return std::get<1>(*i);return 0;}
};
int main(){
    for(bool reverse:{false,true}){
        for(auto [x,y]:{std::pair{60,0},std::pair{-60,0},std::pair{0,60},std::pair{0,-60}}){
            Harness h;
            h.reverse=reverse;
            h.sample(0,{{140,95},{275,210}});
            h.sample(50,{{140+x/2.f,95+y/2.f},{275+x/2.f,210+y/2.f}});
            h.sample(100,{{140.f+x,95.f+y},{275.f+x,210.f+y}});
            h.sample(140,{});
            h.g.tick(210);
            const bool volume=reverse?y!=0:x!=0;
            if(volume){
                assert(h.count(Action::Volume)==2&&h.count(Action::VolumeEnd)==1);
                assert(h.last(Action::VolumeEnd)*(reverse?-y:x)>0);
                assert(h.count(Action::Track)==0&&h.count(Action::Drag)==0);
            }else{
                assert(h.count(Action::Track)==1&&h.count(Action::Drag)==2);
                assert(h.last(Action::Track)*(reverse?-x:y)>0);
                assert(h.count(Action::Volume)==0&&h.count(Action::VolumeEnd)==0);
            }
            assert(h.count(Action::Tap)==0&&h.count(Action::Mute)==0);
        }
    }
    {
        Harness h;
        h.reverse=true;
        h.sample(0,{{90,40},{260,210}});h.sample(80,{});h.g.tick(145);
        assert(h.count(Action::Mute)==0&&h.count(Action::Tap)==0);
        h.sample(170,{{90,40},{260,210}});h.sample(230,{});h.g.tick(300);
        assert(h.count(Action::Mute)==1&&h.count(Action::Tap)==0);
        h.sample(400,{{90,40}});h.sample(480,{});h.g.tick(710);
        assert(h.count(Action::Mute)==1&&h.count(Action::Tap)==1);
    }
    // A delayed host reply must not bring the old song back or start a second animation.
    {mc::TrackTransition t;t.request(1000,1);t.tick(1100,false);assert(t.phase==mc::TrackTransition::Holding&&t.offset(1100)==0);
     t.received(1800,42);t.tick(1800,false);assert(t.starts==1&&t.phase==mc::TrackTransition::Holding);
     t.tick(1850,true);assert(t.phase==mc::TrackTransition::Committing&&t.offset(1850)==-20&&t.reply_ms==800&&t.heading_ms==850);
     t.presented(1900);assert(t.offset(1900)==-20&&t.offset(1980)>-20);t.tick(2060,true);assert(t.phase==mc::TrackTransition::Idle&&t.offset(2060)==0&&t.starts==1);}
    {mc::TrackTransition t;t.request(0,-1);t.received(30,42);t.tick(100,true);t.presented(120);assert(t.offset(120)==20);t.tick(300,true);assert(t.phase==mc::TrackTransition::Idle&&t.direction==-1&&t.starts==1);}
    // Several external changes while assets are pending keep only the newest target.
    {mc::TrackTransition t;t.received(0,1);t.tick(100,false);t.received(200,2);assert(t.target==2&&t.starts==1);t.tick(250,false);assert(t.offset(250)==0);
     t.tick(300,true);t.presented(310);t.received(450,3);assert(t.starts==2&&t.target==3&&t.offset(450)==0);}
    // If the player does not change song, restore once after the explicit reply timeout.
    {mc::TrackTransition t;t.request(0,1);t.tick(100,false);t.tick(2999,false);assert(t.phase==mc::TrackTransition::Holding);t.tick(3000,false);assert(t.phase==mc::TrackTransition::Idle);}
    assert(mc::crc16((const uint8_t*)"123456789",9)==0x29b1);
    assert(mc::crc32((const uint8_t*)"123456789",9)==0xcbf43926);
    auto wire=mc::frame(0x10,255,(const uint8_t*)"123456789",9);
    mc::Parser parser;int frames=0;
    parser.receive=[&](uint8_t type,const uint8_t*p,size_t n){assert(type==0x10&&n==9&&!memcmp(p,"123456789",9));frames++;};
    auto broken=wire;broken.back()^=1;parser.feed(broken.data(),broken.size());
    for(auto byte:wire)parser.feed(&byte,1);
    assert(frames==1);
    assert(!mc::elapsed(1000,1001,3000));assert(!mc::elapsed(1000,1001,2000));
    assert(!mc::elapsed(3999,1000,3000));assert(mc::elapsed(4000,1000,3000));
    {Harness h;h.sample(0,{{10,10}});h.sample(100,{});h.g.tick(319);assert(h.count(Action::Tap)==0);h.g.tick(320);assert(h.count(Action::Tap)==1);}
    {Harness h;h.sample(0,{{10,10},{20,10}});h.sample(80,{});h.g.tick(140);assert(h.g.double_due==380);h.g.tick(381);assert(!h.g.double_due&&h.count(Action::Mute)==0&&h.count(Action::Tap)==0);}
    {Harness h;h.sample(0,{{10,10},{20,10}});h.sample(80,{});h.sample(170,{{10,10},{20,10}});h.sample(230,{});h.g.tick(600);assert(h.count(Action::Mute)==1&&h.count(Action::Tap)==0);}
    // The second tap may finish after the deadline if it started in time.
    {Harness h;h.sample(0,{{10,10},{20,10}});h.sample(80,{});h.sample(360,{{10,10},{20,10}});
     h.sample(460,{});h.g.tick(520);assert(h.count(Action::Mute)==1&&h.count(Action::Tap)==0);}
    {Harness h;h.sample(0,{{10,10},{20,10}});h.sample(80,{});h.sample(390,{{10,10},{20,10}});
     h.sample(460,{});h.g.tick(900);assert(h.count(Action::Mute)==0&&h.count(Action::Tap)==0);}
    {Harness h;h.sample(0,{{10,10}});h.sample(500,{{10,10}});assert(h.count(Action::Long)==1);h.sample(550,{{80,10}});h.sample(600,{});h.g.tick(660);assert(h.count(Action::PageEnd)==1&&h.count(Action::Tap)==0);}
    // Staggered landing and lifting, first finger held longer than 300 ms.
    {Harness h;h.sample(0,{{90,40}});h.sample(180,{{90,40},{280,210}});h.sample(270,{{280,210}});h.sample(320,{});h.g.tick(380);assert(h.g.double_due==620&&h.count(Action::Mute)==0&&h.count(Action::Tap)==0);}
    // A finger rolls sideways while the other stays put; it is still a tap.
    {Harness h;h.sample(0,{{90,40}});h.sample(95,{{90,40},{260,210}});h.sample(140,{{90,40},{300,212}});
     h.sample(190,{{300,212}});h.sample(230,{});h.g.tick(290);
     assert(h.g.double_due==530&&h.count(Action::Mute)==0&&h.count(Action::Volume)==0&&h.count(Action::Tap)==0);}
    // One finger lingers after the two-finger part of the tap has ended.
    {Harness h;h.sample(0,{{90,40}});h.sample(130,{{90,40},{260,210}});h.sample(420,{{90,40},{260,210}});
     h.sample(440,{{260,210}});h.sample(660,{});h.g.tick(720);
     assert(h.g.double_due==960&&h.count(Action::Mute)==0);}
    {Harness h;h.sample(0,{{90,40},{260,210}});h.sample(460,{{90,40},{260,210}});h.sample(490,{});h.g.tick(550);
     assert(!h.g.double_due&&h.count(Action::Mute)==0);}
    // No frame contains two points. A short gap and distinct positions recover the tap.
    {Harness h;h.sample(0,{{90,40,0}});h.sample(50,{});h.sample(130,{{130,60,1}});h.sample(185,{});h.g.tick(500);
     assert(h.g.staggered_taps==1&&h.count(Action::Mute)==0&&h.count(Action::Tap)==0);}
    // A contact ID changes between two frames with no all-up report.
    {Harness h;h.sample(0,{{90,40,0}});h.sample(45,{{91,40,0}});h.sample(55,{{130,60,1}});h.sample(105,{});h.g.tick(430);
     assert(h.g.staggered_taps==1&&h.count(Action::Mute)==0&&h.count(Action::Tap)==0);}
    // Repeated one-finger taps in the same place must never mute.
    {Harness h;h.sample(0,{{90,40,0}});h.sample(50,{});h.sample(130,{{92,41,1}});h.sample(185,{});h.g.tick(500);
     assert(h.g.staggered_taps==0&&h.count(Action::Mute)==0&&h.count(Action::Tap)==1);}
    // Two staggered pairs produce one mute action.
    {Harness h;h.sample(0,{{90,40,0}});h.sample(40,{});h.sample(90,{{130,60,1}});h.sample(125,{});
     h.sample(200,{{90,40,0}});h.sample(230,{});h.sample(270,{{130,60,1}});h.sample(310,{});h.g.tick(500);
     assert(h.g.staggered_taps==2&&h.count(Action::Mute)==1&&h.count(Action::Tap)==0);}
    // Recover normal swiping if the second pair settles into two tracked fingers.
    {Harness h;h.sample(0,{{90,40,0}});h.sample(40,{});h.sample(100,{{130,60,1}});
     h.sample(130,{{90,40,0},{130,60,1}});h.sample(180,{{150,40,0},{190,60,1}});h.sample(210,{});h.g.tick(600);
     assert(h.count(Action::TwoBegin)==1&&h.count(Action::VolumeEnd)==1&&h.count(Action::Mute)==0);}
    for(auto other:{mc::Point{175,105,1},mc::Point{105,175,1},mc::Point{155,155,1},mc::Point{55,155,1}}){
        const mc::Point first{105,105,0};
        Harness split;
        split.sample(0,{first});split.sample(50,{});split.sample(130,{other});split.sample(185,{});split.g.tick(500);
        assert(split.g.staggered_taps==1&&split.count(Action::Mute)==0&&split.count(Action::Tap)==0);
        for(int taps:{1,2}){
            Harness h;
            for(int tap=0;tap<taps;tap++){
                uint64_t t=tap*235;
                h.sample(t,{first});
                h.sample(t+45,{first,other});
                // One fingertip rolls while the hardware reverses the slot order.
                h.sample(t+83,{{other.x+3,other.y+50,1},first});
                h.sample(t+110,{other});
                h.sample(t+145,{});
            }
            h.g.tick(750);
            assert(h.count(Action::Mute)==(taps==2));
            assert(!h.count(Action::Volume)&&!h.count(Action::Drag)&&!h.count(Action::Tap));
        }
        Harness swipe;
        swipe.sample(0,{first,other});
        swipe.sample(90,{{first.x,first.y+65,0},{other.x,other.y+65,1}});
        swipe.sample(110,{});swipe.g.tick(500);
        assert(swipe.count(Action::Track)==1&&swipe.count(Action::Mute)==0);
    }
    // One false all-up frame must not turn one two-finger tap into a double tap.
    {Harness h;h.sample(0,{{10,10},{130,180}});h.sample(80,{});h.sample(100,{{10,10},{130,180}});h.sample(130,{});h.g.tick(500);assert(h.count(Action::Mute)==0&&h.count(Action::Tap)==0&&h.count(Action::Begin)==1);}
    // Recover after losing one point for much longer than the old 40 ms window.
    {Harness h;h.sample(0,{{60,30},{150,210}});h.sample(50,{{90,30},{180,210}});h.sample(70,{{90,30}});h.sample(240,{{90,30}});h.sample(250,{{110,30},{200,210}});h.sample(270,{{130,30},{220,210}});h.sample(290,{{150,30},{240,210}});h.sample(310,{});h.g.tick(370);assert(h.count(Action::Volume)>=3&&h.count(Action::VolumeEnd)==1&&h.count(Action::Tap)==0&&h.count(Action::Mute)==0);}
    // Same row, different rows, same column or diagonals: placement is irrelevant.
    for(auto second:{mc::Point{200,70},mc::Point{200,220},mc::Point{80,220},mc::Point{20,220}}){
        for(int slope:{-1,0,1}){
            Harness h;mc::Point first{80,70};h.sample(0,{first});h.sample(90,{first,second});
            for(int i=1;i<=12;i++){float dx=i*6,dy=slope*i*2;h.sample(90+i*15,{{first.x+dx,first.y+dy},{second.x+dx,second.y+dy}});}
            h.sample(280,{{first.x+72,first.y}});h.sample(320,{});h.g.tick(700);
            assert(h.count(Action::Volume)>0&&h.count(Action::VolumeEnd)==1&&h.last(Action::VolumeEnd)>60);
            assert(h.count(Action::Track)==0&&h.count(Action::Tap)==0&&h.count(Action::Mute)==0);
        }
    }
    {Harness h;h.sample(0,{{20,30},{100,230}});h.sample(50,{{47,60},{127,260}});assert(h.g.axis==1);h.sample(90,{{80,70},{160,270}});h.sample(120,{});h.g.tick(180);assert(h.count(Action::VolumeEnd)==1&&h.count(Action::Track)==0);}
    {Harness h;h.sample(0,{{10,10},{30,10}});h.sample(80,{{10,80},{30,80}});h.sample(100,{});h.g.tick(160);assert(h.count(Action::Track)==1);h.sample(200,{{10,10},{30,10}});h.sample(250,{{10,80},{30,80}});h.sample(290,{});h.g.tick(350);assert(h.count(Action::Track)==1);}
    {Harness h;h.sample(0,{{10,10}});h.sample(100,{{130,10}});h.sample(400,{});h.g.tick(460);assert(h.count(Action::ControlEnd)==1&&h.count(Action::Tap)==0);}
    // Lifted record in slot zero, remaining active finger in slot one.
    {uint8_t data[13]={1,0x40,20,0,30,0,0,0x80,90,0x20,200,0,0};mc::Point p[2];int n=mc::decode_touch(data,13,p);assert(n==1&&p[0].x==200&&p[0].y==229&&p[0].id==2);data[0]=0;assert(mc::decode_touch(data,13,p)==0);data[0]=3;assert(mc::decode_touch(data,13,p)==-1);}
    std::cout<<"firmware core checks passed (timers, touch records, staggered taps, bounce, 12 geometries, dropout recovery)\n";
}
