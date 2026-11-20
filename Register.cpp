#include <comdef.h>
#include <msctf.h>
#include "Globals.h"
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "oleaut32.lib")

static const LANGID kLangId = 0x0804;
static const wchar_t kDesc[] = L"极达五笔";

static HRESULT RegisterCom(wchar_t* clsidStr, const wchar_t* dllPath) {
  HKEY k;
  std::wstring sub = std::wstring(L"CLSID\\") + clsidStr;
  if (RegCreateKeyExW(HKEY_CLASSES_ROOT, sub.c_str(), 0, nullptr, 0, KEY_WRITE,
                      nullptr, &k, nullptr) != ERROR_SUCCESS)
    return E_FAIL;
  RegSetValueExW(k, nullptr, 0, REG_SZ, (const BYTE*)L"JidaTSF",
                 (DWORD)((wcslen(L"JidaTSF") + 1) * sizeof(wchar_t)));
  RegCloseKey(k);

  sub = std::wstring(L"CLSID\\") + clsidStr + L"\\InprocServer32";
  if (RegCreateKeyExW(HKEY_CLASSES_ROOT, sub.c_str(), 0, nullptr, 0, KEY_WRITE,
                      nullptr, &k, nullptr) != ERROR_SUCCESS)
    return E_FAIL;
  RegSetValueExW(k, nullptr, 0, REG_SZ, (const BYTE*)dllPath,
                 (DWORD)((wcslen(dllPath) + 1) * sizeof(wchar_t)));
  RegSetValueExW(k, L"ThreadingModel", 0, REG_SZ, (const BYTE*)L"Apartment",
                 (DWORD)(sizeof(L"Apartment")));
  RegCloseKey(k);
  return S_OK;
}

STDAPI DllRegisterServer() {
  wchar_t dll[MAX_PATH];
  GetModuleFileNameW(g_hInstance, dll, MAX_PATH);
  wchar_t clsidStr[64];
  StringFromGUID2(CLSID_JidaTSF, clsidStr, 64);
  if (FAILED(RegisterCom(clsidStr, dll)))
    return E_FAIL;

  ITfInputProcessorProfiles* profiles = nullptr;
  HRESULT hr = CoCreateInstance(
      CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
      IID_ITfInputProcessorProfiles, (void**)&profiles);
  if (FAILED(hr))
    return hr;
  profiles->Register(CLSID_JidaTSF);
  profiles->AddLanguageProfile(CLSID_JidaTSF, kLangId, GUID_LBI, kDesc,
                               (ULONG)wcslen(kDesc), dll, (ULONG)wcslen(dll),
                               0);
  profiles->Release();

  ITfCategoryMgr* cat = nullptr;
  hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
                        IID_ITfCategoryMgr, (void**)&cat);
  if (SUCCEEDED(hr)) {
    cat->RegisterCategory(CLSID_JidaTSF, GUID_TFCAT_TIP_KEYBOARD,
                          CLSID_JidaTSF);
    cat->RegisterCategory(CLSID_JidaTSF, GUID_TFCAT_TIPCAP_UIELEMENTENABLED,
                          CLSID_JidaTSF);
    cat->Release();
  }
  return S_OK;
}

STDAPI DllUnregisterServer() {
  ITfInputProcessorProfiles* profiles = nullptr;
  if (SUCCEEDED(CoCreateInstance(
          CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
          IID_ITfInputProcessorProfiles, (void**)&profiles))) {
    profiles->RemoveLanguageProfile(CLSID_JidaTSF, kLangId, GUID_LBI);
    profiles->Unregister(CLSID_JidaTSF);
    profiles->Release();
  }
  wchar_t clsidStr[64];
  StringFromGUID2(CLSID_JidaTSF, clsidStr, 64);
  std::wstring sub = std::wstring(L"CLSID\\") + clsidStr;
  RegDeleteTreeW(HKEY_CLASSES_ROOT, sub.c_str());
  return S_OK;
}