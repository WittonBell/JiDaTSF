#pragma once
#include "Globals.h"
#include "StatusWindow.h"

struct CandidateItem {
  std::wstring text;
  std::wstring code;
};

enum class EngineAction {
  None,
  UpdateCandidates,
  Commit,
  TopCommitAndContinue
};

class JiDaEngine {
 public:
  void Reset();

  EngineAction OnLetter(wchar_t ch,
                        std::wstring& committed,
                        std::wstring& newBuffer);
  bool OnDigit(int digit, std::wstring& committed);
  bool OnSpace(std::wstring& committed);  // 提交高亮项
  bool OnSemicolon(std::wstring& committed, bool& waitForDateOrAmount);
  bool OnPunctuation(wchar_t ch, std::wstring& punct, std::wstring& firstCand);
  void OnBackspace();
  EngineAction OnSemicolonTail(wchar_t ch, std::wstring& committed);
  bool OnEnter(std::wstring& committed);  // 取消当前输入

  // 上下/左右键导航
  void MoveSelection(int delta);
  void MovePage(int dir);

  const std::wstring& Buffer() const { return buffer_; }
  const std::vector<CandidateItem>& Candidates() const { return candidates_; }
  int SelectedIndex() const { return selected_; }
  int PageStart() const { return pageStart_; }
  bool IsFourCodeComplete() const;
  bool IsWaitingDateOrAmount() const { return waitDateOrAmount_; }
  void RefreshCandidates() { Query(buffer_); }
  void SetStatus(InputStatus status) { _inputStatus = status; }

 private:
  void Query(const std::wstring& code);
  void CommitText(const std::wstring& t);
  void ClampSelection();

  std::wstring buffer_;
  std::vector<CandidateItem> candidates_;
  std::wstring lastCommitted_;
  std::wstring semicolonTail_;
  bool waitDateOrAmount_ = false;
  int pageStart_ = 0;
  int selected_ = 0;

  InputStatus _inputStatus;

 public:
  // 提交历史（用户词造词用）
  void OnCommitted(const std::wstring& code, const std::wstring& text);
  bool CreateUserPhrase(std::wstring& outCode, std::wstring& outText);
  void ClearRecentCommits() { recentCommits_.clear(); }
  struct RecentCommit {
    std::wstring code;      // 用户输入的简码
    std::wstring fullCode;  // 五笔全码
    std::wstring text;
  };
  const std::vector<RecentCommit>& GetRecentCommits() const {
    return recentCommits_;
  }
  // 根据选中的条目按五笔规则生成词组编码
  static std::wstring BuildPhraseCode(
      const std::vector<struct PhraseEntry>& entries);
  const std::wstring& LastCommitted() const { return lastCommitted_; }
  const std::wstring& LastCommittedCode() const { return lastCommittedCode_; }

 private:
  std::vector<RecentCommit> recentCommits_;
  std::wstring lastCommittedCode_;
};