// SPDX-License-Identifier: Apache-2.0
#include <cassert>
#include <vector>
#include "../core/status_bar.cpp"

static int autosave_count = 0;
static aeraui::WifiConnection wifi_connection;
static aeraui::WifiTraffic wifi_traffic;
static aeraui::WifiStatusDisplay wifi_display;
static int traffic_reads = 0;
static aeraui::BatteryStyle battery_style = aeraui::BatteryStyle::kClassic;
static int battery_circle_size = aeraui::kBatteryCircleSizeDefault;
static bool battery_percentage = true;
static bool clock24 = true;
static bool recording_active = false;
static aeraui::plugin_api::MirrorMode mirror_mode = aeraui::plugin_api::MirrorMode::kOff;
static aeraui::web::DownloadSummary download_summary;
namespace aeraui {
std::string RecoveryUiFont() { return {}; }
bool RecoveryPreference(Preference preference) { return preference == Preference::kClock24 ? clock24 : true; }
int RecoveryBrightness() { return 50; }
void RecoverySetBrightness(int) {}
bool RecoveryAutoSavePreferences() { ++autosave_count; return true; }
bool RecoveryFlashlightSupported() { return false; }
bool RecoveryFlashlightEnabled() { return false; }
bool RecoverySetFlashlight(bool) { return false; }
WifiStatus RecoveryWifiStatus() { return {}; }
WifiConnection RecoveryWifiConnection() { return wifi_connection; }
WifiTraffic RecoveryWifiTraffic() { ++traffic_reads; return wifi_traffic; }
WifiStatusDisplay RecoveryWifiStatusDisplay() { return wifi_display; }
BatteryStyle RecoveryBatteryStyle() { return battery_style; }
int RecoveryBatteryCircleSize() { return battery_circle_size; }
bool RecoveryBatteryPercentage() { return battery_percentage; }
void RecoveryVibrate(Haptic) {}
}
namespace aeraui::web { DownloadSummary CurrentDownloadSummary() { return download_summary; } }
namespace aeraui::plugin_api { MirrorMode ActiveMirrorMode() { return mirror_mode; } }
namespace aeraui::update { Snapshot GetSnapshot() { return {}; } }
namespace aeraui::recorder {
Snapshot GetSnapshot() { Snapshot result; result.state = State::kRecording; return result; }
bool Active() { return recording_active; }
bool Installed() { return false; }
}

static lv_indev_data_t pointer{};
static lv_indev_t *input;
static void Touch(int x, int y, bool pressed) {
  pointer.point = {x, y};
  pointer.state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
  lv_indev_read(input);
}
static void Settle() {
  for (int i = 0; i < 12; ++i) { lv_tick_inc(32); lv_timer_handler(); }
}

static void CheckTrafficRate() {
  using namespace aeraui;
  assert(widgets::WifiSignalLevel(-999) == -1);
  assert(widgets::WifiSignalLevel(0) == -1);
  assert(widgets::WifiSignalLevel(-89) == 0);
  assert(widgets::WifiSignalLevel(-88) == 1);
  assert(widgets::WifiSignalLevel(-78) == 1);
  assert(widgets::WifiSignalLevel(-77) == 2);
  assert(widgets::WifiSignalLevel(-67) == 2);
  assert(widgets::WifiSignalLevel(-66) == 3);
  assert(widgets::WifiSignalLevel(-56) == 3);
  assert(widgets::WifiSignalLevel(-55) == 4);
  TrafficRate rate;
  WifiTraffic traffic{true, "wlan0", 12, 10000000, 20000000};
  assert(rate.Sample(traffic, 1000) == 0);
  traffic.received_bytes += 700000;
  traffic.sent_bytes += 300000;
  assert(rate.Sample(traffic, 2000) == 1000000);
  assert(TrafficText(rate.bytes_per_second) == "1.0 MB/s");
  assert(TrafficText(850000) == "850 KB/s");
  assert(TrafficText(12400000) == "12.4 MB/s");
  traffic.received_bytes += 700000;
  traffic.sent_bytes += 300000;
  assert(rate.Sample(traffic, 4000) == 500000);
  assert(rate.Sample(traffic, 5000) == 0);
  traffic.received_bytes = 1;
  assert(rate.Sample(traffic, 6000) == 0);
  traffic.interface_index++;
  assert(rate.Sample(traffic, 7000) == 0);
  traffic.interface = "wifi1";
  assert(rate.Sample(traffic, 8000) == 0);
  assert(rate.Sample({}, 9000) == 0);
  assert(!rate.previous.valid);
  assert(rate.Sample(traffic, UINT32_MAX - 499) == 0);
  traffic.received_bytes += 1000;
  assert(rate.Sample(traffic, 500) == 1000);
  assert(rate.Sample(traffic, 500) == 1000);
}

