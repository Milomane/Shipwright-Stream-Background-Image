#pragma once

#include <string>

struct WidgetInfo;

namespace ObsBackground {
void InitOnce();     // create folders + register hook
void ForceRefresh(); // manual button in menu

void DrawPickerKokiri(WidgetInfo& info);  // menu widget
void DrawPickerDefault(WidgetInfo& info); // menu widget

void OpenOutputFolder(); // menu button
}
