// TSF 配置档注册、注销及 AppContainer 读取权限设置实现。
#include <aclapi.h>
#include <msctf.h>
#include <sddl.h>
#include <shlwapi.h>
#include <string>
#include "Globals.h"

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shlwapi.lib")

static const LANGID kLangId = 0x0804;  // 简体中文
static const wchar_t kDesc[] = L"极达五笔";

STDAPI DllUnregisterServer();

    // ------------------------------------------------------------------
// COM：注册 CLSID\{...}\InprocServer32
// ------------------------------------------------------------------
static HRESULT RegisterComServer(const CLSID& clsid, const wchar_t* dllPath) {
  // 在注册表中登记 CLSID、DLL 路径及 Apartment 线程模型。
  wchar_t clsidStr[64] = {0};
  if (!StringFromGUID2(clsid, clsidStr, 64))
    return E_FAIL;

  std::wstring keyClsid =
      std::wstring(L"Software\\Classes\\CLSID\\") + clsidStr;

  HKEY hKey = nullptr;
  LONG ret = RegCreateKeyExW(HKEY_LOCAL_MACHINE, keyClsid.c_str(), 0, nullptr,
                             0, KEY_WRITE, nullptr, &hKey, nullptr);
  if (ret != ERROR_SUCCESS)
    return HRESULT_FROM_WIN32(ret);

  RegSetValueExW(hKey, nullptr, 0, REG_SZ, (const BYTE*)kDesc,
                 (DWORD)((wcslen(kDesc) + 1) * sizeof(wchar_t)));
  RegCloseKey(hKey);

  std::wstring keyInproc = keyClsid + L"\\InprocServer32";
  ret = RegCreateKeyExW(HKEY_LOCAL_MACHINE, keyInproc.c_str(), 0, nullptr, 0,
                        KEY_WRITE, nullptr, &hKey, nullptr);
  if (ret != ERROR_SUCCESS)
    return HRESULT_FROM_WIN32(ret);

  RegSetValueExW(hKey, nullptr, 0, REG_SZ, (const BYTE*)dllPath,
                 (DWORD)((wcslen(dllPath) + 1) * sizeof(wchar_t)));
  RegSetValueExW(hKey, L"ThreadingModel", 0, REG_SZ, (const BYTE*)L"Apartment",
                 (DWORD)(sizeof(L"Apartment")));
  RegCloseKey(hKey);

  return S_OK;
}

// ------------------------------------------------------------------
// 授予 AppContainer 读权限（S-1-15-2-1 / S-1-15-2-2），使 UWP 可加载
// ------------------------------------------------------------------
static HRESULT GrantAppContainerReadAccess(const wchar_t* path) {
  // 向通用 AppContainer 和受限 AppContainer SID 授予只读执行权限。
  if (!path || !*path)
    return E_INVALIDARG;

  PSID pAllAppPkgs = nullptr;
  PSID pRestrictedAppPkgs = nullptr;
  if (!ConvertStringSidToSidW(L"S-1-15-2-1", &pAllAppPkgs))
    return HRESULT_FROM_WIN32(GetLastError());
  if (!ConvertStringSidToSidW(L"S-1-15-2-2", &pRestrictedAppPkgs)) {
    LocalFree(pAllAppPkgs);
    return HRESULT_FROM_WIN32(GetLastError());
  }

  PACL pOldDacl = nullptr;
  PSECURITY_DESCRIPTOR pSD = nullptr;
  DWORD dwRes =
      GetNamedSecurityInfoW(path, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                            nullptr, nullptr, &pOldDacl, nullptr, &pSD);
  if (dwRes != ERROR_SUCCESS) {
    LocalFree(pAllAppPkgs);
    LocalFree(pRestrictedAppPkgs);
    return HRESULT_FROM_WIN32(dwRes);
  }

  EXPLICIT_ACCESSW ea[2] = {};
  ea[0].grfAccessPermissions = GENERIC_READ | GENERIC_EXECUTE;
  ea[0].grfAccessMode = GRANT_ACCESS;
  ea[0].grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
  ea[0].Trustee.TrusteeForm = TRUSTEE_IS_SID;
  ea[0].Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
  ea[0].Trustee.ptstrName = (LPWSTR)pAllAppPkgs;

  ea[1] = ea[0];
  ea[1].Trustee.ptstrName = (LPWSTR)pRestrictedAppPkgs;

  PACL pNewDacl = nullptr;
  dwRes = SetEntriesInAclW(2, ea, pOldDacl, &pNewDacl);

  LocalFree(pAllAppPkgs);
  LocalFree(pRestrictedAppPkgs);

  if (dwRes != ERROR_SUCCESS) {
    if (pSD)
      LocalFree(pSD);
    return HRESULT_FROM_WIN32(dwRes);
  }

  dwRes = SetNamedSecurityInfoW((LPWSTR)path, SE_FILE_OBJECT,
                                DACL_SECURITY_INFORMATION, nullptr, nullptr,
                                pNewDacl, nullptr);

  if (pNewDacl)
    LocalFree(pNewDacl);
  if (pSD)
    LocalFree(pSD);

  return (dwRes == ERROR_SUCCESS) ? S_OK : HRESULT_FROM_WIN32(dwRes);
}

