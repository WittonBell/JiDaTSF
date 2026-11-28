#pragma once
// 造词悬浮窗口：选择最近提交的汉字并生成五笔词组编码。
#include <windows.h>
#include <functional>
#include <string>
#include <vector>

struct PhraseEntry {
  std::wstring code;      // 用户输入的简码
  std::wstring fullCode;  // 五笔全码
  std::wstring text;      // 候选汉字或词组文本
};

class CPhraseWindow {
 public:
  using ConfirmHandler =
      std::function<void(const std::wstring& code, const std::wstring& text)>;
  using CancelHandler = std::function<void()>;

  CancelHandler OnCancel;

  bool Create(HINSTANCE inst);
  void Destroy();
  void Show(const std::vector<PhraseEntry>& entries,
            POINT anchor,
            ConfirmHandler onConfirm);
  void Hide();
  bool IsVisible() const { return _hwnd && IsWindowVisible(_hwnd) != FALSE; }
  HWND GetHwnd() const { return _hwnd; }

  bool HandleKey(WPARAM vk);

 private:
  static LRESULT CALLBACK _WndProc(HWND, UINT, WPARAM, LPARAM);
  LRESULT _OnPaint();
  LRESULT _OnLButtonUp(LPARAM);
  LRESULT _OnDpiChanged(WPARAM, LPARAM);

  void _RebuildFont();
  void _UpdateSize();
  void _Reposition(RECT anchor);
  void _Commit();

  std::wstring _ComputeCode() const;
  std::wstring _ComputeText() const;
  static std::wstring _BuildCode(const std::vector<PhraseEntry>& sel);

  HWND _hwnd = nullptr;
  HFONT _font = nullptr;
  HFONT _smallFont = nullptr;
  UINT _dpi = 96;
  int _padding = 0;
  int _rowHeight = 0;
  int _hintHeight = 0;
  int _selectedCount = 2;  // 当前参与造词的字数（默认 2，最多 entries.size()）

  std::vector<PhraseEntry> _entries;
  ConfirmHandler _onConfirm;

  int _lastX = -1, _lastY = -1, _lastW = -1, _lastH = -1;
};