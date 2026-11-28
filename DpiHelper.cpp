#include "DpiHelper.h"
// Windows Per-Monitor V2 DPI 辅助实现。
#include <shellscalingapi.h>
#pragma comment(lib, "Shcore.lib")

void EnablePerMonitorV2() {
  // 在进程级启用 Per-Monitor V2，保证窗口可按所在显示器缩放。
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
}

UINT GetDpiForRect(const RECT& rc) {
  HMONITOR hmon = MonitorFromRect(&rc, MONITOR_DEFAULTTONEAREST);
  UINT dx = 96, dy = 96;
  if (SUCCEEDED(GetDpiForMonitor(hmon, MDT_EFFECTIVE_DPI, &dx, &dy)))
    return dx;
  return 96;
}