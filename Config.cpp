#include "Config.h"
#include <algorithm>
#include <shlobj.h>
#include "Globals.h"
#pragma comment(lib, "shell32.lib")

namespace {
void LogConfigError(const wchar_t* operation, DWORD error) {
  wchar_t message[256];
  swprintf_s(message, L"[JiDa] %ls failed (error %lu)\n", operation, error);
  OutputDebugStringW(message);
}

void WriteProfileString(const wchar_t* section,
                        const wchar_t* key,
                        const wchar_t* value,
                        const wchar_t* path) {
  if (!WritePrivateProfileStringW(section, key, value, path))
    LogConfigError(L"WritePrivateProfileStringW", GetLastError());
}

void MigrateLegacyConfigFile(const std::wstring& legacyDir,
                             const std::wstring& configDir,
                             const wchar_t* fileName) {
  std::wstring source = legacyDir + L"\\" + fileName;
  std::wstring destination = configDir + L"\\" + fileName;
  if (GetFileAttributesW(destination.c_str()) != INVALID_FILE_ATTRIBUTES ||
      GetFileAttributesW(source.c_str()) == INVALID_FILE_ATTRIBUTES)
    return;

  if (!CopyFileW(source.c_str(), destination.c_str(), TRUE)) {
    DWORD error = GetLastError();
    if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS)
      LogConfigError(L"CopyFileW", error);
  }
}
}  // namespace

std::wstring GetJiDaConfigDir() {
  if (!g_hInstance) {
    LogConfigError(L"GetModuleFileNameW", ERROR_INVALID_HANDLE);
    return L"";
  }

  std::vector<wchar_t> modulePath(512);
  DWORD length = 0;
  for (;;) {
    SetLastError(ERROR_SUCCESS);
    length = GetModuleFileNameW(g_hInstance, modulePath.data(),
                                static_cast<DWORD>(modulePath.size()));
    if (!length) {
      LogConfigError(L"GetModuleFileNameW", GetLastError());
      return L"";
    }
    if (length < modulePath.size())
      break;
    if (modulePath.size() >= 32768) {
      LogConfigError(L"GetModuleFileNameW", ERROR_INSUFFICIENT_BUFFER);
      return L"";
    }
    modulePath.resize((std::min)(modulePath.size() * 2, size_t{32768}));
  }

  std::wstring dir(modulePath.data(), length);
  size_t slash = dir.find_last_of(L"\\/");
  if (slash == std::wstring::npos) {
    LogConfigError(L"Resolve DLL directory", ERROR_INVALID_NAME);
    return L"";
  }
  dir.resize(slash);
  dir += L"\\conf";

  DWORD attributes = GetFileAttributesW(dir.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES) {
    if (!CreateDirectoryW(dir.c_str(), nullptr) &&
        GetLastError() != ERROR_ALREADY_EXISTS) {
      LogConfigError(L"CreateDirectoryW", GetLastError());
      return L"";
    }
    attributes = GetFileAttributesW(dir.c_str());
  }
  if (attributes == INVALID_FILE_ATTRIBUTES ||
      !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
    LogConfigError(L"GetFileAttributesW",
                   attributes == INVALID_FILE_ATTRIBUTES ? GetLastError()
                                                          : ERROR_DIRECTORY);
    return L"";
  }

  PWSTR appDataPath = nullptr;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr,
                                     &appDataPath))) {
    std::wstring legacyDir = appDataPath;
    CoTaskMemFree(appDataPath);
    legacyDir += L"\\JiDaTSF";
    MigrateLegacyConfigFile(legacyDir, dir, L"config.ini");
    MigrateLegacyConfigFile(legacyDir, dir, L"userdict.txt");
  }
  return dir;
}

std::wstring GetJiDaConfigPath() {
  static std::wstring cached;
  if (cached.empty()) {
    std::wstring dir = GetJiDaConfigDir();
    if (!dir.empty())
      cached = dir + L"\\config.ini";
  }
  return cached;
}