static void CheckTrafficLayout(lv_display_t *display) {
  using namespace aeraui;
  for (const auto action : {StatusBarAction::kNone, StatusBarAction::kBack, StatusBarAction::kPower}) {
    for (bool crowded : {false, true}) {
      for (const int width : {1440, 3168}) {
        lv_display_set_resolution(display, width, 3168);
        ConfigureStatusBar(165, 90, 90);
        clock24 = !crowded;
        recording_active = crowded;
        mirror_mode = crowded ? plugin_api::MirrorMode::kWifi : plugin_api::MirrorMode::kOff;
        download_summary.active_count = crowded ? 1 : 0;
        wifi_connection = {true, "A very long network name that should not overlap other status indicators"};
        wifi_traffic = {true, "wlan0", 12, 0, 0};
        wifi_display = {};
        battery_style = BatteryStyle::kClassic;
        battery_circle_size = kBatteryCircleSizeDefault;
        battery_percentage = true;
        auto *screen = lv_obj_create(nullptr);
        lv_screen_load(screen);
        AttachStatusBar(screen, nullptr, nullptr, action);
        auto *state = static_cast<StatusState *>(
            lv_event_dsc_get_user_data(lv_obj_get_event_dsc(screen, 0)));
        lv_tick_inc(1000);
        wifi_traffic.received_bytes += 700000;
        wifi_traffic.sent_bytes += 300000;
        Refresh(state, false);
        lv_obj_update_layout(screen);
        assert(!lv_obj_has_flag(state->wifi_speed, LV_OBJ_FLAG_HIDDEN));
        assert(std::string(lv_label_get_text(state->wifi_speed)) == "1.0 MB/s");
        assert(lv_obj_get_x(state->wifi_speed) + lv_obj_get_width(state->wifi_speed) < lv_obj_get_x(state->wifi_icon.root));
        assert(lv_obj_get_x(state->wifi) + lv_obj_get_width(state->wifi) < lv_obj_get_x(state->battery_text));
        auto *left = crowded ? state->recording : state->clock;
        assert(lv_obj_get_x(left) + lv_obj_get_width(left) < lv_obj_get_x(state->wifi_speed));
        const int original_speed_x = lv_obj_get_x(state->wifi_speed);
        wifi_display.speed_position = 0;
        RefreshStatusBars();
        lv_obj_update_layout(screen);
        assert(lv_obj_get_x(state->wifi_speed) <= original_speed_x);
        assert(lv_obj_get_x(left) + lv_obj_get_width(left) < lv_obj_get_x(state->wifi_speed));
        const int leftmost_speed_x = lv_obj_get_x(state->wifi_speed);
        wifi_display.speed_position = 50;
        RefreshStatusBars();
        lv_obj_update_layout(screen);
        assert(lv_obj_get_x(state->wifi_speed) >= leftmost_speed_x);
        assert(lv_obj_get_x(state->wifi_speed) <= original_speed_x);
        wifi_display.speed_position = 100;
        RefreshStatusBars();
        lv_obj_update_layout(screen);
        assert(lv_obj_get_x(state->wifi_speed) == original_speed_x);
        const int original_wifi_x = lv_obj_get_x(state->wifi_icon.root);
        wifi_display.show_name = false;
        RefreshStatusBars();
        lv_obj_update_layout(screen);
        assert(lv_obj_has_flag(state->wifi, LV_OBJ_FLAG_HIDDEN));
        assert(!lv_obj_has_flag(state->wifi_icon.root, LV_OBJ_FLAG_HIDDEN));
        assert(lv_obj_get_x(state->wifi_icon.root) >= original_wifi_x);
        assert(lv_obj_get_x(state->wifi_speed) >= original_speed_x);
        state->battery = 100;
        state->charging = false;
        Refresh(state, false);
        lv_obj_update_layout(screen);
        const int unplugged_wifi_x = lv_obj_get_x(state->wifi_icon.root);
        const int unplugged_speed_x = lv_obj_get_x(state->wifi_speed);
        state->charging = true;
        Refresh(state, false);
        lv_obj_update_layout(screen);
        assert(lv_obj_get_x(state->wifi_icon.root) < unplugged_wifi_x);
        assert(lv_obj_get_x(state->battery_text) - lv_obj_get_x(state->wifi_icon.root) - widgets::kWifiIndicatorWidth == 24);
        state->charging = false;
        Refresh(state, false);
        lv_obj_update_layout(screen);
        assert(lv_obj_get_x(state->wifi_icon.root) == unplugged_wifi_x);
        assert(lv_obj_get_x(state->wifi_speed) == unplugged_speed_x);
        assert(lv_obj_get_x(state->battery_text) - lv_obj_get_x(state->wifi_icon.root) - widgets::kWifiIndicatorWidth == 24);
        battery_percentage = false;
        RefreshStatusBars(); lv_obj_update_layout(screen);
        assert(lv_obj_has_flag(state->battery_text, LV_OBJ_FLAG_HIDDEN));
        assert(!lv_obj_has_flag(state->battery_body, LV_OBJ_FLAG_HIDDEN));
        assert(lv_obj_get_x(state->wifi_icon.root) > unplugged_wifi_x);
        const int no_percentage_wifi_x = lv_obj_get_x(state->wifi_icon.root);
        state->charging = true;
        RefreshStatusBars(); lv_obj_update_layout(screen);
        assert(!lv_obj_has_flag(state->battery_charge, LV_OBJ_FLAG_HIDDEN));
        assert(lv_obj_get_x(state->wifi_icon.root) == no_percentage_wifi_x);
        state->charging = false;
        battery_percentage = true;
        battery_style = BatteryStyle::kCircle;
        RefreshStatusBars(); lv_obj_update_layout(screen);
        assert(lv_obj_get_x(state->wifi_icon.root) > unplugged_wifi_x);
        assert(lv_obj_has_flag(state->battery_text, LV_OBJ_FLAG_HIDDEN));
        assert(!lv_obj_has_flag(state->battery_circle.root, LV_OBJ_FLAG_HIDDEN));
        assert(std::string(lv_label_get_text(state->battery_circle.number)).empty());
        assert(lv_arc_get_value(state->battery_circle.ring) == 100);
        for (int level : {-1, 0, 1, 15, 99, 100}) {
          state->battery = level;
          RefreshStatusBars(); lv_obj_update_layout(screen);
          const std::string expected = level < 0 ? "--" : level == 100 ? "" : std::to_string(level);
          assert(std::string(lv_label_get_text(state->battery_circle.number)) == expected);
          assert(lv_obj_get_width(state->battery_circle.root) == battery_circle_size);
        }
        state->battery = 99;
        for (int size = kBatteryCircleSizeMin; size <= kBatteryCircleSizeMax; ++size) {
          battery_circle_size = size;
          RefreshStatusBars(); lv_obj_update_layout(screen);
          assert(lv_obj_get_width(state->battery_circle.root) == size);
          assert(lv_obj_get_x(state->battery_circle.number) >= 0);
          assert(lv_obj_get_y(state->battery_circle.number) >= 0);
          assert(lv_obj_get_x(state->battery_circle.number) + lv_obj_get_width(state->battery_circle.number) <= size);
          assert(lv_obj_get_y(state->battery_circle.number) + lv_obj_get_height(state->battery_circle.number) <= size);
          assert(lv_obj_get_x(state->battery_circle.root) - lv_obj_get_x(state->wifi_icon.root) - widgets::kWifiIndicatorWidth == 24);
        }
        battery_circle_size = kBatteryCircleSizeDefault;
        RefreshStatusBars(); lv_obj_update_layout(screen);
        const int circle_wifi_x = lv_obj_get_x(state->wifi_icon.root);
        state->charging = true;
        RefreshStatusBars(); lv_obj_update_layout(screen);
        assert(!lv_obj_has_flag(state->battery_circle.charge, LV_OBJ_FLAG_HIDDEN));
        assert(lv_obj_get_x(state->wifi_icon.root) == circle_wifi_x);
        for (const int dbm : {-90, -80, -70, -60, -50, -999}) {
          wifi_connection.signal_dbm = dbm;
          RefreshStatusBars();
          const int level = widgets::WifiSignalLevel(dbm);
          for (int i = 0; i < 3; ++i)
            assert(lv_obj_get_style_arc_opa(state->wifi_icon.arcs[i], LV_PART_MAIN) ==
                (level >= i + 2 ? LV_OPA_COVER : LV_OPA_30));
        }
        wifi_display.show_speed = false;
        const int reads = traffic_reads;
        RefreshStatusBars();
        assert(traffic_reads == reads);
        assert(lv_obj_has_flag(state->wifi_speed, LV_OBJ_FLAG_HIDDEN));
        assert(!lv_obj_has_flag(state->wifi_icon.root, LV_OBJ_FLAG_HIDDEN));
        wifi_display.show_speed = true;
        wifi_connection.connected = false;
        Refresh(state, false);
        assert(lv_obj_has_flag(state->wifi_speed, LV_OBJ_FLAG_HIDDEN));
        assert(lv_obj_has_flag(state->wifi, LV_OBJ_FLAG_HIDDEN));
        assert(lv_obj_has_flag(state->wifi_icon.root, LV_OBJ_FLAG_HIDDEN));
        wifi_connection.connected = true;
        wifi_traffic.valid = false;
        Refresh(state, false);
        assert(lv_obj_has_flag(state->wifi_speed, LV_OBJ_FLAG_HIDDEN));
        assert(!lv_obj_has_flag(state->wifi_icon.root, LV_OBJ_FLAG_HIDDEN));
        lv_obj_delete(screen);
        assert(gStatusBars.empty());
      }
    }
  }
}

