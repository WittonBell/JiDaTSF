#include "Globals.h"
#include <algorithm>
#include <cwchar>
#include <cwctype>

HINSTANCE g_hInstance = nullptr;

const CLSID CLSID_JiDaTSF = {0x7A3E8F1C,
                             0x4B2D,
                             0x4E5F,
                             {0x9C, 0x8A, 0x1B, 0x6D, 0x3E, 0x7F, 0x2A, 0x5C}};
const GUID GUID_JiDa_LBI = {0x2A5C3E7F,
                            0x1B6D,
                            0x3E4F,
                            {0x8A, 0x9C, 0x1C, 0x3E, 0x5F, 0x7A, 0x2B, 0x8C}};

std::vector<WubiEntry> g_entries;
std::vector<TrieNode> g_trie;
int g_trieRoot = 0;

std::vector<std::pair<wchar_t, std::wstring>> g_punctMap;
JiDaConfig g_config;

static void StripEntryPrefix(std::wstring& s) {
  while (!s.empty()) {
    wchar_t c = s[0];
    if (c == L'~' || c == L'^' || c == L'!')
      s.erase(0, 1);
    else
      break;
  }
}

static void SplitTokens(const std::wstring& s, std::vector<std::wstring>& out) {
  out.clear();
  std::wstring cur;
  for (wchar_t c : s) {
    if (c == L' ' || c == L'\t') {
      if (!cur.empty()) {
        out.push_back(cur);
        cur.clear();
      }
    } else
      cur += c;
  }
  if (!cur.empty())
    out.push_back(cur);
}

static void ParseJiDaFormat(FILE* fp, std::vector<WubiEntry>& entries) {
  wchar_t line[8192];
  bool inText = false, inDesc = false;
  while (fgetws(line, 8192, fp)) {
    std::wstring s = line;
    if (!s.empty() && s[0] == 0xFEFF)
      s.erase(0, 1);
    while (!s.empty() && (s.back() == L'\r' || s.back() == L'\n' ||
                          s.back() == L' ' || s.back() == L'\t'))
      s.pop_back();
    if (s.empty())
      continue;
    if (s.rfind(L"------", 0) == 0)
      continue;
    if (s.rfind(L"★★★", 0) == 0)
      continue;

    if (s == L"[Description]") {
      inDesc = true;
      inText = false;
      continue;
    }
    if (s == L"[rule]") {
      inDesc = false;
      inText = false;
      continue;
    }
    if (s == L"[Text]") {
      inDesc = false;
      inText = true;
      continue;
    }
    if (!s.empty() && s[0] == L'[') {
      inDesc = false;
      inText = false;
      continue;
    }

    if (inDesc) {
      if (s.rfind(L"WildChar=", 0) == 0 && s.size() > 9) {
        wchar_t wc = towlower(s[9]);
        if (wc >= L'a' && wc <= L'z')
          g_config.wildcardChar = wc;
      }
      continue;
    }
    if (!inText)
      continue;

    std::vector<std::wstring> tokens;
    SplitTokens(s, tokens);
    if (tokens.size() < 2)
      continue;

    std::wstring code = tokens[0];
    StripEntryPrefix(code);
    if (code.empty())
      continue;
    for (auto& ch : code)
      ch = towlower(ch);
    if (code.find_first_not_of(L"abcdefghijklmnopqrstuvwxyz") !=
        std::wstring::npos)
      continue;
    if (code.size() > 4)
      continue;

    for (size_t i = 1; i < tokens.size(); ++i) {
      std::wstring word = tokens[i];
      StripEntryPrefix(word);
      if (word.empty())
        continue;
      entries.push_back(WubiEntry{code, word, (int)code.size() - 1});
    }
  }
}

static void ParseSimpleFormat(FILE* fp, std::vector<WubiEntry>& entries) {
  wchar_t line[4096];
  while (fgetws(line, 4096, fp)) {
    std::wstring s = line;
    if (!s.empty() && s[0] == 0xFEFF)
      s.erase(0, 1);
    while (!s.empty() &&
           (s.back() == L'\r' || s.back() == L'\n' || s.back() == L' '))
      s.pop_back();
    if (s.empty() || s[0] == L'#')
      continue;
    auto tab = s.find(L'\t');
    if (tab == std::wstring::npos)
      continue;
    std::wstring code = s.substr(0, tab);
    std::wstring word = s.substr(tab + 1);
    if (word.empty() || code.size() > 4)
      continue;
    for (auto& ch : code)
      ch = towlower(ch);
    entries.push_back(WubiEntry{code, word, (int)code.size() - 1});
  }
}

static void BuildTrie() {
  g_trie.clear();
  g_trie.reserve(g_entries.size() * 2 + 1);
  g_trie.emplace_back();
  g_trieRoot = 0;

  for (int i = 0; i < (int)g_entries.size(); ++i) {
    int cur = g_trieRoot;
    bool ok = true;
    for (wchar_t ch : g_entries[i].code) {
      int ci = ch - L'a';
      if (ci < 0 || ci >= 26) {
        ok = false;
        break;
      }
      if (g_trie[cur].children[ci] == -1) {
        g_trie[cur].children[ci] = (int)g_trie.size();
        g_trie.emplace_back();
      }
      cur = g_trie[cur].children[ci];
    }
    if (ok)
      g_trie[cur].ownEntries.push_back(i);
  }

  struct Builder {
    std::vector<TrieNode>& t;
    void build(int n) {
      for (int c = 0; c < 26; ++c)
        if (t[n].children[c] >= 0)
          build(t[n].children[c]);
      auto& m = t[n].mergedEntries;
      m = t[n].ownEntries;
      for (int c = 0; c < 26; ++c) {
        int ch = t[n].children[c];
        if (ch >= 0) {
          const auto& cm = t[ch].mergedEntries;
          m.insert(m.end(), cm.begin(), cm.end());
        }
      }
    }
  };
  Builder{g_trie}.build(g_trieRoot);
}

bool LoadJiDaTable(const std::wstring& path) {
  FILE* fp = nullptr;
  if (_wfopen_s(&fp, path.c_str(), L"r, ccs=UTF-8") != 0 || !fp)
    return false;

  bool isJiDa = false;
  {
    wchar_t line[4096];
    while (fgetws(line, 4096, fp))
      if (wcsstr(line, L"[Text]")) {
        isJiDa = true;
        break;
      }
  }
  rewind(fp);

  std::vector<WubiEntry> entries;
  entries.reserve(150000);
  if (isJiDa)
    ParseJiDaFormat(fp, entries);
  else
    ParseSimpleFormat(fp, entries);
  fclose(fp);

  std::stable_sort(entries.begin(), entries.end(),
                   [](const WubiEntry& a, const WubiEntry& b) {
                     if (a.code.size() != b.code.size())
                       return a.code.size() < b.code.size();
                     return a.code < b.code;
                   });
  g_entries = std::move(entries);

  BuildTrie();

  g_punctMap = {{L',', L"，"}, {L'.', L"。"},  {L';', L"；"}, {L':', L"："},
                {L'?', L"？"}, {L'!', L"！"},  {L'(', L"（"}, {L')', L"）"},
                {L'[', L"【"}, {L']', L"】"},  {L'{', L"「"}, {L'}', L"」"},
                {L'/', L"/"},  {L'\\', L"、"}, {L'\'', L"‘"}, {L'"', L"“"}};
  return !g_entries.empty();
}