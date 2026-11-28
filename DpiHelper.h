#pragma once
// 高 DPI 感知初始化及指定矩形的当前 DPI 查询。
#include <windows.h>

void EnablePerMonitorV2();
UINT GetDpiForRect(const RECT& rc);