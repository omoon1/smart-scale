#pragma once
#include "config.hxx"
#include "scale.hxx"
#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace smart_scale {
enum class Mode { espresso, pour_over };
enum class Phase { ready, brewing, result, remove_load };
enum class Kind { cup, portafilter, pour_over_set };
enum class DoseSource { unknown, measured, manual };
enum class Scenario { known, unknown, sequential, pour_over };
struct Equipment {
    std::uint32_t id{};
    Kind kind{Kind::cup};
    std::string name;
    std::string configuration;
    std::optional<float> empty_g;
    std::optional<float> usual_dose_g;
};
struct Point { float seconds{}, grams{}; };
struct Session {
    Phase phase{Phase::ready};
    Mode mode{Mode::espresso};
    std::optional<Equipment> weighing_tool, vessel, active_object;
    std::optional<float> dose_g, target_g;
    DoseSource dose_source{DoseSource::unknown};
    float net_g{}, seconds{}, flow_g_s{};
    bool loaded{}, baseline_known{}, uncertain{}, paused{};
    std::array<Point, config::max_samples> samples{};
    std::size_t sample_count{};
    [[nodiscard]] std::optional<float> ratio() const;
};
struct Record { Session session; bool aborted{}; };
struct Recognition { std::optional<Equipment> equipment; std::optional<float> dose_g; };
Recognition recognize(float total_g, const Equipment* equipment, std::size_t count);

// Pure C++ state owner. All calls are serialized by the UI task.
class Controller {
public:
    explicit Controller(bool live = false);
    bool live() const { return live_; }
    Scale& scale() { return scale_; }
    const Scale& scale() const { return scale_; }
    void sensor_sample(std::optional<std::int32_t> raw, std::uint64_t now_ms);
    bool tare();
    bool start_brew();
    [[nodiscard]] const Session& session() const { return session_; }
    [[nodiscard]] const auto& equipment() const { return equipment_; }
    [[nodiscard]] std::size_t equipment_count() const { return equipment_count_; }
    [[nodiscard]] std::size_t record_count() const { return record_count_; }
    [[nodiscard]] const Record& latest_record() const;
    bool select_equipment(std::size_t index);
    bool set_dose(float grams, DoseSource source = DoseSource::manual);
    bool set_mode(Mode mode);
    bool register_equipment(Equipment equipment, bool stable);
    bool remove_equipment(std::uint32_t id);
    void remove_object();
    void place(std::size_t index);
    void weigh_beans(float grams);
    void liquid(float delta_g, float elapsed_s);
    void finish();
    void reset();
    void next();
    void load_scenario(Scenario scenario);
    void run_demo();
    void pause_demo();
    void tick(float elapsed_s);
    [[nodiscard]] bool demo_running() const { return demo_running_; }
private:
    void target();
    void place_demo_kind(Kind kind);
    void archive(bool aborted);
    bool live_{};
    Scale scale_;
    Session session_{};
    std::array<Equipment, config::max_equipment> equipment_{};
    std::size_t equipment_count_{3};
    std::uint32_t next_equipment_id_{4};
    std::array<Record, config::max_records> records_{};
    std::size_t record_count_{}, next_record_{};
    bool demo_running_{};
    Scenario scenario_{Scenario::known};
    float demo_time_{};
    int demo_stage_{};
};
}
