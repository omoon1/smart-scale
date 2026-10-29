#include "equipment_codec.hxx"
#include <cassert>
#include <algorithm>
#include <iostream>
#include <limits>

using namespace smart_scale;
int main() {
    Controller c(true);
    const Equipment other{4, Kind::other, 100.0F, std::nullopt};
    const Equipment tool{4, Kind::portafilter, 110.0F, 18.0F};
    assert(c.register_equipment(other, true));
    assert(!c.register_equipment(other, true));
    assert(c.register_equipment(tool, true)); // Same icon, different kind is valid.
    assert(c.equipment_map().size() == 2);
    const auto items = c.equipment();
    assert(recognize(105, items.data(), items.size()).ambiguous);
    const auto bytes = equipment_storage::encode(items);
    assert(bytes && bytes->size() == 2 * equipment_storage::record_bytes);
    const auto loaded = equipment_storage::decode(*bytes);
    assert(loaded && loaded->size() == 2);
    Controller rebooted(true);
    assert(rebooted.restore_equipment(*loaded));
    assert(!rebooted.equipment_dirty());
    assert(rebooted.has_equipment(Kind::other, 4) && rebooted.has_equipment(Kind::portafilter, 4));
    assert(rebooted.equipment_map().at(equipment_key(tool)).usual_dose_g == 18.0F);
    assert(!rebooted.register_equipment(other, true));
    assert(rebooted.remove_equipment(equipment_key(other)));
    assert(rebooted.has_equipment(Kind::portafilter, 4));
    assert(rebooted.register_equipment(other, true)); // Deleted combination becomes available.
    const auto revision = rebooted.equipment_revision();
    assert(rebooted.remove_equipment(equipment_key(tool)));
    rebooted.equipment_saved(revision); // An older save must not clear a newer mutation.
    assert(rebooted.equipment_dirty());
    rebooted.equipment_saved(rebooted.equipment_revision());
    assert(!rebooted.equipment_dirty());
    assert(rebooted.remove_equipment(equipment_key(other)));
    const auto empty_bytes = equipment_storage::encode(rebooted.equipment());
    assert(empty_bytes && empty_bytes->empty());
    assert(equipment_storage::decode(*empty_bytes)->empty());
    assert(rebooted.restore_equipment({other, tool}));
    assert(!rebooted.restore_equipment({other, other}));
    assert(rebooted.equipment_count() == 2); // Invalid restore does not partially replace the map.
    auto invalid = other;
    invalid.id = equipment_icon_count;
    assert(!equipment_storage::encode({invalid}));
    invalid = other; invalid.kind = static_cast<Kind>(3);
    assert(!equipment_storage::encode({invalid}));
    invalid = other; invalid.empty_g = std::numeric_limits<float>::quiet_NaN();
    assert(!equipment_storage::encode({invalid}));
    invalid = other; invalid.usual_dose_g = -1;
    assert(!equipment_storage::encode({invalid}));
    auto broken = *bytes;
    broken.pop_back();
    assert(!equipment_storage::decode(broken));
    broken = *bytes; broken[2] = 2;
    assert(!equipment_storage::decode(broken));
    broken = *bytes; broken[3] = 0; broken[4] = 0; broken[5] = 0xc0; broken[6] = 0x7f;
    assert(!equipment_storage::decode(broken)); // NaN reference weight.
    broken = *bytes; broken[0] = 3;
    assert(!equipment_storage::decode(broken));
    broken = *bytes; broken[1] = equipment_icon_count;
    assert(!equipment_storage::decode(broken));
    broken = *bytes;
    std::copy_n(broken.begin(), equipment_storage::record_bytes, broken.begin() + equipment_storage::record_bytes);
    assert(!equipment_storage::decode(broken));
    Controller ordered(true);
    assert(ordered.restore_equipment({tool, other}));
    assert(equipment_key(ordered.equipment()[0]) == equipment_key(other));
    std::cout << "Equipment identity, persistence codec, restore and save-generation checks passed\n";
}
