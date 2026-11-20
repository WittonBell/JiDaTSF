#include "DpiHelper.h"
#include <shellscalingapi.h>
#pragma comment(lib, "Shcore.lib")

void EnablePerMonitorV2() {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
}

UINT GetDpiForRect(const RECT& rc) {
  HMONITOR hmon = MonitorFromRect(&rc, MONITOR_DEFAULTTONEAREST);
  UINT dx = 96, dy = 96;
  if (SUCCEEDED(GetDpiForMonitor(hmon, MDT_EFFECTIVE_DPI, &dx, &dy)))
    return dx;
  return 96;
}