#pragma once
// 全局配置、码表、Trie 索引、用户词和虚拟键盘共享定义。
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

// 候选框排列方式：纵向或横向。
enum class CandLayout { Vertical, Horizontal };
// 五笔词条允许使用的字符集等级，数值越大覆盖范围越广。
enum class CharacterSetVersion : int {
  GB2312 = 0,
  GBK = 1,
  GB18030_2000 = 2,
  GB18030_2005 = 3
};

// 中英文切换快捷键类型。
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
  std::wstring normal;   // 未按 Shift 时输出的字符。
  std::wstring shifted;  // 按下 Shift 时输出的字符。
};

struct VirtualKeyDefinition {
  UINT key;
  const wchar_t* label;
  wchar_t normal;
  wchar_t shifted;
};

struct WubiEntry {
  std::wstring code;       // 五笔编码。
  std::wstring word;       // 对应词或汉字。
  int level;               // 词条排序权重。
  CharacterSetVersion requiredCharacterSet = CharacterSetVersion::GB2312;
};

struct TrieNode {
  int children[26];          // a-z 分支节点下标。
  std::vector<int> ownEntries; // 当前编码节点直接拥有的词条下标。
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
  bool perKeyHint = true; // 在候选后显示尚未输入的编码后缀。
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