static int ReadInt(const wchar_t* sec, const wchar_t* key, int def) {
  return (int)GetPrivateProfileIntW(sec, key, def, GetJiDaConfigPath().c_str());
}
static bool ReadBool(const wchar_t* sec, const wchar_t* key, bool def) {
  return ReadInt(sec, key, def ? 1 : 0) != 0;
}
static CharacterSetVersion ReadCharacterSetVersion(const wchar_t* section) {
  int version = ReadInt(section, L"CharacterSetVersion", -1);
  if (version == -1) {
    bool legacyGbkExt = ReadBool(
        section, L"GbkExt",
        g_config.characterSetVersion != CharacterSetVersion::GB2312);
    version = legacyGbkExt ? static_cast<int>(CharacterSetVersion::GBK)
                           : static_cast<int>(CharacterSetVersion::GB2312);
  }
  if (version < static_cast<int>(CharacterSetVersion::GB2312) ||
      version > static_cast<int>(CharacterSetVersion::GB18030_2005))
    return g_config.characterSetVersion;
  return static_cast<CharacterSetVersion>(version);
}
static COLORREF ReadColor(const wchar_t* sec,
                          const wchar_t* key,
                          COLORREF def) {
  return (COLORREF)ReadInt(sec, key, (int)def);
}
static void WriteInt(const wchar_t* sec, const wchar_t* key, int v) {
  wchar_t buf[32];
  swprintf_s(buf, L"%d", v);
  WriteProfileString(sec, key, buf, GetJiDaConfigPath().c_str());
}
static void WriteBool(const wchar_t* sec, const wchar_t* key, bool v) {
  WriteProfileString(sec, key, v ? L"1" : L"0", GetJiDaConfigPath().c_str());
}
static void WriteColor(const wchar_t* sec, const wchar_t* key, COLORREF c) {
  WriteInt(sec, key, (int)c);
}
static void ReadStr(const wchar_t* sec,
                    const wchar_t* key,
                    const wchar_t* def,
                    wchar_t* out,
                    DWORD cch) {
  GetPrivateProfileStringW(sec, key, def, out, cch,
                           GetJiDaConfigPath().c_str());
}
static void WriteStr(const wchar_t* sec, const wchar_t* key, const wchar_t* v) {
  WriteProfileString(sec, key, v, GetJiDaConfigPath().c_str());
}
static void ReadKeyMappings(const wchar_t* section,
                            std::map<UINT, KeyCharacterPair>& mappings) {
  mappings.clear();
  for (size_t i = 0; i < g_virtualKeyboardKeyCount; ++i) {
    const VirtualKeyDefinition& key = g_virtualKeyboardKeys[i];
    wchar_t normalKey[32], shiftedKey[32];
    swprintf_s(normalKey, L"%03X_Normal", key.key);
    swprintf_s(shiftedKey, L"%03X_Shift", key.key);
    wchar_t normal[4] = {}, shifted[4] = {};
    ReadStr(section, normalKey, L"", normal, _countof(normal));
    ReadStr(section, shiftedKey, L"", shifted, _countof(shifted));
    if (normal[0] || shifted[0])
      mappings[key.key] = {normal, shifted};
  }
}
static void WriteKeyMappings(
    const wchar_t* section,
    const std::map<UINT, KeyCharacterPair>& mappings) {
  for (size_t i = 0; i < g_virtualKeyboardKeyCount; ++i) {
    const VirtualKeyDefinition& key = g_virtualKeyboardKeys[i];
    wchar_t normalKey[32], shiftedKey[32];
    swprintf_s(normalKey, L"%03X_Normal", key.key);
    swprintf_s(shiftedKey, L"%03X_Shift", key.key);
    auto mapping = mappings.find(key.key);
    WriteStr(section, normalKey,
             mapping == mappings.end() ? L"" : mapping->second.normal.c_str());
    WriteStr(section, shiftedKey,
             mapping == mappings.end() ? L"" : mapping->second.shifted.c_str());
  }
}

