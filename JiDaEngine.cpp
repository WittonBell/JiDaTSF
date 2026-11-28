#include "JiDaEngine.h"
// 输入引擎实现：包含 Trie 查询、候选选择、分号日期金额和造词逻辑。
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cwctype>
#include <string_view>
#include "PhraseWindow.h"

void JiDaEngine::Reset() {
  // 清空当前编码状态，但保留最近提交记录供 Z 重码和造词使用。
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
  // 根据当前输入编码重新生成候选列表，并重置候选页与高亮状态。
  //
  // 查询流程及优先级：
  //   1. 清空旧候选，恢复到第一页并取消原来的高亮选择。
  //   2. 若编码为空或全局词库尚未加载，则不产生候选并直接返回。
  //   3. 把编码完全相同的用户词放到候选列表最前面。
  //   4. 沿 Trie 查询精确编码及其所有后续前缀候选。
  //   5. 若启用了 Z 通配，则依次展开通配字符并补充候选。
  //   6. 使用 seen 按词文本去重，最多保留 kMaxCandidates 个候选。
  //
  // candidates_.clear 不会清除当前缓冲区或提交历史，只表示需要根据新的
  // code 重新计算候选；pageStart_ 和 selected_ 必须同步归零，避免新结果沿用
  // 旧候选页或越界的高亮索引。
  candidates_.clear();
  pageStart_ = 0;
  selected_ = 0;
  // 无输入编码时没有可查询结果，提前返回可以避免访问空 Trie。
  if (code.empty())
    return;

  // 词库可能由后台线程异步加载；未加载完成时候选列表保持为空。
  if (!IsJiDaTableLoaded())
    return;

  // 每个进程只记录一次词库规模日志，避免候选查询频繁触发大量调试输出。
  static std::atomic<bool> loggedOnce{false};
  bool expected = false;
  if (loggedOnce.compare_exchange_strong(expected, true)) {
    wchar_t dbg[128];
    swprintf_s(dbg, L"[JiDa] Query: entries=%zu trie=%zu\n", g_entries.size(),
               g_trie.size());
    OutputDebugStringW(dbg);
  }

  // 限制候选数量可以避免单个短编码展开出过多词条，占用界面和更新成本。
  static constexpr int kMaxCandidates = 50;
  // 已显示词文本集合按词去重，而不是按编码去重：同一个词可能存在于多个编码，
  // 此时只保留一份候选，防止用户词、精确匹配和通配匹配重复出现。
  std::unordered_set<std::wstring_view> seen;

  // 阶段 0：查询用户词。
  // 用户词只匹配完整编码（up.code == code），不会参与前缀或 Z 通配扩展；
  // 命中的用户词会最先进入候选列表，优先级高于词库候选。
  for (auto& up : g_userPhrases) {
    if (up.code == code) {
      // seen 确保用户词和后续词库候选文本相同时不会重复显示。
      if (seen.insert(std::wstring_view(up.text)).second)
        candidates_.push_back({up.text, up.code});
    }
  }

  // Lambda：递归遍历从 node 开始的 Trie 子树。
  // node 表示一个完整编码的前缀节点；ownEntries 保存该节点直接对应的词条，
  // children 列表保存继续追加字母后形成的子节点。
  //
  // 遍历顺序为：先输出当前节点 ownEntries，再按 children 的 a-z 顺序递归。
  // 这一顺序与词库原有合并索引的候选顺序尽量保持一致。
  // 返回 true 表示候选已达到上限，调用方应停止继续查询。
  auto appendSubtree = [&](auto&& self, int node) -> bool {
    // 先输出以当前节点编码结尾的精确词条。
    for (int idx : g_trie[node].ownEntries) {
      auto& entry = g_entries[idx];
      // 用户配置的字符集等级低于词条要求时，此词条不允许显示。
      // 例如配置为 GB2312 时，GBK 或更高字符集的词条会被过滤。
      if (static_cast<int>(g_config.characterSetVersion) <
          static_cast<int>(entry.requiredCharacterSet))
        continue;
      // 文本首次出现时才加入候选；即使候选满了，也只在确实新增候选时计数。
      if (seen.insert(std::wstring_view(entry.word)).second) {
        candidates_.push_back({entry.word, entry.code});
        // 达到最大候选数后立即返回 true，由所有递归层逐级停止。
        if ((int)candidates_.size() >= kMaxCandidates)
          return true;
      }
    }
    // 按 a-z 顺序遍历子树，将以当前编码为前缀的词条追加为候选。
    for (int child : g_trie[node].children) {
      if (child >= 0 && self(self, child))
        return true;
    }
    return false;
  };

  // 阶段 1：普通 Trie 精确匹配与前缀匹配。
  // TrieWalk 从根节点逐字符进入，定位到与 code 完全对应的节点。
  // 从此节点遍历子树可以同时取得精确编码及其所有更长编码的候选。
  int cur = TrieWalk(code);
  if (cur >= 0)
    appendSubtree(appendSubtree, cur);

  // 阶段 2：Z 通配匹配。
  // g_config.zWildcard 启用时，用户编码中按 wildcardChar 配置出现的位置
  // 会被视为任意字母占位符，并沿用普通查询的前缀扩展方式。
  if (g_config.zWildcard) {
    wchar_t wc = g_config.wildcardChar;
    size_t zpos = code.find(wc);
    // 编码中只有至少一个通配字符时才展开。
    if (zpos != std::wstring::npos) {
      // 枚举 a 到 y 共 25 个实际字母，但不枚举通配字符本身。
      // 若配置的通配字符是 z，则共尝试 a-y；若配置为别的字符，
      // 该循环仍然是把一个通配位置替换为除通配字符外的字母。
      for (wchar_t c = L'a'; c <= L'y'; ++c) {
        if (c == wc)
          continue;
        // 每次复制原编码，仅替换第一个匹配到的通配位置。
        // 目前实现只展开 code 中从左到右找到的第一个通配符。
        std::wstring expanded = code;
        expanded[zpos] = c;
        int cur2 = TrieWalk(expanded);
        // 展开的编码在 Trie 中不存在时跳过，不产生候选。
        if (cur2 < 0)
          continue;
        // 遍历展开编码对应节点的完整子树，并继续执行字符集过滤及去重。
        appendSubtree(appendSubtree, cur2);
        // 普通查询或前面通配展开已经达到上限时，不再尝试更多字母。
        if ((int)candidates_.size() >= kMaxCandidates)
          break;
      }
    }
  }
}

EngineAction JiDaEngine::OnLetter(wchar_t ch,
                                  std::wstring& committed,
                                  std::wstring& newBuffer) {
  // 转换并校验英文字母，处理 Z 重码、四码上屏及唯一候选自动提交。
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
  // 数字键对应当前页内候选的绝对索引。
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
  // 空格提交高亮候选；若选中状态异常则回退到首项。
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
  // 上下移动候选高亮，并在越过页边界时切换候选页。
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
  // 分号后继续收集日期或金额输入，遇到结束键后提交附加文本。
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
                               std::wstring& firstCand,
                               const std::wstring& customPunctuation) {
  if (!g_config.punctuationTop || buffer_.empty())
    return false;
  if (!customPunctuation.empty()) {
    punct = customPunctuation;
    if (!candidates_.empty()) {
      firstCand = candidates_.front().text;
      CommitText(firstCand);
    }
    return true;
  }
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
