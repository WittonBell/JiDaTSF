#include "JidaEngine.h"
#include <algorithm>
#include <cstdio>
#include <cwctype>
#include <string_view>

void JidaEngine::Reset() {
  buffer_.clear();
  candidates_.clear();
  semicolonTail_.clear();
  waitDateOrAmount_ = false;
  pageStart_ = 0;
}
void JidaEngine::CommitText(const std::wstring& t) {
  if (!t.empty())
    lastCommitted_ = t;
}

// Trie 单步查找辅助
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

void JidaEngine::Query(const std::wstring& code) {
  candidates_.clear();
  pageStart_ = 0;
  if (code.empty())
    return;

  static constexpr int kMaxCandidates = 50;
  std::unordered_set<std::wstring_view> seen;

  // 1. 精确匹配 + 前缀匹配（Trie mergedEntries 已包含两者，短码优先）
  int cur = TrieWalk(code);
  if (cur >= 0) {
    for (int idx : g_trie[cur].mergedEntries) {
      auto& e = g_entries[idx];
      if (seen.insert(std::wstring_view(e.word)).second) {
        candidates_.push_back({e.word, e.code});
        if ((int)candidates_.size() >= kMaxCandidates)
          break;
      }
    }
  }

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
        for (int idx : g_trie[cur2].mergedEntries) {
          auto& e = g_entries[idx];
          if (seen.insert(std::wstring_view(e.word)).second) {
            candidates_.push_back({e.word, e.code});
            if ((int)candidates_.size() >= kMaxCandidates)
              break;
          }
        }
        if ((int)candidates_.size() >= kMaxCandidates)
          break;
      }
    }
  }
}

EngineAction JidaEngine::OnLetter(wchar_t ch,
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

  // 四码满：先顶出首选，再以新字母开新编码
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
  if (g_config.autoCommitUnique && IsFourCodeComplete() &&
      candidates_.size() == 1) {
    committed = candidates_.front().text;
    CommitText(committed);
    Reset();
    return EngineAction::Commit;
  }
  return EngineAction::UpdateCandidates;
}

bool JidaEngine::IsFourCodeComplete() const {
  if (buffer_.size() != 4 || waitDateOrAmount_)
    return false;
  return !candidates_.empty();
}

bool JidaEngine::OnDigit(int digit, std::wstring& committed) {
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

bool JidaEngine::OnSpace(std::wstring& committed) {
  if (candidates_.empty())
    return false;
  committed = candidates_.front().text;
  CommitText(committed);
  Reset();
  return true;
}

bool JidaEngine::OnSemicolon(std::wstring& committed,
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

EngineAction JidaEngine::OnSemicolonTail(wchar_t ch, std::wstring& committed) {
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

bool JidaEngine::OnPunctuation(wchar_t ch,
                               std::wstring& punct,
                               std::wstring& firstCand) {
  if (!g_config.punctuationTop || buffer_.empty())
    return false;
  for (auto& p : g_punctMap) {
    if (p.first == ch) {
      punct = p.second;
      if (!candidates_.empty())
        firstCand = candidates_.front().text;
      return true;
    }
  }
  return false;
}

void JidaEngine::OnBackspace() {
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

bool JidaEngine::OnEnter(std::wstring& committed) {
  // 极点的回车 = 取消当前输入，不上屏任何文字
  (void)committed;
  if (buffer_.empty() && !waitDateOrAmount_)
    return false;
  Reset();
  return false;  // 返回 false 表示"没有文字需要提交"
}