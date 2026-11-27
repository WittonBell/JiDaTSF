#pragma once
#include <msctf.h>
#include <windows.h>
#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

extern HINSTANCE g_hInstance;
extern const CLSID CLSID_JiDaTSF;
extern const GUID GUID_JiDa_LBI;

enum class CandLayout { Vertical, Horizontal };
enum class CharacterSetVersion : int {
  GB2312 = 0,
  GBK = 1,
  GB18030_2000 = 2,
  GB18030_2005 = 3
};

enum class ChineseToggleShortcut : int {
  Disabled = 0,
  LeftShift,
  RightShift,
  Shift,
  LeftControl,
  RightControl,
  Control
};

struct KeyCharacterPair {
  std::wstring normal;
  std::wstring shifted;
};

struct VirtualKeyDefinition {
  UINT key;
  const wchar_t* label;
  wchar_t normal;
  wchar_t shifted;
};

struct WubiEntry {
  std::wstring code;
  std::wstring word;
  int level;
  CharacterSetVersion requiredCharacterSet = CharacterSetVersion::GB2312;
};

struct TrieNode {
  int children[26];
  std::vector<int> ownEntries;
  TrieNode() { memset(children, -1, sizeof(children)); }
};

struct UserPhrase {
  std::wstring code;
  std::wstring text;
};

struct JiDaConfig {
  bool zRepeat = true;
  bool zWildcard = true;
  bool semicolonSecond = true;
  bool punctuationTop = true;
  bool perKeyHint = true;
  CharacterSetVersion characterSetVersion = CharacterSetVersion::GBK;
  bool inlinePreedit = false;
  bool showStatusBar = true;
  // Shift 直接上屏
  bool shiftDirectCommit = false;
  ChineseToggleShortcut chineseToggleShortcut =
      ChineseToggleShortcut::RightShift;
  std::map<UINT, KeyCharacterPair> softKeyboard;
  std::map<UINT, KeyCharacterPair> customPunctuation;
  COLORREF keyboardKeyLabelColor = RGB(128, 128, 128);
  COLORREF keyboardNormalColor = RGB(0, 0, 0);
  COLORREF keyboardShiftedColor = RGB(220, 0, 0);

  CandLayout candLayout = CandLayout::Horizontal;
  int candPageSize = 9;

  // 候选框外观
  wchar_t candFontFace[LF_FACESIZE] = L"Microsoft YaHei";
  int candFontSize = 14;
  int candFontWeight = FW_NORMAL;
  bool candFontItalic = false;
  COLORREF candBgColor = RGB(255, 255, 255);
  COLORREF candTextColor = RGB(0, 0, 0);
  COLORREF candSelBgColor = RGB(251, 255, 247);
  COLORREF candSelTextColor = RGB(255, 255, 255);

  wchar_t wildcardChar = L'z';
};

extern std::vector<WubiEntry> g_entries;
extern std::vector<TrieNode> g_trie;
extern int g_trieRoot;

extern std::vector<UserPhrase> g_userPhrases;
extern std::unordered_map<std::wstring, std::wstring> g_charFullCode;

extern std::vector<std::pair<wchar_t, std::wstring>> g_punctMap;
extern JiDaConfig g_config;
extern const VirtualKeyDefinition g_virtualKeyboardKeys[];
extern const size_t g_virtualKeyboardKeyCount;

bool GetSoftKeyboardCharacter(UINT key, bool shifted, wchar_t& character);
std::wstring GetPunctuationCharacter(UINT key,
                                     bool shifted,
                                     wchar_t fallback);

inline int CandPageSize() {
  return g_config.candPageSize;
}

// 码表：路径 / 加载
void SetJiDaTablePath(const std::wstring& path);
bool IsJiDaTableLoaded();
bool IsJiDaTableLoading();
bool LoadJiDaTableCache();
void LoadJiDaTableAsync();
bool LoadJiDaTable(const std::wstring& path);  // 同步加载

// 用户词
void LoadUserPhrases();
void SaveUserPhrases();
bool AddUserPhrase(const std::wstring& code, const std::wstring& text);