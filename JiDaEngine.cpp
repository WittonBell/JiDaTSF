#include "JiDaEngine.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cwctype>
#include <string_view>
#include "PhraseWindow.h"

void JiDaEngine::Reset() {
  buffer_.clear();
  candidates_.clear();
  semicolonTail_.clear();
  waitDateOrAmount_ = false;
  pageStart_ = 0;
  selected_ = 0;
  // 注意：不能清 recentCommits_，否则每次提交后历史都被清空
}

void JiDaEngine::CommitText(const std::wstring& t) {
  if (t.empty())
    return;
  lastCommitted_ = t;
  lastCommittedCode_ = buffer_;  // ← 关键：在 Reset 前保存当时编码
}

void JiDaEngine::ClampSelection() {
  if (candidates_.empty()) {
    selected_ = 0;
    return;
  }
  int pageEnd = (std::min)(pageStart_ + CandPageSize(), (int)candidates_.size());
  if (selected_ < pageStart_)
    selected_ = pageStart_;
  if (selected_ >= pageEnd)
    selected_ = pageEnd - 1;
}

static int TrieStep(int cur, wchar_t ch) {
  if (cur < 0)
    return -1;
  int ci = ch - L'a';
  if (ci < 0 || ci >= 26)
    return -1;
  return g_trie[cur].children[ci];
}
static int TrieWalk(const std::wstring& code) {
  int cur = g_trieRoot;
  for (wchar_t ch : code) {
    cur = TrieStep(cur, ch);
    if (cur < 0)
      break;
  }
  return cur;
}

void JiDaEngine::Query(const std::wstring& code) {
  candidates_.clear();
  pageStart_ = 0;
  selected_ = 0;
  if (code.empty())
    return;

  if (!IsJiDaTableLoaded())
    return;

  static std::atomic<bool> loggedOnce{false};
  bool expected = false;
  if (loggedOnce.compare_exchange_strong(expected, true)) {
    wchar_t dbg[128];
    swprintf_s(dbg, L"[JiDa] Query: entries=%zu trie=%zu\n", g_entries.size(),
               g_trie.size());
    OutputDebugStringW(dbg);
  }

  static constexpr int kMaxCandidates = 50;
  std::unordered_set<std::wstring_view> seen;

  // 0. 用户词优先
  for (auto& up : g_userPhrases) {
    if (up.code == code) {
      if (seen.insert(std::wstring_view(up.text)).second)
        candidates_.push_back({up.text, up.code});
    }
  }

  auto appendSubtree = [&](auto&& self, int node) -> bool {
    for (int idx : g_trie[node].ownEntries) {
      auto& entry = g_entries[idx];
      if (!g_config.gbkExt && entry.isGbkExt)
        continue;
      if (seen.insert(std::wstring_view(entry.word)).second) {
        candidates_.push_back({entry.word, entry.code});
        if ((int)candidates_.size() >= kMaxCandidates)
          return true;
      }
    }
    for (int child : g_trie[node].children) {
      if (child >= 0 && self(self, child))
        return true;
    }
    return false;
  };

  // 1. 精确匹配 + 前缀匹配；遍历顺序与旧的合并索引一致。
  int cur = TrieWalk(code);
  if (cur >= 0)
    appendSubtree(appendSubtree, cur);

  // 2. Z 通配：将 wildcardChar 替换为 a-y 逐一展开
  if (g_config.zWildcard) {
    wchar_t wc = g_config.wildcardChar;
    size_t zpos = code.find(wc);
    if (zpos != std::wstring::npos) {
      for (wchar_t c = L'a'; c <= L'y'; ++c) {
        if (c == wc)
          continue;
        std::wstring expanded = code;
        expanded[zpos] = c;
        int cur2 = TrieWalk(expanded);
        if (cur2 < 0)
          continue;
        appendSubtree(appendSubtree, cur2);
        if ((int)candidates_.size() >= kMaxCandidates)
          break;
      }
    }
  }
}

