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

struct WubiEntry {
  std::wstring code;
  std::wstring word;
  int level;
  bool isGbkExt = false;
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

enum class CandLayout { Vertical, Horizontal };

struct JiDaConfig {
  bool zRepeat = true;
  bool zWildcard = true;
  bool semicolonSecond = true;
  bool punctuationTop = true;
  bool perKeyHint = true;
  bool gbkExt = true;
  bool inlinePreedit = false;
  bool showStatusBar = true;
  // Shift 直接上屏
  bool shiftDirectCommit = false;

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