#pragma once
#include "Globals.h"

struct CandidateItem {
  std::wstring text;
  std::wstring code;
  bool isPunct = false;
};

enum class EngineAction {
  None,
  UpdateCandidates,
  Commit,
  TopCommitAndContinue
};

class JidaEngine {
 public:
  void SetPage(int p) { pageStart_ = p; }
  int PageStart() const { return pageStart_; }
  void Reset();
  EngineAction OnLetter(wchar_t ch,
                        std::wstring& committed,
                        std::wstring& newBuffer);
  bool OnDigit(int digit, std::wstring& committed);
  bool OnSpace(std::wstring& committed);
  bool OnSemicolon(std::wstring& committed, bool& waitForDateOrAmount);
  bool OnPunctuation(wchar_t ch, std::wstring& punct, std::wstring& firstCand);
  void OnBackspace();
  bool OnEnter(std::wstring& committed);
  EngineAction OnSemicolonTail(wchar_t ch, std::wstring& committed);

  const std::wstring& Buffer() const { return buffer_; }
  const std::vector<CandidateItem>& Candidates() const { return candidates_; }
  const std::wstring& LastCommitted() const { return lastCommitted_; }
  bool IsFourCodeComplete() const;
  bool IsWaitingDateOrAmount() const { return waitDateOrAmount_; }

 private:
  void Query(const std::wstring& code);
  void CommitText(const std::wstring& t);

  std::wstring buffer_;
  std::vector<CandidateItem> candidates_;
  std::wstring lastCommitted_;
  std::wstring semicolonTail_;
  bool waitDateOrAmount_ = false;
  int pageStart_ = 0;
  static constexpr int kPageSize = 9;
};