EngineAction JiDaEngine::OnLetter(wchar_t ch,
                                  std::wstring& committed,
                                  std::wstring& newBuffer) {
  ch = towlower(ch);
  if (ch < L'a' || ch > L'z')
    return EngineAction::None;

  // Z 重复
  if (ch == L'z' && buffer_.empty() && g_config.zRepeat &&
      !lastCommitted_.empty()) {
    committed = lastCommitted_;
    CommitText(committed);
    Reset();
    return EngineAction::Commit;
  }
  if (waitDateOrAmount_) {
    if (iswdigit(ch) || ch == L'.' || ch == L';') {
      semicolonTail_ += ch;
      return EngineAction::UpdateCandidates;
    }
    return EngineAction::None;
  }

  // 四码满：先上屏首选，再以新字母开新编码
  if (buffer_.size() == 4 && IsFourCodeComplete()) {
    std::wstring top = candidates_.front().text;
    committed = top;
    CommitText(top);
    buffer_ = std::wstring(1, ch);
    Query(buffer_);
    newBuffer = buffer_;
    return EngineAction::TopCommitAndContinue;
  }
  if (buffer_.size() >= 4)
    return EngineAction::None;

  buffer_ += ch;
  Query(buffer_);

  // 四码唯一自动上屏
  if (IsFourCodeComplete() && candidates_.size() == 1) {
    committed = candidates_.front().text;
    CommitText(committed);
    Reset();
    return EngineAction::Commit;
  }
  return EngineAction::UpdateCandidates;
}

bool JiDaEngine::IsFourCodeComplete() const {
  if (buffer_.size() != 4 || waitDateOrAmount_)
    return false;
  return !candidates_.empty();
}

bool JiDaEngine::OnDigit(int digit, std::wstring& committed) {
  if (digit < 1 || digit > 9)
    return false;
  int idx = pageStart_ + digit - 1;
  if (idx >= 0 && idx < (int)candidates_.size()) {
    committed = candidates_[idx].text;
    CommitText(committed);
    Reset();
    return true;
  }
  return false;
}

bool JiDaEngine::OnSpace(std::wstring& committed) {
  if (candidates_.empty())
    return false;
  int idx = selected_;
  if (idx < 0 || idx >= (int)candidates_.size())
    idx = 0;
  committed = candidates_[idx].text;
  CommitText(committed);
  Reset();
  return true;
}

void JiDaEngine::MoveSelection(int delta) {
  if (candidates_.empty())
    return;
  int pageEnd = (std::min)(pageStart_ + CandPageSize(), (int)candidates_.size());
  int next = selected_ + delta;
  if (next < pageStart_) {
    // 上一页最后一项
    if (pageStart_ > 0) {
      pageStart_ -= CandPageSize();
      if (pageStart_ < 0)
        pageStart_ = 0;
      pageEnd = (std::min)(pageStart_ + CandPageSize(), (int)candidates_.size());
      selected_ = pageEnd - 1;
    }
    return;
  }
  if (next >= pageEnd) {
    // 下一页第一项
    if (pageEnd < (int)candidates_.size()) {
      pageStart_ += CandPageSize();
      selected_ = pageStart_;
    }
    return;
  }
  selected_ = next;
}

void JiDaEngine::MovePage(int dir) {
  if (candidates_.empty())
    return;
  if (dir < 0) {
    if (pageStart_ >= CandPageSize())
      pageStart_ -= CandPageSize();
    else
      pageStart_ = 0;
  } else {
    if (pageStart_ + CandPageSize()< (int)candidates_.size())
      pageStart_ += CandPageSize();
  }
  selected_ = pageStart_;
  ClampSelection();
}

