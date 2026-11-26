#include "DpiHelper.h"
#include "Globals.h"
#include "Config.h"

BOOL APIENTRY DllMain(HMODULE h, DWORD r, LPVOID) {
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

    SetJiDaTablePath(dir + L"data\\86海峰词库\\freeime.mb");
  }
  return TRUE;
}