void LoadJiDaConfig() {
  std::wstring configPath = GetJiDaConfigPath();
  if (configPath.empty() ||
      GetFileAttributesW(configPath.c_str()) == INVALID_FILE_ATTRIBUTES)
    return;

  const wchar_t* in = L"Input";
  g_config.zWildcard = ReadBool(in, L"ZWildcard", g_config.zWildcard);
  g_config.zRepeat = ReadBool(in, L"ZRepeat", g_config.zRepeat);
  g_config.semicolonSecond =
      ReadBool(in, L"SemicolonSecond", g_config.semicolonSecond);
  g_config.punctuationTop =
      ReadBool(in, L"PunctuationTop", g_config.punctuationTop);
  g_config.perKeyHint = ReadBool(in, L"PerKeyHint", g_config.perKeyHint);
  g_config.characterSetVersion = ReadCharacterSetVersion(in);
  g_config.inlinePreedit =
      ReadBool(in, L"InlinePreedit", g_config.inlinePreedit);
  g_config.shiftDirectCommit =
      ReadBool(in, L"ShiftDirectCommit", g_config.shiftDirectCommit);
  int shortcut = ReadInt(in, L"ChineseToggleShortcut",
                         static_cast<int>(g_config.chineseToggleShortcut));
  if (shortcut >= static_cast<int>(ChineseToggleShortcut::Disabled) &&
      shortcut <= static_cast<int>(ChineseToggleShortcut::Control))
    g_config.chineseToggleShortcut =
        static_cast<ChineseToggleShortcut>(shortcut);
  ReadKeyMappings(L"SoftKeyboard", g_config.softKeyboard);
  ReadKeyMappings(L"Punctuation", g_config.customPunctuation);
  const wchar_t* keyboardDisplay = L"KeyboardDisplay";
  g_config.keyboardKeyLabelColor =
      ReadColor(keyboardDisplay, L"KeyLabelColor",
                g_config.keyboardKeyLabelColor);
  g_config.keyboardNormalColor =
      ReadColor(keyboardDisplay, L"NormalColor", g_config.keyboardNormalColor);
  g_config.keyboardShiftedColor =
      ReadColor(keyboardDisplay, L"ShiftedColor",
                g_config.keyboardShiftedColor);

  const wchar_t* DP = L"Display";
  int vert = ReadInt(DP, L"Vertical",
                     g_config.candLayout == CandLayout::Vertical ? 1 : 0);
  g_config.candLayout = vert ? CandLayout::Vertical : CandLayout::Horizontal;
  g_config.candPageSize = ReadInt(DP, L"PageSize", g_config.candPageSize);
  if (g_config.candPageSize < 3)
    g_config.candPageSize = 3;
  if (g_config.candPageSize > 9)
    g_config.candPageSize = 9;

  const wchar_t* AP = L"Appearance";
  ReadStr(AP, L"FontFace", g_config.candFontFace, g_config.candFontFace,
          LF_FACESIZE);
  g_config.candFontSize = ReadInt(AP, L"FontSize", g_config.candFontSize);
  if (g_config.candFontSize < 8)
    g_config.candFontSize = 8;
  if (g_config.candFontSize > 48)
    g_config.candFontSize = 48;
  g_config.candFontWeight = ReadInt(AP, L"FontWeight", g_config.candFontWeight);
  g_config.candFontItalic =
      ReadBool(AP, L"FontItalic", g_config.candFontItalic);
  g_config.candBgColor = ReadColor(AP, L"BgColor", g_config.candBgColor);
  g_config.candTextColor = ReadColor(AP, L"TextColor", g_config.candTextColor);
  g_config.candSelBgColor =
      ReadColor(AP, L"SelBgColor", g_config.candSelBgColor);
  g_config.candSelTextColor =
      ReadColor(AP, L"SelTextColor", g_config.candSelTextColor);

  g_config.showStatusBar =
      ReadBool(L"StatusBar", L"Show", g_config.showStatusBar);
}

void SaveJiDaConfig() {
  if (GetJiDaConfigPath().empty())
    return;

  const wchar_t* in = L"Input";
  WriteBool(in, L"ZWildcard", g_config.zWildcard);
  WriteBool(in, L"ZRepeat", g_config.zRepeat);
  WriteBool(in, L"SemicolonSecond", g_config.semicolonSecond);
  WriteBool(in, L"PunctuationTop", g_config.punctuationTop);
  WriteBool(in, L"PerKeyHint", g_config.perKeyHint);
  WriteInt(in, L"CharacterSetVersion",
           static_cast<int>(g_config.characterSetVersion));
  WriteProfileString(in, L"GbkExt", nullptr, GetJiDaConfigPath().c_str());
  WriteBool(in, L"InlinePreedit", g_config.inlinePreedit);
  WriteBool(in, L"ShiftDirectCommit", g_config.shiftDirectCommit);
  WriteInt(in, L"ChineseToggleShortcut",
           static_cast<int>(g_config.chineseToggleShortcut));
  WriteKeyMappings(L"SoftKeyboard", g_config.softKeyboard);
  WriteKeyMappings(L"Punctuation", g_config.customPunctuation);
  const wchar_t* keyboardDisplay = L"KeyboardDisplay";
  WriteColor(keyboardDisplay, L"KeyLabelColor", g_config.keyboardKeyLabelColor);
  WriteColor(keyboardDisplay, L"NormalColor", g_config.keyboardNormalColor);
  WriteColor(keyboardDisplay, L"ShiftedColor", g_config.keyboardShiftedColor);

  const wchar_t* DP = L"Display";
  WriteInt(DP, L"Vertical",
           g_config.candLayout == CandLayout::Vertical ? 1 : 0);
  WriteInt(DP, L"PageSize", g_config.candPageSize);

  const wchar_t* AP = L"Appearance";
  WriteStr(AP, L"FontFace", g_config.candFontFace);
  WriteInt(AP, L"FontSize", g_config.candFontSize);
  WriteInt(AP, L"FontWeight", g_config.candFontWeight);
  WriteBool(AP, L"FontItalic", g_config.candFontItalic);
  WriteColor(AP, L"BgColor", g_config.candBgColor);
  WriteColor(AP, L"TextColor", g_config.candTextColor);
  WriteColor(AP, L"SelBgColor", g_config.candSelBgColor);
  WriteColor(AP, L"SelTextColor", g_config.candSelTextColor);

  WriteBool(L"StatusBar", L"Show", g_config.showStatusBar);
}