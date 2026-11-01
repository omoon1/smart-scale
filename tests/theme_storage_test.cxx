#include "theme.hxx"
#include "nvs.h"
#include <cassert>
#include <cstring>
using namespace smart_scale;
int open_error{}, read_error{}, write_error{}, commit_error{};
unsigned writes{}, closes{};
std::uint8_t stored{}, staged{};
int nvs_open(const char* name, int, nvs_handle_t* h) {
    assert(std::strcmp(name, "appearance") == 0); *h = 1; return open_error;
}
int nvs_get_u8(nvs_handle_t, const char* key, std::uint8_t* value) {
    assert(std::strcmp(key, "theme") == 0); *value = stored; return read_error;
}
int nvs_set_u8(nvs_handle_t, const char* key, std::uint8_t value) {
    assert(std::strcmp(key, "theme") == 0); ++writes; staged = value; return write_error;
}
int nvs_commit(nvs_handle_t) { if (!commit_error) stored = staged; return commit_error; }
void nvs_close(nvs_handle_t) { ++closes; }
int main() {
    open_error = 1; assert(load_theme() == Theme::sage_green); assert(!save_theme(Theme::espresso_amber));
    open_error = 0; read_error = 1; assert(load_theme() == Theme::sage_green);
    read_error = 0; stored = 255; assert(load_theme() == Theme::sage_green);
    assert(!save_theme(static_cast<Theme>(255)) && writes == 0);
    for (auto theme : {Theme::sage_green, Theme::espresso_amber, Theme::porcelain_gray}) {
        assert(save_theme(theme)); assert(load_theme() == theme);
    }
    write_error = 1; assert(!save_theme(Theme::espresso_amber));
    write_error = 0; commit_error = 1; assert(!save_theme(Theme::espresso_amber));
    assert(load_theme() == Theme::porcelain_gray);
    assert(closes == 11);
}
