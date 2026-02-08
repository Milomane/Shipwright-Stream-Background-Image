#pragma once

#include <string>

struct WidgetInfo;

namespace ObsBackground {
void InitOnce();     // create folders + register hook
void ForceRefresh(); // manual button in menu

void DrawPickerDefault(WidgetInfo& info); // menu widget

void DrawAreaPickerList(WidgetInfo& info);

void OpenOutputFolder(); // menu button
}
