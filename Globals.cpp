#include "Globals.h"
// 全局状态实现：码表加载、缓存读取、Trie 构建和用户词持久化。
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <cwctype>
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
    {L'?', L"？"}, {L'!', L"！"},  {L'@', L"·"},  {L'#', L"#"},
    {L'$', L"￥"}, {L'%', L"%"},  {L'^', L"……"}, {L'&', L"—"},
    {L'*', L"*"},  {L'(', L"（"}, {L')', L"）"}, {L'-', L"——"},
    {L'+', L"+"},  {L'_', L"——"}, {L'~', L"~"},
    {L'[', L"【"}, {L']', L"】"},  {L'{', L"「"}, {L'}', L"」"},
    {L'/', L"/"},  {L'\\', L"、"}, {L'\'', L"‘"}, {L'"', L"“"}};
JiDaConfig g_config;

const VirtualKeyDefinition g_virtualKeyboardKeys[] = {
    {'1', L"1", L'1', L'!'}, {'2', L"2", L'2', L'@'},
    {'3', L"3", L'3', L'#'}, {'4', L"4", L'4', L'$'},
    {'5', L"5", L'5', L'%'}, {'6', L"6", L'6', L'^'},
    {'7', L"7", L'7', L'&'}, {'8', L"8", L'8', L'*'},
    {'9', L"9", L'9', L'('}, {'0', L"0", L'0', L')'},
    {VK_OEM_MINUS, L"-", L'-', L'_'},
    {VK_OEM_PLUS, L"=", L'=', L'+'},
    {'Q', L"Q", L'q', L'Q'}, {'W', L"W", L'w', L'W'},
    {'E', L"E", L'e', L'E'}, {'R', L"R", L'r', L'R'},
    {'T', L"T", L't', L'T'}, {'Y', L"Y", L'y', L'Y'},
    {'U', L"U", L'u', L'U'}, {'I', L"I", L'i', L'I'},
    {'O', L"O", L'o', L'O'}, {'P', L"P", L'p', L'P'},
    {VK_OEM_4, L"[", L'[', L'{'}, {VK_OEM_6, L"]", L']', L'}'},
    {VK_OEM_5, L"\\", L'\\', L'|'},
    {'A', L"A", L'a', L'A'}, {'S', L"S", L's', L'S'},
    {'D', L"D", L'd', L'D'}, {'F', L"F", L'f', L'F'},
    {'G', L"G", L'g', L'G'}, {'H', L"H", L'h', L'H'},
    {'J', L"J", L'j', L'J'}, {'K', L"K", L'k', L'K'},
    {'L', L"L", L'l', L'L'}, {VK_OEM_1, L";", L';', L':'},
    {VK_OEM_7, L"'", L'\'', L'"'},
    {VK_OEM_3, L"`", L'`', L'~'},
    {'Z', L"Z", L'z', L'Z'}, {'X', L"X", L'x', L'X'},
    {'C', L"C", L'c', L'C'}, {'V', L"V", L'v', L'V'},
    {'B', L"B", L'b', L'B'}, {'N', L"N", L'n', L'N'},
    {'M', L"M", L'm', L'M'}, {VK_OEM_COMMA, L",", L',', L'<'},
    {VK_OEM_PERIOD, L".", L'.', L'>'},
    {VK_OEM_2, L"/", L'/', L'?'}};
const size_t g_virtualKeyboardKeyCount =
    sizeof(g_virtualKeyboardKeys) / sizeof(g_virtualKeyboardKeys[0]);

bool GetSoftKeyboardCharacter(UINT key, bool shifted, wchar_t& character) {
  // 从虚拟键盘配置中读取普通或 Shift 状态字符。
  auto found = g_config.softKeyboard.find(key);
  if (found == g_config.softKeyboard.end())
    return false;
  const std::wstring& value =
      shifted ? found->second.shifted : found->second.normal;
  if (value.empty())
    return false;
  character = value[0];
  return true;
}

