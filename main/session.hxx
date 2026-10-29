#pragma once
#include "config.hxx"
#include "scale.hxx"
#include "flow_start.hxx"
#include <array>
#include <cstdint>
#include <optional>
#include <vector>
#include <unordered_map>

namespace smart_scale {
enum class Mode { espresso, pour_over };
enum class Phase { ready, brewing, result, remove_load };
enum class Kind { other, portafilter, pour_over_set };
enum class DoseSource { unknown, measured, manual };
enum class Scenario { known, unknown, sequential, pour_over };
enum class BrewError { none, object_removed, sensor_fault };
struct Equipment {
    std::uint32_t id{};
    Kind kind{Kind::other};
    std::optional<float> empty_g;
    std::optional<float> usual_dose_g;
};
// id is the stable zero-based index into the bundled icon list.
inline constexpr std::uint32_t equipment_icon_count = 20;
using EquipmentKey = std::uint32_t;
constexpr EquipmentKey equipment_key(Kind kind, std::uint32_t id) {
    return static_cast<EquipmentKey>(kind) * equipment_icon_count + id;
}
constexpr EquipmentKey equipment_key(const Equipment& item) { return equipment_key(item.kind, item.id); }
using EquipmentMap = std::unordered_map<EquipmentKey, Equipment>;
bool valid_equipment(const Equipment& item);
struct Point { float seconds{}, grams{}; };
struct Session {
    Phase phase{Phase::ready};
    Mode mode{Mode::espresso};
    std::optional<Equipment> weighing_tool, vessel, active_object;
    std::optional<float> dose_g, target_g;
    DoseSource dose_source{DoseSource::unknown};
    float net_g{}, seconds{}, flow_g_s{};
    bool loaded{}, baseline_known{}, uncertain{}, paused{};
    bool brew_prepared{}; // Registered vessel, one-time tare and a valid coffee dose.
    std::optional<float> empty_reference_g;
    std::optional<float> removal_threshold_g;
    BrewError brew_error{BrewError::none};
    std::array<Point, config::max_samples> samples{};
    std::size_t sample_count{};
    [[nodiscard]] std::optional<float> ratio() const;
};
struct Record { Session session; bool aborted{}; };
struct Recognition { std::optional<Equipment> equipment; bool ambiguous{}; };
Recognition recognize(float total_g, const Equipment* equipment, std::size_t count);

// Pure C++ state owner. All calls are serialized by the UI task.
class Controller {
public:
    explicit Controller(bool live = false);
    bool live() const { return live_; }
    const char* recognition_status() const;
    bool object_ambiguous() const { return object_ambiguous_; }
    Scale& scale() { return scale_; }
    const Scale& scale() const { return scale_; }
    void sensor_fault();
    void sensor_sample(std::optional<std::int32_t> raw, std::uint64_t now_ms);
    bool tare();
    bool start_brew();
    void set_auto_brew_enabled(bool enabled) {
        if (auto_brew_enabled_ != enabled) flow_start_.reset();
        auto_brew_enabled_ = enabled;
    }
    double error_remaining_s() const {
        const double remaining = config::brew_error_return_s - brew_error_elapsed_s_;
        return remaining > 0 ? remaining : 0;
    }
    [[nodiscard]] const Session& session() const { return session_; }
    // Stable display order; never expose unordered_map iteration order as a row ID.
    [[nodiscard]] std::vector<Equipment> equipment() const;
    [[nodiscard]] const EquipmentMap& equipment_map() const { return equipment_; }
    bool has_equipment(Kind kind, std::uint32_t id) const {
        return id < equipment_icon_count && static_cast<unsigned>(kind) <= static_cast<unsigned>(Kind::pour_over_set) &&
            equipment_.contains(equipment_key(kind, id));
    }
    bool restore_equipment(const std::vector<Equipment>& items);
    bool equipment_storage_error() const { return equipment_storage_error_; }
    void equipment_storage_error(bool error) { equipment_storage_error_ = error; }
    bool equipment_dirty() const { return equipment_revision_ != saved_equipment_revision_; }
    std::uint64_t equipment_revision() const { return equipment_revision_; }
    void equipment_saved(std::uint64_t revision) { if (revision == equipment_revision_) saved_equipment_revision_ = revision; }
    [[nodiscard]] std::size_t equipment_count() const { return equipment_.size(); }
    [[nodiscard]] std::size_t record_count() const { return record_count_; }
    [[nodiscard]] const Record& latest_record() const;
    bool select_equipment(std::size_t index);
    bool set_dose(float grams, DoseSource source = DoseSource::manual);
    bool set_mode(Mode mode);
    bool register_equipment(Equipment equipment, bool stable);
    bool remove_equipment(EquipmentKey key);
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
    void mark_brew_error(BrewError error);
    void begin_brew(float elapsed_s = 0);
    void update_brew_preparation();
    std::optional<EquipmentKey> tared_vessel_id_;
    std::optional<float> vessel_baseline_g_;
    std::optional<Calibration> vessel_calibration_;
    FlowStartDetector flow_start_;
    bool auto_brew_enabled_{true};
    std::optional<std::uint64_t> auto_brew_origin_ms_;
    std::optional<std::uint32_t> auto_brew_epoch_;
    bool live_{};
    std::optional<std::uint64_t> empty_since_;
    std::optional<std::uint64_t> below_object_since_;
    double brew_error_elapsed_s_{};
    bool recognition_checked_{}, object_ambiguous_{}, dose_tared_{};
    Scale scale_;
    Session session_{};
    EquipmentMap equipment_;
    bool equipment_storage_error_{};
    std::uint64_t equipment_revision_{}, saved_equipment_revision_{};
    std::array<Record, config::max_records> records_{};
    std::size_t record_count_{}, next_record_{};
    bool demo_running_{};
    Scenario scenario_{Scenario::known};
    float demo_time_{};
    int demo_stage_{};
};
}
