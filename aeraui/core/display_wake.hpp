/*
 * Copyright (C) 2026 AERA Recovery Project contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

namespace aeraui {

// Build the lock overlay while suspended, then re-enable the display pipeline
// before any frame can be submitted. A rejected blank-pipeline commit must not
// poison the renderer's scanout-ownership safety state during ordinary wake.
template <class Engine, class ScreenTimer, class Performance>
void WakeLockedDisplay(Engine& engine, ScreenTimer& timer, Performance& performance) {
  engine.ShowLockScreen();
  timer.Wake();
  engine.SetSuspended(false);
  performance.Boost();
  engine.RunFrame();
}

}  // namespace aeraui