std::wstring GetPunctuationCharacter(UINT key,
                                     bool shifted,
                                     wchar_t fallback) {
  // 优先使用按键自定义标点；未配置时回退到系统默认标点映射。
  auto configured = g_config.customPunctuation.find(key);
  if (configured != g_config.customPunctuation.end()) {
    const std::wstring& value =
        shifted ? configured->second.shifted : configured->second.normal;
    if (!value.empty())
      return value;
  }
  for (const auto& mapping : g_punctMap) {
    if (mapping.first == fallback)
      return mapping.second;
  }
  return std::wstring(1, fallback);
}

// ---------- 词库加载状态 ----------
static std::wstring s_tablePath;
static std::atomic<bool> s_tableLoaded{false};
static std::atomic<bool> s_tableLoading{false};
static constexpr uint32_t kDictionaryCacheVersion = 2;

static bool ReadCacheValue(FILE* fp, void* value, size_t size) {
  // 从缓存文件中读取指定字节数，并以读取成功作为有效性判断。
  return fread(value, 1, size, fp) == size;
}

static bool GetFileMetadata(const std::wstring& path,
                            uint64_t& size,
                            uint64_t& lastWrite) {
  // 获取源码词库的文件大小和最后写入时间，用于校验缓存是否过期。
  WIN32_FILE_ATTRIBUTE_DATA data;
  if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data))
    return false;
  size = (static_cast<uint64_t>(data.nFileSizeHigh) << 32) |
         data.nFileSizeLow;
  lastWrite = (static_cast<uint64_t>(data.ftLastWriteTime.dwHighDateTime) << 32) |
              data.ftLastWriteTime.dwLowDateTime;
  return true;
}

static void BuildTrie();

static void BuildCharacterFullCode() {
  // 为每个单字记录最短且字典序最小的全码，供造词逻辑查询。
  g_charFullCode.clear();
  for (auto& e : g_entries) {
    if (e.word.size() != 1)
      continue;
    auto it = g_charFullCode.find(e.word);
    if (it == g_charFullCode.end() || e.code.size() > it->second.size() ||
        (e.code.size() == it->second.size() && e.code < it->second))
      g_charFullCode[e.word] = e.code;
  }
}

static bool LoadJiDaTableCacheFile(const std::wstring& path) {
  // 校验缓存魔数、版本、源码尺寸与修改时间后加载二进制词库。
  uint64_t sourceSize = 0, sourceLastWrite = 0;
  if (!GetFileMetadata(path, sourceSize, sourceLastWrite))
    return false;

  FILE* fp = nullptr;
  if (_wfopen_s(&fp, (path + L".jdc").c_str(), L"rb") != 0 || !fp)
    return false;

  char magic[4] = {};
  uint32_t version = 0, entryCount = 0;
  uint64_t cachedSize = 0, cachedLastWrite = 0;
  uint16_t wildcardChar = 0;
  bool valid = ReadCacheValue(fp, magic, sizeof(magic)) &&
               ReadCacheValue(fp, &version, sizeof(version)) &&
               ReadCacheValue(fp, &cachedSize, sizeof(cachedSize)) &&
               ReadCacheValue(fp, &cachedLastWrite, sizeof(cachedLastWrite)) &&
               ReadCacheValue(fp, &wildcardChar, sizeof(wildcardChar)) &&
               ReadCacheValue(fp, &entryCount, sizeof(entryCount));
  if (!valid || memcmp(magic, "JDTC", sizeof(magic)) != 0 ||
      version != kDictionaryCacheVersion || cachedSize != sourceSize ||
      cachedLastWrite != sourceLastWrite || wildcardChar < L'a' ||
      wildcardChar > L'z' || entryCount > 2000000) {
    fclose(fp);
    return false;
  }

  std::vector<WubiEntry> entries;
  entries.reserve(entryCount);
  for (uint32_t i = 0; i < entryCount; ++i) {
    uint32_t codeLength = 0, wordLength = 0;
    uint8_t requiredCharacterSet = 0;
    if (!ReadCacheValue(fp, &codeLength, sizeof(codeLength)) ||
        !ReadCacheValue(fp, &wordLength, sizeof(wordLength)) ||
        !ReadCacheValue(fp, &requiredCharacterSet,
                        sizeof(requiredCharacterSet)) ||
        requiredCharacterSet >
            static_cast<uint8_t>(CharacterSetVersion::GB18030_2005) ||
        codeLength == 0 || codeLength > 4 || wordLength == 0 ||
        wordLength > 8192) {
      valid = false;
      break;
    }

    WubiEntry entry;
    entry.code.resize(codeLength);
    entry.word.resize(wordLength);
    if (!ReadCacheValue(fp, entry.code.data(),
                        codeLength * sizeof(wchar_t)) ||
        !ReadCacheValue(fp, entry.word.data(),
                        wordLength * sizeof(wchar_t))) {
      valid = false;
      break;
    }
    entry.level = static_cast<int>(codeLength) - 1;
    entry.requiredCharacterSet =
        static_cast<CharacterSetVersion>(requiredCharacterSet);
    entries.push_back(std::move(entry));
  }
  if (valid && fgetc(fp) != EOF)
    valid = false;
  fclose(fp);
  if (!valid || entries.empty())
    return false;

  g_config.wildcardChar = static_cast<wchar_t>(wildcardChar);
  g_entries = std::move(entries);
  BuildTrie();
  BuildCharacterFullCode();
  return true;
}

