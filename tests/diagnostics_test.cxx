#include "scale_diagnostics.hxx"
#include <cassert>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
using namespace smart_scale;
int main() {
    char names[64]; failed_names(14, names); assert(std::string(names) == "noise,drift,residual");
    failed_names(0, names); assert(std::string(names) == "none");
    for (auto dt : {100U, 13U}) {
        auto trace = std::make_unique<AnomalyTrace>();
        DiagnosticSample s; s.ready = true; s.median_g = 239; s.failed = 0;
        std::uint64_t now = 0;
        auto feed = [&](double grams) {
            s.sample_ms = s.now_ms = now += dt; s.seq++; s.median_g = grams;
            return trace->push(s);
        };
        while (now < 7000) assert(!feed(239));
        assert(feed(237)); const auto trigger = now;
        while (!trace->frozen()) feed(239);
        assert(trace->trigger_ms() == trigger && trace->id() == 1 && !trace->truncated());
        assert(trace->front() && trace->front()->sample_ms >= trigger - 5000);
        auto previous = trace->front()->sample_ms;
        unsigned rows = 0; std::uint64_t last = 0;
        while (trace->front()) {
            const auto& row = *trace->front();
            assert(row.sample_ms >= previous && row.dt_ms == dt);
            last = previous = row.sample_ms; ++rows; trace->pop();
        }
        assert(last >= trigger + 5000 && rows > 9000 / dt);
        assert(trace->finish());
        assert(!feed(237)); assert(trace->id() == 1); // Cooldown.
        while (now < trigger + 31000) feed(239);
        assert(feed(237)); assert(trace->id() == 2);
        trace->poll(now + 5000); assert(trace->frozen()); // Disconnect: no new samples required.
        const auto first = trace->front()->seq;
        feed(239); assert(trace->missed_samples() == 1 && trace->front()->seq == first);
    }
    auto saturated = std::make_unique<AnomalyTrace>();
    DiagnosticSample row; row.ready = true; row.median_g = 0; row.failed = 0;
    for (unsigned ms = 1; ms <= 7000; ++ms) {
        row.sample_ms = row.now_ms = ms;
        if (ms == 1000) row.median_g = 2;
        saturated->push(row);
    }
    assert(saturated->frozen() && saturated->truncated() > 0);
    auto fault = std::make_unique<AnomalyTrace>();
    assert(fault->begin(100, "sensor_invalidated"));
    fault->poll(5100); assert(fault->frozen() && !fault->front() && fault->finish());
    // Observation must not change measurement behavior, including polarity/tare.
    for (float sign : {1.F, -1.F}) {
        Scale plain, observed;
        std::vector<std::string> events;
        observed.observe([](void* p, const Scale& scale, const char* event) {
            static_cast<std::vector<std::string>*>(p)->emplace_back(event);
            const auto snapshot = diagnostic_sample(scale, scale.sample_ms() + 50);
            assert(snapshot.now_ms - snapshot.sample_ms == 50);
            if (snapshot.ready) assert(std::isfinite(snapshot.raw_g));
        }, &events);
        assert(plain.restore({0, sign * 1000}) && observed.restore({0, sign * 1000}));
        assert(!observed.tare());
        for (unsigned i = 1; i <= 100; ++i) {
            const auto raw = static_cast<int>((i < 70 ? 239000 : 237000) * sign);
            plain.ingest(raw, i * 100); observed.ingest(raw, i * 100);
            assert(plain.stable() == observed.stable() && plain.gross() == observed.gross());
        }
        assert(observed.stable());
        assert(plain.tare() && observed.tare()); assert(plain.net() == observed.net());
        const auto snapshot = diagnostic_sample(observed, 10050);
        assert(snapshot.tare_g > 236 && snapshot.tare_g < 238 && snapshot.net_g == 0);
        auto has = [&](const char* event) { return std::find(events.begin(), events.end(), event) != events.end(); };
        assert(has("tare_rejected") && has("tare_applied") && has("stability_changed") && has("sample"));
        observed.check_timeout(12000); assert(has("sensor_invalidated"));
    }
    std::cout << "Diagnostic capture, cooldown, saturation, fault and observation checks passed\n";
}
