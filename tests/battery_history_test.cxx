#include "battery_history.hxx"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace smart_scale;
BatterySnapshot battery(unsigned p=72, bool usb=false) { return {true,true,usb,usb,false,p}; }
int main() {
    const auto estimated = decode_voltage_battery(3800);
    assert(BatteryHistory::state(estimated) == UsageState::estimated);
    assert(BatteryHistory::state(estimated, true) == UsageState::sleep);

    BatteryHistory h;
    assert(h.size()==0);
    h.update(0,UsageState::charging,battery(20,true));
    h.update(3600,UsageState::active,battery(90));
    h.update(7200,UsageState::sleep,battery(80));
    h.update(14400,UsageState::active,battery(70));
    unsigned n=0;
    h.segments(21600,[&](double x1,double y1,double x2,double y2,UsageState s){
        assert(x1>=0 && x1<x2 && x2<=1 && y1>=0 && y2<=100);
        assert(s==static_cast<UsageState>(n)); ++n;
    });
    assert(n==3); // Charging, active, sleep intervals retain their distinct colors.
    n=0;
    h.segments(3600,[&](double x1,double y1,double x2,double y2,UsageState s){
        assert(x1==0 && x2==1 && s==UsageState::sleep);
        assert(std::abs(y1-75)<0.001 && y2==70); ++n;
    });
    assert(n==1); // Clip the sleep join to the requested trailing window.
    h.update(14405,UsageState::unknown,{});
    h.update(14500,UsageState::active,battery(69));
    n=0;
    h.segments(60,[&](double,double,double,double,UsageState){++n;});
    assert(n==0); // Never draw a trace across unavailable telemetry.
    BatteryHistory sampled;
    for(uint64_t t=0;t<=4*86400;t+=5) sampled.update(t,UsageState::active,battery());
    assert(sampled.size()<=866);
    assert(sampled.at(1).seconds>sampled.now()-BatteryHistory::retention_seconds);
    sampled.segments(3*86400,[&](double x1,double,double x2,double,UsageState){assert(x1>=0 && x2<=1);});
    BatteryHistory chatter;
    for(uint64_t t=0;t<10000;++t) chatter.update(t,t%2?UsageState::active:UsageState::charging,battery());
    assert(chatter.size()==BatteryHistory::capacity);
    assert(chatter.at(0).seconds==10000-BatteryHistory::capacity);
    chatter.update(0,UsageState::active,battery()); // Reboot / reset monotonic clock.
    assert(chatter.size()==1 && chatter.now()==0);
    chatter.clear(); assert(chatter.size()==0);
    assert(BatteryHistory{}.size()==0); // No NVS restoration.
    BatteryHistory live;
    live.update(100,UsageState::active,battery(80));
    live.update(105,UsageState::active,battery(79));
    assert(live.size()==1 && live.latest().percent==79);
    n=0; live.segments(21600,[&](double x1,double,double x2,double,UsageState){assert(x1>0.99 && x2==1);++n;});
    assert(n==1); // Empty pre-boot time is not fabricated.
    live.update(105,UsageState::sleep,battery(79));
    live.update(205,UsageState::active,battery(78));
    n=0; live.segments(100,[&](double,double,double,double,UsageState s){assert(s==UsageState::sleep);++n;});
    assert(n==1);
    assert(BatteryHistory::state(battery(100,true),true)==UsageState::charging);
    std::cout << "Volatile battery graph history tests passed\n";
}