bool JiDaEngine::OnSemicolon(std::wstring& committed,
                             bool& waitForDateOrAmount) {
  waitForDateOrAmount = false;
  if (!buffer_.empty() && g_config.semicolonSecond) {
    if (candidates_.size() >= 2) {
      committed = candidates_[1].text;
      CommitText(committed);
      Reset();
      return true;
    }
    return false;
  }
  buffer_ = L";";
  semicolonTail_.clear();
  waitDateOrAmount_ = true;
  waitForDateOrAmount = true;
  return false;
}

EngineAction JiDaEngine::OnSemicolonTail(wchar_t ch, std::wstring& committed) {
  if (!waitDateOrAmount_)
    return EngineAction::None;
  if (ch == L';') {
    std::wstring tail = semicolonTail_;
    if (!tail.empty() && tail.find(L'.') == std::wstring::npos) {
      committed = L"￥" + tail;
      CommitText(committed);
      Reset();
      return EngineAction::Commit;
    }
    if (!tail.empty() && std::count(tail.begin(), tail.end(), L'.') == 2) {
      int y = 0, m = 0, d = 0;
      if (swscanf_s(tail.c_str(), L"%d.%d.%d", &y, &m, &d) == 3) {
        committed = std::to_wstring(y) + L"年" + std::to_wstring(m) + L"月" +
                    std::to_wstring(d) + L"日";
        CommitText(committed);
        Reset();
        return EngineAction::Commit;
      }
    }
    return EngineAction::None;
  }
  if (iswdigit(ch) || ch == L'.') {
    semicolonTail_ += ch;
    return EngineAction::UpdateCandidates;
  }
  return EngineAction::None;
}

bool JiDaEngine::OnPunctuation(wchar_t ch,
                               std::wstring& punct,
                               std::wstring& firstCand) {
  if (!g_config.punctuationTop || buffer_.empty())
    return false;
  for (auto& p : g_punctMap) {
    if (p.first == ch) {
      punct = p.second;
      if (!candidates_.empty()) {
        firstCand = candidates_.front().text;
        CommitText(firstCand);  // ← 关键：标点顶字路径也要存档
      }
      return true;
    }
  }
  return false;
}

void JiDaEngine::OnBackspace() {
  if (waitDateOrAmount_) {
    if (!semicolonTail_.empty())
      semicolonTail_.pop_back();
    else {
      waitDateOrAmount_ = false;
      buffer_.clear();
    }
    return;
  }
  if (!buffer_.empty())
    buffer_.pop_back();
  Query(buffer_);
}

bool JiDaEngine::OnEnter(std::wstring& committed) {
  (void)committed;
  if (buffer_.empty() && !waitDateOrAmount_)
    return false;
  Reset();
  return false;
}

void JiDaEngine::OnCommitted(const std::wstring& code,
                             const std::wstring& text) {
  if (code.empty() || text.empty())
    return;
  for (wchar_t c : code)
    if (c < L'a' || c > L'z')
      return;
  for (wchar_t c : text) {
    if (c < 0x4E00 || c > 0x9FFF)  { // 只记录汉字
      return;
    }
  }

  // 连续单字上屏时去重；词组上屏则逐字加入历史。
  if (text.size() == 1 && !recentCommits_.empty() &&
      recentCommits_.back().text == text)
    return;

  for (wchar_t c : text) {
    std::wstring character(1, c);
    std::wstring fullCode;
    auto it = g_charFullCode.find(character);
    if (it != g_charFullCode.end())
      fullCode = it->second;
    else
      fullCode = code;

    recentCommits_.push_back({code, fullCode, character});
    if (recentCommits_.size() > 20)
      recentCommits_.erase(recentCommits_.begin());
  }
}
bool JiDaEngine::CreateUserPhrase(std::wstring& outCode,
                                  std::wstring& outText) {
  if (recentCommits_.size() < 2)
    return false;
  outCode.clear();
  outText.clear();
  for (auto& r : recentCommits_) {
    outCode += r.code;
    outText += r.text;
  }
  recentCommits_.clear();
  if (outCode.size() > 4)
    outCode.resize(4);
  return !outCode.empty() && !outText.empty();
}
