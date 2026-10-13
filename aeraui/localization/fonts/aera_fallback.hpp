/*
 * Copyright (C) 2026 AERA Recovery Project contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <string>

#include <lvgl.h>

namespace aeraui::fonts {

void InitializeFallbackFonts();
// All UI objects must be deleted; LVGL/FreeType must still be initialized.
void ShutdownFallbackFonts();
void SetFallbackLanguage(const std::string &language);
bool SelectUiFont(const std::string &id, const std::string &path);
const std::string &SelectedUiFont();
const lv_font_t *PreviewFont(const std::string &path, uint32_t size);
const lv_font_t *ScaledUiFont(const lv_font_t *font, int percent);
const lv_font_t *WithLanguageFallback(const lv_font_t *font);
const lv_font_t *DisplayFont(uint32_t size);

}  // namespace aeraui::fonts
