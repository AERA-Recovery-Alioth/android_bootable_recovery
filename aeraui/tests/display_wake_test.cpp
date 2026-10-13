/*
 * Copyright (C) 2026 AERA Recovery Project contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#include "../core/display_wake.hpp"
#include <cassert>

struct Display {
  bool active = false;
  bool locked = false;
  bool suspended = true;
  bool renderer_ready = true;
  unsigned frames = 0;
};
struct Engine {
  Display& display;
  void ShowLockScreen() { display.locked = true; }
  void SetSuspended(bool value) { display.suspended = value; }
  void RunFrame() {
    if (display.suspended || !display.renderer_ready) return;
    // Model the actual DRM rejection: drawing while blank permanently closes
    // the renderer until it is rebuilt, even if the panel is enabled later.
    if (!display.active) { display.renderer_ready = false; return; }
    assert(display.locked);
    ++display.frames;
  }
};
struct Timer {
  Display& display;
  void Wake() { assert(display.locked); display.active = true; }
};
struct Performance { void Boost() {} };

int main() {
  Display display;
  Engine engine{display};
  Timer timer{display};
  Performance performance;
  for (unsigned cycle = 0; cycle < 5; ++cycle) {
    display.active = false;
    display.locked = false;
    display.suspended = true;
    aeraui::WakeLockedDisplay(engine, timer, performance);
    assert(display.active && display.locked && !display.suspended);
    assert(display.renderer_ready && display.frames == cycle + 1);
  }
}
