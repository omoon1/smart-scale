#include "touch_sleep_check.hxx"
#include "expander_wake.hxx"
#include <array>
#include <cassert>
#include <iostream>
using namespace smart_scale;
using namespace smart_scale::board349;
int main() {
    std::array<uint8_t, touch_packet_bytes> packet{};
    unsigned reads{};
    bool io_ok=true, irq_high=true;
    auto check = [&] {
        reads=0;
        return qualify_sleep_touch([&](TouchReport& report) {
            ++reads;
            report=decode_touch_report(packet.data(),packet.size());
            return io_ok;
        }, [&]{return irq_high;});
    };
    // All uniform values follow the explicit board policy, including 00/FF.
    for (unsigned byte=0;byte<256;++byte) {
        packet.fill(byte);
        const auto report=decode_touch_report(packet.data(),packet.size());
        assert(report.state==TouchState::released && report.contacts==0);
        assert(check()==TouchSleepCheck::released);
        assert(reads==1); // no debounce delay or polling loop
        irq_high=false;
        assert(check()==TouchSleepCheck::released); // final IRQ guard still applies
        irq_high=true;
        io_ok=false;
        assert(check()==TouchSleepCheck::io_error);
        io_ok=true;
    }
    for (const auto byte : {0xa5,0xd5,0x84}) {
        packet.fill(byte); packet[13]^=1;
        assert(check()==TouchSleepCheck::invalid);
    }
    packet.fill(0); packet[0]=1; // explicit zero-contact report
    assert(check()==TouchSleepCheck::released);
    irq_high=false;
    assert(check()==TouchSleepCheck::released); // later final IRQ guard still applies
    irq_high=true;
    packet[1]=1; packet[3]=10; packet[5]=10;
    assert(check()==TouchSleepCheck::pressed);
    packet[1]=2; packet[9]=20; packet[11]=20;
    assert(check()==TouchSleepCheck::pressed);
    packet[10]=1;
    assert(check()==TouchSleepCheck::invalid); // invalid second-contact coordinates

    // Contact arriving after port acknowledgement and final IRQ remain guarded.
    uint32_t baseline{};
    auto prepare = [&](bool late_contact, bool final_irq) {
        unsigned checks=0;
        return prepare_expander_wake([](uint32_t& v){v=0xff;return true;},
            [&](bool& pressed) {
                packet.fill(0x84);
                if (++checks==2 && late_contact) {
                    packet.fill(0); packet[1]=1; packet[3]=10; packet[5]=10;
                }
                const auto result=check();
                pressed=result==TouchSleepCheck::pressed;
                return pressed || result==TouchSleepCheck::released;
            }, [&]{return final_irq;},baseline,1U,0U);
    };
    assert(prepare(false,true)==WakePreparation::ready);
    assert(prepare(true,true)==WakePreparation::touch);
    assert(prepare(false,false)==WakePreparation::pending_irq);
    std::cout << "immediate repeated-packet sleep tests passed\n";
}
