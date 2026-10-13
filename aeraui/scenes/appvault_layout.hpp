/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <algorithm>
#include <lvgl.h>

namespace aeraui::appvault::layout {
inline int PlaceBelow(lv_obj_t *label, lv_obj_t *above, int minimum_y, int gap) {
  lv_obj_update_layout(above);
  const int y = std::max(minimum_y, static_cast<int>(
      lv_obj_get_y(above) + lv_obj_get_height(above) + gap));
  lv_obj_set_y(label, y);
  lv_obj_update_layout(label);
  return y + lv_obj_get_height(label);
}
inline int Row(lv_obj_t *row, lv_obj_t *title, lv_obj_t *detail,
               int minimum_detail_y, int minimum_height) {
  const int bottom = PlaceBelow(detail, title, minimum_detail_y, 12);
  lv_obj_set_height(row, std::max(minimum_height, bottom + 20));
  lv_obj_update_layout(row);
  return lv_obj_get_height(row);
}
inline int RestoreHeader(lv_obj_t *title, lv_obj_t *subtitle, lv_obj_t *selected) {
  PlaceBelow(subtitle, title, 88, 12);
  return PlaceBelow(selected, subtitle, 150, 20) + 24;
}
inline int Hero(lv_obj_t *status, lv_obj_t *detail, lv_obj_t *location_title,
                lv_obj_t *location, lv_obj_t *protection, lv_obj_t *progress) {
  PlaceBelow(detail, status, 104, 12);
  PlaceBelow(location_title, detail, 218, 28);
  PlaceBelow(location, location_title, 266, 12);
  PlaceBelow(protection, location, 334, 16);
  return PlaceBelow(progress, protection, 430, 28) + 40;
}
inline int AppsHeader(lv_obj_t *title, lv_obj_t *selected, lv_obj_t *user) {
  PlaceBelow(selected, title, 82, 12);
  return PlaceBelow(user, selected, 130, 16) + 14;
}
}  // namespace aeraui::appvault::layout
