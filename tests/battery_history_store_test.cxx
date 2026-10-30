#include "battery_history_store.hxx"
#include <cassert>
#include <cstring>
#include <vector>
#include <iostream>
namespace { std::vector<unsigned char> blob, staged; unsigned commits{}, attempts{}; bool fail{}; }
esp_err_t nvs_open(const char*,int,nvs_handle_t* h) { *h=1; return ESP_OK; }
void nvs_close(nvs_handle_t) {}
esp_err_t nvs_get_blob(nvs_handle_t,const char*,void* out,size_t* n) {
    if(blob.empty()) return ESP_ERR_NVS_NOT_FOUND;
    if(*n < blob.size()) return 2;
    *n=blob.size(); std::memcpy(out,blob.data(),*n); return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t,const char*,const void* in,size_t n) {
    ++attempts; auto* p=static_cast<const unsigned char*>(in); staged.assign(p,p+n); return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t) { if(fail) return 2; blob=staged; ++commits; return ESP_OK; }
int main() {
    using namespace smart_scale;
    BatteryHistory h;
    device::BatteryHistoryStore store;
    store.init(h);
    BatterySnapshot b{true,true,false,false,false,80};
    h.update(0,1793318400,UsageState::active,b);
    assert(store.checkpoint(h,0)); assert(blob.size()==200 && !h.dirty());
    h.update(5,1793318405,UsageState::active,b);
    assert(!store.checkpoint(h,5,true));
    assert(!store.checkpoint(h,59));
    assert(store.checkpoint(h,60)); // coalesced event was retained
    h.update(61,1793318461,UsageState::sleep,b);
    assert(store.checkpoint(h,61,true,true)); // pre-sleep bypass
    h.update(962,1793319362,UsageState::active,b);
    fail=true; assert(!store.checkpoint(h,962)); assert(h.dirty());
    auto before=attempts;
    assert(!store.checkpoint(h,1000)); assert(attempts==before);
    fail=false; assert(store.checkpoint(h,1022)); assert(!h.dirty());
    BatteryHistory restored; device::BatteryHistoryStore reader; reader.init(restored);
    assert(restored.data().days[20756%7].day == 20756); // Oct 30 2026
    assert(restored.data().days[20756%7].seconds == h.data().days[20756%7].seconds);
    blob[0]=99;
    BatteryHistory bad; device::BatteryHistoryStore invalid; invalid.init(bad);
    assert(bad.data().newest_day==0);
    std::cout << "Battery NVS checkpoint tests passed\n";
}
