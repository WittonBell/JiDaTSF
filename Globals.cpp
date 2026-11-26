#include "Globals.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <iterator>
#include <mutex>
#include <system_error>
#include <thread>
#include "Config.h"

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

std::vector<UserPhrase> g_userPhrases;
std::unordered_map<std::wstring, std::wstring> g_charFullCode;
std::vector<std::pair<wchar_t, std::wstring>> g_punctMap = {
    {L',', L"，"}, {L'.', L"。"},  {L';', L"；"}, {L':', L"："},
    {L'?', L"？"}, {L'!', L"！"},  {L'(', L"（"}, {L')', L"）"},
    {L'[', L"【"}, {L']', L"】"},  {L'{', L"「"}, {L'}', L"」"},
    {L'/', L"/"},  {L'\\', L"、"}, {L'\'', L"‘"}, {L'"', L"“"}};
JiDaConfig g_config;

// ---------- 词库加载状态 ----------
static std::wstring s_tablePath;
static std::atomic<bool> s_tableLoaded{false};
static std::atomic<bool> s_tableLoading{false};
static std::mutex s_tableLoadMutex;
static constexpr size_t kMbPhraseOffset = 0x1B620;

static bool ReadMbDictionary(const std::wstring& path,
                             std::vector<WubiEntry>& entries) {
  FILE* fp = nullptr;
  if (_wfopen_s(&fp, path.c_str(), L"rb") != 0 || !fp) {
    OutputDebugStringW(L"[JiDa] MB dictionary: cannot open file\n");
    return false;
  }

  if (fseek(fp, 0, SEEK_END) != 0) {
    fclose(fp);
    return false;
  }
  long fileSize = ftell(fp);
  if (fileSize < 0 || fseek(fp, 0, SEEK_SET) != 0) {
    fclose(fp);
    return false;
  }
  std::vector<unsigned char> data(static_cast<size_t>(fileSize));
  bool readOk =
      !data.empty() && fread(data.data(), 1, data.size(), fp) == data.size();
  fclose(fp);
  if (!readOk || data.size() < kMbPhraseOffset + 3 ||
      memcmp(data.data(), "Freeime Dictionary", 18) != 0) {
    OutputDebugStringW(L"[JiDa] MB dictionary: invalid header or file size\n");
    return false;
  }

  size_t position = kMbPhraseOffset;
  while (position < data.size()) {
    if (data.size() - position < 3)
      break;

    uint8_t codeLength = data[position];
    if (codeLength == 0xFF) {
      if (data.size() - position < 2)
        break;
      // The binary table interleaves two-byte index markers with records.
      position += 2;
      continue;
    }
    if (codeLength == 0)
      break;

    uint8_t wordByteLength = data[position + 1];
    uint8_t flags = data[position + 2];
    size_t recordSize = 3 + static_cast<size_t>(codeLength) + wordByteLength;
    if (wordByteLength == 0 || (wordByteLength & 1) != 0 ||
        recordSize > data.size() - position) {
      OutputDebugStringW(L"[JiDa] MB dictionary: malformed phrase record\n");
      return false;
    }

    const unsigned char* codeBytes = data.data() + position + 3;
    bool validWubiCode = codeLength <= 4;
    std::wstring code;
    if (validWubiCode) {
      code.reserve(codeLength);
      for (uint8_t i = 0; i < codeLength; ++i) {
        unsigned char ch = codeBytes[i];
        if (ch >= 'A' && ch <= 'Z')
          ch = static_cast<unsigned char>(ch - 'A' + 'a');
        if (ch < 'a' || ch > 'z') {
          validWubiCode = false;
          break;
        }
        code.push_back(static_cast<wchar_t>(ch));
      }
    }

    if (validWubiCode) {
      std::wstring word;
      word.reserve(wordByteLength / 2);
      const unsigned char* wordBytes = codeBytes + codeLength;
      for (uint8_t i = 0; i < wordByteLength; i += 2) {
        uint16_t ch = static_cast<uint16_t>(wordBytes[i]) |
                      (static_cast<uint16_t>(wordBytes[i + 1]) << 8);
        word.push_back(static_cast<wchar_t>(ch));
      }

      if (flags == 0x32) {
        // Variant records store a simplified/traditional pair as "A(B)".
        size_t variantStart = word.find(L'(');
        if (variantStart != std::wstring::npos)
          word.resize(variantStart);
      }
      if (!word.empty()) {
        entries.push_back(
            WubiEntry{std::move(code), std::move(word), codeLength - 1});
      }
    }
    position += recordSize;
  }
  return true;
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
}