// ------------------------------------------------------------------
// DllRegisterServer
// ------------------------------------------------------------------
STDAPI DllRegisterServer() {
  // 注册 COM 服务、TSF 配置档、分类特性和资源目录权限。
  HRESULT hr = S_OK;

  wchar_t dllPath[MAX_PATH] = {0};
  if (!GetModuleFileNameW(g_hInstance, dllPath, MAX_PATH))
    return HRESULT_FROM_WIN32(GetLastError());

  hr = RegisterComServer(CLSID_JiDaTSF, dllPath);
  if (FAILED(hr))
    return hr;

  HRESULT hrCo = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  bool bNeedCoUninit = SUCCEEDED(hrCo);

  ITfInputProcessorProfileMgr* pProfileMgr = nullptr;
  hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
                        CLSCTX_INPROC_SERVER, IID_ITfInputProcessorProfileMgr,
                        (void**)&pProfileMgr);
  if (SUCCEEDED(hr) && pProfileMgr) {
    hr = pProfileMgr->RegisterProfile(
        CLSID_JiDaTSF,                  // rclsid
        kLangId,                          // langid
        GUID_JiDa_LBI,                    // guidProfile
        kDesc, (ULONG)wcslen(kDesc),      // 描述字符串
        dllPath, (ULONG)wcslen(dllPath),  // 图标文件
        0,                                // uIconIndex
        nullptr,  // hklsubstitute = NULL（关键：非键盘布局）
        0,        // dwPreferredLayout（必须为 0）
        FALSE,    // bEnabledByDefault
        0         // dwFlags
    );
    pProfileMgr->Release();
    pProfileMgr = nullptr;
  }

  if (SUCCEEDED(hr)) {
    ITfCategoryMgr* pCategoryMgr = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr,
                                   CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr,
                                   (void**)&pCategoryMgr))) {
      pCategoryMgr->RegisterCategory(CLSID_JiDaTSF, GUID_TFCAT_TIP_KEYBOARD,
                                     CLSID_JiDaTSF);
      pCategoryMgr->RegisterCategory(
          CLSID_JiDaTSF, GUID_TFCAT_TIPCAP_UIELEMENTENABLED, CLSID_JiDaTSF);
      pCategoryMgr->RegisterCategory(
          CLSID_JiDaTSF, GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT, CLSID_JiDaTSF);
      pCategoryMgr->RegisterCategory(
          CLSID_JiDaTSF, GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT, CLSID_JiDaTSF);
      pCategoryMgr->Release();
    }
  }

  if (SUCCEEDED(hr)) {
    std::wstring dir(dllPath);
    size_t p = dir.find_last_of(L"\\/");
    if (p != std::wstring::npos)
      dir.resize(p);

    GrantAppContainerReadAccess(dir.c_str());
    GrantAppContainerReadAccess((dir + L"\\data").c_str());
  }

  if (bNeedCoUninit)
    CoUninitialize();

  if (FAILED(hr)) {
    DllUnregisterServer();
  }
  return hr;
}

// ------------------------------------------------------------------
// DllUnregisterServer
// ------------------------------------------------------------------
STDAPI DllUnregisterServer() {
  // 注销 TSF 配置档、类别和 CLSID 注册表项。
  HRESULT hrCo = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  bool bNeedCoUninit = SUCCEEDED(hrCo);

  ITfInputProcessorProfileMgr* pProfileMgr = nullptr;
  if (SUCCEEDED(CoCreateInstance(
          CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
          IID_ITfInputProcessorProfileMgr, (void**)&pProfileMgr))) {
    pProfileMgr->UnregisterProfile(CLSID_JiDaTSF, kLangId, GUID_JiDa_LBI, 0);
    pProfileMgr->Release();
  }

  ITfCategoryMgr* pCategoryMgr = nullptr;
  if (SUCCEEDED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr,
                                 CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr,
                                 (void**)&pCategoryMgr))) {
    pCategoryMgr->UnregisterCategory(CLSID_JiDaTSF, GUID_TFCAT_TIP_KEYBOARD,
                                     CLSID_JiDaTSF);
    pCategoryMgr->UnregisterCategory(
        CLSID_JiDaTSF, GUID_TFCAT_TIPCAP_UIELEMENTENABLED, CLSID_JiDaTSF);
    pCategoryMgr->UnregisterCategory(
        CLSID_JiDaTSF, GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT, CLSID_JiDaTSF);
    pCategoryMgr->UnregisterCategory(
        CLSID_JiDaTSF, GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT, CLSID_JiDaTSF);
    pCategoryMgr->Release();
  }

  wchar_t clsidStr[64] = {0};
  if (StringFromGUID2(CLSID_JiDaTSF, clsidStr, 64)) {
    std::wstring sub = std::wstring(L"Software\\Classes\\CLSID\\") + clsidStr;
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, sub.c_str());
  }

  if (bNeedCoUninit)
    CoUninitialize();
  return S_OK;
}