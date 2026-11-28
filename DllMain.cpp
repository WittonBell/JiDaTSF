#include "DpiHelper.h"
// DLL 入口：初始化全局实例、配置、用户词及码表路径。
#include "Globals.h"
#include "Config.h"

BOOL APIENTRY DllMain(HMODULE h, DWORD r, LPVOID) {
  // 进程加载时保存模块句柄并初始化配置、用户词和资源路径。
  if (r == DLL_PROCESS_ATTACH) {
    g_hInstance = h;
    DisableThreadLibraryCalls(h);
    EnablePerMonitorV2();
    LoadJiDaConfig();
    LoadUserPhrases();

    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(h, buf, MAX_PATH);
    std::wstring dir(buf);
    auto slash = dir.find_last_of(L"\\/");
    if (slash != std::wstring::npos)
      dir = dir.substr(0, slash + 1);

    SetJiDaTablePath(dir + L"data\\jidian_wubi86.txt");
    // 不在 DllMain 里同步加载：由 Activate 调 LoadJiDaTableAsync()
  }
  return TRUE;
}