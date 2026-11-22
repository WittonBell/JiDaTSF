#pragma once
#include <windows.h>
#include <functional>
#include <string>
#include <vector>

struct PhraseEntry {
  std::wstring code;
  std::wstring fullCode;  // 新增
  std::wstring text;
  bool selected = true;
};

class CPhraseWindow {
 public:
  bool Create(HINSTANCE inst);
  void Destroy();

  using ConfirmHandler = std::function<void(const std::vector<PhraseEntry>&)>;
  using CancelHandler = std::function<void()>;

  CancelHandler OnCancel;

  void Show(const std::vector<PhraseEntry>& entries,
            POINT anchor,
            ConfirmHandler onConfirm);

  void Hide();
  bool IsVisible() const { return _hwnd && IsWindowVisible(_hwnd) != FALSE; }
  HWND GetHwnd() const { return _hwnd; }

  // 返回 true 表示该键已被造词窗口消费
  bool HandleKey(WPARAM vk);

 private:
  static LRESULT CALLBACK _WndProc(HWND, UINT, WPARAM, LPARAM);
  LRESULT _OnPaint();
  LRESULT _OnLButtonUp(LPARAM lParam);
  LRESULT _OnDpiChanged(WPARAM wParam, LPARAM lParam);

  void _RebuildFont();
  void _UpdateSize();
  void _Reposition(RECT anchor);
  void _Commit();
  std::wstring _BuildCode() const;
  std::wstring _BuildText() const;

  HWND _hwnd = nullptr;
  HFONT _font = nullptr;
  UINT _dpi = 96;
  int _cursor = 0;
  int _padding = 0;
  int _rowHeight = 0;
  std::vector<PhraseEntry> _entries;
  ConfirmHandler _onConfirm;
  int _lastX = -1, _lastY = -1, _lastW = -1, _lastH = -1;
};