void SetJiDaTablePath(const std::wstring& path) {
  // 保存码表源文件路径，供同步和异步加载流程复用。
  s_tablePath = path;
}
bool IsJiDaTableLoaded() {
  // 查询码表是否已加载完成，线程读取使用原子状态。
  return s_tableLoaded.load();
}
bool IsJiDaTableLoading() {
  // 查询码表是否正在由后台线程加载。
  return s_tableLoading.load();
}

bool LoadJiDaTableCache() {
  // 在主线程中尝试加载二进制缓存；加载中或已加载时直接返回。
  if (s_tableLoaded.load())
    return true;
  bool expected = false;
  if (!s_tableLoading.compare_exchange_strong(expected, true))
    return false;

  bool ok = LoadJiDaTableCacheFile(s_tablePath);
  if (ok) {
    s_tableLoaded = true;
    OutputDebugStringW(L"[JiDa] Dictionary cache loaded synchronously\n");
  }
  s_tableLoading = false;
  return ok;
}

void LoadJiDaTableAsync() {
  // 后台加载码表，避免阻塞 TSF 激活和宿主界面。
  if (s_tableLoaded.load())
    return;
  bool expected = false;
  if (!s_tableLoading.compare_exchange_strong(expected, true))
    return;

  std::wstring path = s_tablePath;
  std::thread([path]() {
    bool ok = LoadJiDaTable(path);
    if (ok) {
      s_tableLoaded = true;
      OutputDebugStringW(L"[JiDa] Table loaded OK\n");
    } else {
      OutputDebugStringW(
          L"[JiDa] Table load FAILED, will retry on next Activate\n");
    }
    s_tableLoading = false;
  }).detach();
}

// ---------- 解析 ----------
static void StripEntryPrefix(std::wstring& s) {
  // 去除词条开头的特殊排序前缀字符。
  while (!s.empty()) {
    wchar_t c = s[0];
    if (c == L'~' || c == L'^' || c == L'!')
      s.erase(0, 1);
    else
      break;
  }
}
static void SplitTokens(const std::wstring& s, std::vector<std::wstring>& out) {
  // 按空格或制表符拆分一行文本中的编码和词条。
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
  // 解析极达词库的区段、通配符和制表符/空格分隔词条。
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
      CharacterSetVersion requiredCharacterSet =
          !word.empty() && word[0] == L'~' ? CharacterSetVersion::GBK
                                           : CharacterSetVersion::GB2312;
      StripEntryPrefix(word);
      if (word.empty())
        continue;
      entries.push_back(WubiEntry{code, word, (int)code.size() - 1,
                                  requiredCharacterSet});
    }
  }
}

static void ParseSimpleFormat(FILE* fp, std::vector<WubiEntry>& entries) {
  // 解析“编码<TAB>词条”格式的简易词库文本。
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
    entries.push_back(WubiEntry{code, word, (int)code.size() - 1,
                                CharacterSetVersion::GB2312});
  }
}

