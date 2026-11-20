#pragma once
#include <msctf.h>
#include <windows.h>
#include <map>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

extern HINSTANCE g_hInstance;
extern const CLSID CLSID_JidaTSF;
extern const GUID GUID_LBI;

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

struct JidianConfig {
  bool autoCommitUnique = true;
  bool zRepeat = true;
  bool zWildcard = true;
  bool semicolonSecond = true;
  bool punctuationTop = true;
  bool perKeyHint = true;
  bool gbkExt = true;
  bool inlinePreedit = false;
  wchar_t wildcardChar = L'z';
};

extern std::vector<WubiEntry> g_entries;
extern std::vector<TrieNode> g_trie;
extern int g_trieRoot;

extern std::vector<std::pair<wchar_t, std::wstring>> g_punctMap;
extern JidianConfig g_config;

bool LoadJidaTable(const std::wstring& path);