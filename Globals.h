#pragma once
#include <msctf.h>
#include <windows.h>
#include <map>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

using namespace std;

extern HINSTANCE g_hInstance;
extern const CLSID CLSID_JiDaTSF;
extern const GUID GUID_JiDa_LBI;

struct WubiEntry {
  std::wstring code;
  std::wstring word;
  int level;
};

struct TrieNode {
  int children[26];
  std::vector<int> ownEntries;     // code 完全等于当前路径的词条
  std::vector<int> mergedEntries;  // 以当前路径为前缀的全部词条（短码优先）

  TrieNode() { memset(children, -1, sizeof(children)); }
};

enum class CandLayout { Vertical, Horizontal };

struct JiDaConfig {
  bool autoCommitUnique = true;                    // 四码唯一自动上屏
  bool zRepeat = true;                             // Z 重复上次上屏
  bool zWildcard = true;                           // Z 通配
  bool semicolonSecond = true;                     // 分号选第二重码
  bool punctuationTop = true;                      // 标点顶屏
  bool perKeyHint = true;                          // 逐键提示
  bool gbkExt = true;                              // GBK 生僻字
  wchar_t wildcardChar = L'z';                     // 通配符字符
  bool inlinePreedit = false;                      // 嵌入预编辑
  bool showStatusBar = true;                       // 是否显示状态栏
  int candPageSize = 9;                            // 候选框中每页的条目数
  int candFontSize = 14;                           // 候选框中的字体大小
  CandLayout candLayout = CandLayout::Horizontal;  // 候选框选项排列方式
};

extern std::vector<WubiEntry> g_entries;
extern std::vector<TrieNode> g_trie;
extern int g_trieRoot;

extern std::vector<std::pair<wchar_t, wstring>> g_punctMap;
extern JiDaConfig g_config;

bool LoadJiDaTable(const wstring& path);

// 便利：候选页大小，所有引用 CandPageSize()的地方统一改成这个函数
inline int CandPageSize() {
  return g_config.candPageSize;
}