static void BuildTrie() {
  // 根据 g_entries 中的编码构建字典 Trie（前缀树）。
  //
  // 构建规则：
  //   1. Trie 的根节点下标固定为 g_trieRoot，本实现中始终为 0。
  //   2. 编码中的每个英文字母 a-z 对应 children 数组中的一个分支。
  //   3. 从根节点开始逐字符遍历；分支不存在时先创建新节点，再继续向下。
  //   4. 编码遍历结束后，将词条在 g_entries 中的下标保存到末端节点的
  //      ownEntries 中。这样既能快速定位精确编码，也能通过遍历子树查询前缀。
  //   5. 遇到非 a-z 字符的非法编码时跳过该词条，防止越界访问。
  //
  // 重建前清空旧 Trie。reserve 为根节点和预计节点预留容量，能减少
  // emplace_back 时的动态重分配次数；但实际节点数仍由词条编码决定。
  g_trie.clear();
  g_trie.reserve(g_entries.size() + 1);
  // 添加根节点；TrieNode 构造函数会把全部 26 个 children 初始化为 -1。
  g_trie.emplace_back();
  g_trieRoot = 0;

  // i 是当前词条在 g_entries 中的下标，它会同时作为 ownEntries 的值，
  // 查询候选时可通过 g_entries[i] 取回完整编码、词语和字符集等级。
  for (int i = 0; i < (int)g_entries.size(); ++i) {
    // 每个词条都从共享根节点重新开始，沿其编码逐级向下查找。
    int cur = g_trieRoot;
    // ok 用于记录当前编码是否全部合法且成功写入 Trie。
    bool ok = true;
    for (wchar_t ch : g_entries[i].code) {
      // 将小写字母映射为 children 数组下标：
      // a 对应 0，b 对应 1，...，z 对应 25。
      int ci = ch - L'a';
      if (ci < 0 || ci >= 26) {
        ok = false;
        break;
      }
      // 若当前节点还没有对应字母的分支，则创建一个 TrieNode，
      // 并把新节点下标写入父节点的 children[ci]。
      if (g_trie[cur].children[ci] == -1) {
        g_trie[cur].children[ci] = (int)g_trie.size();
        g_trie.emplace_back();
      }
      // 进入子节点，继续处理编码中的下一个字符。
      cur = g_trie[cur].children[ci];
    }
    // 编码合法时，表示该编码正好位于当前节点；将词条下标挂到该节点，
    // 同一编码的多个词条会依次追加到同一个 ownEntries 向量中。
    if (ok)
      g_trie[cur].ownEntries.push_back(i);
  }
}

bool LoadJiDaTable(const std::wstring& path) {
  // 打开文本词库，识别格式并解析为词条后构建 Trie 和单字全码。
  FILE* fp = nullptr;
  if (_wfopen_s(&fp, path.c_str(), L"r, ccs=UTF-8") != 0 || !fp) {
    OutputDebugStringW(L"[JiDa] LoadJiDaTable: cannot open file\n");
    return false;
  }
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

  g_entries = std::move(entries);
  BuildTrie();
  BuildCharacterFullCode();

  return !g_entries.empty();
}

// ---------- 用户词 ----------
void LoadUserPhrases() {
  // 从配置目录读取用户词表，保持编码和文本一一对应。
  g_userPhrases.clear();
  std::wstring configDir = GetJiDaConfigDir();
  if (configDir.empty())
    return;
  std::wstring path = configDir + L"\\userdict.txt";
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
  // 将内存中的全部用户词按“编码<TAB>文本”格式写入文件。
  std::wstring configDir = GetJiDaConfigDir();
  if (configDir.empty())
    return;
  std::wstring path = configDir + L"\\userdict.txt";
  FILE* fp = nullptr;
  errno_t openError = _wfopen_s(&fp, path.c_str(), L"w, ccs=UTF-8");
  if (openError != 0 || !fp) {
    OutputDebugStringW(
        L"[JiDa] Unable to save user dictionary in DLL conf directory\n");
    return;
  }
  fwprintf(fp, L"# JiDaTSF user dictionary\n");
  for (auto& up : g_userPhrases) {
    fwprintf(fp, L"%ls\t%ls\n", up.code.c_str(), up.text.c_str());
  }
  fclose(fp);
}

bool AddUserPhrase(const std::wstring& code, const std::wstring& text) {
  // 校验编码长度并去除重复项，然后追加到内存用户词表。
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