int main() {
  using namespace aeraui;
  CheckTrafficRate();
  lv_init(); i18n::Initialize("en");
  auto *display = lv_display_create(1440, 3168);
  std::vector<uint8_t> frame(1440 * 3168 * 4);
  lv_display_set_buffers(display, frame.data(), nullptr, frame.size(), LV_DISPLAY_RENDER_MODE_DIRECT);
  lv_display_set_flush_cb(display, [](lv_display_t *d, const lv_area_t *, uint8_t *) { lv_display_flush_ready(d); });
  auto *screen = lv_screen_active();
  StatusState state{}; state.screen = screen; state.callback = [](Action, void *) {};
  state.bar = lv_obj_create(screen);
  design::Clear(state.bar); lv_obj_set_size(state.bar, 1440, 165);
  lv_obj_add_flag(state.bar, LV_OBJ_FLAG_CLICKABLE);
  for (auto event : {LV_EVENT_PRESSED, LV_EVENT_PRESSING, LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST})
    lv_obj_add_event_cb(state.bar, StatusGesture, event, &state);
  input = lv_indev_create(); lv_indev_set_type(input, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(input, [](lv_indev_t *, lv_indev_data_t *data) { *data = pointer; });
  lv_obj_update_layout(screen);

  Touch(400, 2800, true); Touch(400, 2800, false);
  assert(state.shade == nullptr);
  Touch(400, 80, true); Touch(400, 80, false); Settle();
  assert(state.shade && state.shade_visible == state.shade_height);
  lv_slider_set_value(state.brightness_slider, 70, LV_ANIM_OFF);
  lv_obj_send_event(state.brightness_slider, LV_EVENT_VALUE_CHANGED, nullptr);
  assert(autosave_count == 0);
  lv_obj_send_event(state.brightness_slider, LV_EVENT_RELEASED, nullptr);
  assert(autosave_count == 1);
  AnimateShade(&state, false); Settle();

  Touch(400, 80, true);
  lv_obj_send_event(state.bar, LV_EVENT_PRESS_LOST, nullptr);
  Touch(400, 80, false); Settle();
  assert(state.shade == nullptr && !state.dragging && !StatusBarShadeOpen());

  Touch(400, 80, true); Touch(800, 80, true); Touch(800, 80, false); Settle();
  assert(state.shade == nullptr);
  Touch(400, 80, true); Touch(400, 480, true); Touch(400, 480, false); Settle();
  assert(state.shade && state.shade_visible == state.shade_height);
  AnimateShade(&state, false); Settle();

  Touch(400, 80, true); Touch(400, 480, true);
  lv_obj_send_event(state.bar, LV_EVENT_PRESS_LOST, nullptr);
  Touch(400, 480, false); Settle();
  assert(state.shade == nullptr && !state.dragging);
  wifi_connection.connected = true;
  wifi_connection.ssid = "A very long network name used to check status collisions";
  for (int width : {1080, 1440})
  for (int scale : {100, 125, 150})
  for (auto mode : {plugin_api::MirrorMode::kOff, plugin_api::MirrorMode::kWifi}) {
    lv_display_set_resolution(display, width, width == 1080 ? 2400 : 3168);
    design::kDeviceFontScale = scale; design::ApplyInterfaceSize(3);
    mirror_mode = mode;
    auto* page = lv_obj_create(nullptr);
    AttachStatusBar(page, [](Action, void*) {}, nullptr, StatusBarAction::kNone, true);
    auto* status = static_cast<StatusState*>(
        lv_event_dsc_get_user_data(lv_obj_get_event_dsc(page, 0)));
    assert(status && status->bar);
    status->battery = 100; status->charging = true;
    Refresh(status, false); lv_obj_update_layout(page);
    if (!lv_obj_has_flag(status->wifi, LV_OBJ_FLAG_HIDDEN)) {
      const int left = lv_obj_get_x(status->wifi);
      const int right = left + lv_obj_get_width(status->wifi);
      assert(right + 24 <= lv_obj_get_x(status->battery_text));
      for (auto* indicator : {status->clock, status->mirror, status->download,
                             status->recording_dot, status->recording})
        if (!lv_obj_has_flag(indicator, LV_OBJ_FLAG_HIDDEN))
          assert(left >= lv_obj_get_x(indicator) + lv_obj_get_width(indicator) + 24);
    }
    lv_obj_delete(page);
  }
  lv_indev_delete(input);
  CheckTrafficLayout(display);
  lv_display_delete(display); lv_deinit();
  puts("Bottom taps, status taps, pull-down, horizontal motion and canceled gestures passed");
  puts("Combined Wi-Fi throughput, units, resets and status layouts passed");
}
