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
};