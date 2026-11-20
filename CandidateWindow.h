#pragma once
#include <windows.h>
#include <functional>
#include <string>
#include <vector>
#include "Globals.h"
#include "JidaEngine.h"

class CCandidateWindow {
 public:
  std::function<void(const std::wstring&)> OnPick;

  bool Create(HINSTANCE hInst);
  void Destroy();
  void Show(const std::vector<CandidateItem>& items, RECT rcCaret, UINT dpi);
  void Hide();
  void PageUp();
  void PageDown();

  // 新增：设置候选窗口顶部显示的编码
  void SetTitle(const std::wstring& t) { _title = t; }
  int PageStart() const { return _pageStart; }

 private:
  static LRESULT CALLBACK _WndProc(HWND, UINT, WPARAM, LPARAM);
  LRESULT _OnPaint();
  LRESULT _OnLButtonUp(LPARAM lParam);
  LRESULT _OnMouseMove(LPARAM lParam);
  LRESULT _OnMouseWheel(WPARAM wParam);
  LRESULT _OnDpiChanged(WPARAM wParam, LPARAM lParam);

  void _RebuildFont();
  void _UpdateSize();

  HWND _hwnd = nullptr;
  UINT _dpi = 96;
  int _selected = 0;
  int _pageStart = 0;
  std::vector<CandidateItem> _items;

  std::wstring _title;  // 新增：当前编码

  HFONT _font = nullptr;
  int _rowHeight = 0;
  int _padding = 0;
  int _maxWidth = 0;

  static constexpr int kPageSize = 9;
};