static void BuildCharacterFullCode() {
  g_charFullCode.clear();
  for (auto& entry : g_entries) {
    if (entry.word.size() != 1)
      continue;
    auto it = g_charFullCode.find(entry.word);
    if (it == g_charFullCode.end() || entry.code.size() > it->second.size())
      g_charFullCode[entry.word] = entry.code;
  }
}

void SetJiDaTablePath(const std::wstring& path) {
  s_tablePath = path;
}

bool IsJiDaTableLoaded() {
  return s_tableLoaded.load();
}

bool IsJiDaTableLoading() {
  return s_tableLoading.load();
}

bool LoadJiDaTable() {
  std::lock_guard<std::mutex> lock(s_tableLoadMutex);
  if (s_tableLoaded.load())
    return true;

  std::vector<WubiEntry> entries;
  if (!ReadMbDictionary(s_tablePath, entries)) {
    OutputDebugStringW(L"[JiDa] Failed to load primary MB dictionary\n");
    return false;
  }
  if (entries.empty()) {
    OutputDebugStringW(
        L"[JiDa] Primary MB dictionary contains no supported Wubi entries\n");
    return false;
  }

  std::wstring attachPath = s_tablePath;
  size_t separator = attachPath.find_last_of(L"\\/");
  if (separator != std::wstring::npos)
    attachPath.resize(separator + 1);
  else
    attachPath.clear();
  attachPath += L"attach.mb";

  DWORD attachAttributes = GetFileAttributesW(attachPath.c_str());
  if (attachAttributes != INVALID_FILE_ATTRIBUTES &&
      !(attachAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
    std::vector<WubiEntry> attachEntries;
    if (ReadMbDictionary(attachPath, attachEntries)) {
      entries.insert(entries.end(),
                     std::make_move_iterator(attachEntries.begin()),
                     std::make_move_iterator(attachEntries.end()));
    } else {
      OutputDebugStringW(L"[JiDa] Optional attach.mb could not be loaded\n");
    }
  }

  g_entries = std::move(entries);
  BuildTrie();
  BuildCharacterFullCode();

  bool loaded = !g_entries.empty();
  s_tableLoaded = loaded;
  wchar_t message[128];
  swprintf_s(message, L"[JiDa] MB dictionary loaded: %zu entries\n",
             g_entries.size());
  OutputDebugStringW(message);
  return loaded;
}

void LoadJiDaTableAsync() {
  if (s_tableLoaded.load())
    return;

  bool expected = false;
  if (!s_tableLoading.compare_exchange_strong(expected, true))
    return;

  try {
    std::thread([]() {
      bool loaded = LoadJiDaTable();
      if (!loaded)
        OutputDebugStringW(L"[JiDa] Async MB dictionary load failed\n");
      s_tableLoading = false;
    }).detach();
  } catch (const std::system_error&) {
    s_tableLoading = false;
    OutputDebugStringW(L"[JiDa] Failed to start MB dictionary worker\n");
  }
}

// ---------- 用户词 ----------
void LoadUserPhrases() {
  g_userPhrases.clear();
  std::wstring path = GetJiDaConfigDir() + L"\\userdict.txt";
  FILE* fp = nullptr;
  if (_wfopen_s(&fp, path.c_str(), L"r, ccs=UTF-8") != 0 || !fp)
    return;
  wchar_t line[1024];
  while (fgetws(line, 1024, fp)) {
    std::wstring s = line;
    if (!s.empty() && s[0] == 0xFEFF)
      s.erase(0, 1);
    while (!s.empty() && (s.back() == L'\r' || s.back() == L'\n'))
      s.pop_back();
    if (s.empty() || s[0] == L'#')
      continue;
    auto tab = s.find(L'\t');
    if (tab == std::wstring::npos)
      continue;
    UserPhrase up;
    up.code = s.substr(0, tab);
    up.text = s.substr(tab + 1);
    if (!up.code.empty() && !up.text.empty())
      g_userPhrases.push_back(up);
  }
  fclose(fp);
}

void SaveUserPhrases() {
  std::wstring path = GetJiDaConfigDir() + L"\\userdict.txt";
  FILE* fp = nullptr;
  if (_wfopen_s(&fp, path.c_str(), L"w, ccs=UTF-8") != 0 || !fp)
    return;
  fwprintf(fp, L"# JiDaTSF user dictionary\n");
  for (auto& up : g_userPhrases) {
    fwprintf(fp, L"%ls\t%ls\n", up.code.c_str(), up.text.c_str());
  }
  fclose(fp);
}

bool AddUserPhrase(const std::wstring& code, const std::wstring& text) {
  if (code.empty() || text.empty())
    return false;
  if (code.size() > 4)
    return false;
  for (auto& up : g_userPhrases)
    if (up.code == code && up.text == text)
      return false;
  g_userPhrases.push_back({code, text});
  return true;
}