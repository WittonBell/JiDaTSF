#include "Config.h"
#include <shlobj.h>
#include "Globals.h"
#pragma comment(lib, "shell32.lib")

std::wstring GetJiDaConfigDir() {
  PWSTR path = nullptr;
  std::wstring dir;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_CREATE,
                                     nullptr, &path))) {
    dir = path;
    CoTaskMemFree(path);
  } else {
    wchar_t buf[MAX_PATH] = {0};
    GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
    dir = buf;
  }
  dir += L"\\JiDaTSF";
  CreateDirectoryW(dir.c_str(), nullptr);
  return dir;
}

std::wstring GetJiDaConfigPath() {
  static std::wstring cached;
  if (cached.empty())
    cached = GetJiDaConfigDir() + L"\\config.ini";
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
  WritePrivateProfileStringW(sec, key, buf, GetJiDaConfigPath().c_str());
}
static void WriteBool(const wchar_t* sec, const wchar_t* key, bool v) {
  WritePrivateProfileStringW(sec, key, v ? L"1" : L"0",
                             GetJiDaConfigPath().c_str());
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
  WritePrivateProfileStringW(sec, key, v, GetJiDaConfigPath().c_str());
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
  if (GetFileAttributesW(GetJiDaConfigPath().c_str()) ==
      INVALID_FILE_ATTRIBUTES)
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
  const wchar_t* in = L"Input";
  WriteBool(in, L"ZWildcard", g_config.zWildcard);
  WriteBool(in, L"ZRepeat", g_config.zRepeat);
  WriteBool(in, L"SemicolonSecond", g_config.semicolonSecond);
  WriteBool(in, L"PunctuationTop", g_config.punctuationTop);
  WriteBool(in, L"PerKeyHint", g_config.perKeyHint);
  WriteInt(in, L"CharacterSetVersion",
           static_cast<int>(g_config.characterSetVersion));
  WritePrivateProfileStringW(in, L"GbkExt", nullptr,
                             GetJiDaConfigPath().c_str());
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