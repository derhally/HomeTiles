#pragma once

// The Assist screen styles drawn mostly from lines: Sentinel, Phosphor,
// Light Disc, Lens and Iris (assist_styles_lines.cpp). assist_styles.cpp
// dispatches to them.
//
// Loop task (LVGL) only.

#include <lvgl.h>

#include "src/ui/assist/assist_styles.h"

namespace assist_styles {

namespace sentinel {
void create(lv_obj_t* parent);
void frame(const Frame& f);
}  // namespace sentinel

namespace phosphor {
void create(lv_obj_t* parent);
void frame(const Frame& f);
}  // namespace phosphor

namespace disc {
void create(lv_obj_t* parent);
void frame(const Frame& f);
}  // namespace disc

namespace lens {
void create(lv_obj_t* parent);
void frame(const Frame& f);
}  // namespace lens

namespace iris {
void create(lv_obj_t* parent);
void frame(const Frame& f);
}  // namespace iris

}  // namespace assist_styles
