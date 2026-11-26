#pragma once
#include <windows.h>
#include <functional>
#include <string>
#include <vector>
#include "Globals.h"
#include "JiDaEngine.h"

class CCandidateWindow {
 public:
  std::function<void(int)> OnPick;  // 回调：被点/悬停的候选绝对索引

  bool Create(HINSTANCE hInst);
  void Destroy();
  void Show(const std::vector<CandidateItem>& items,
            int selected,
            RECT rcCaret,
            UINT dpi);
  void ShowText(const std::wstring& text,
                const std::wstring& hint,
                RECT rcCaret,
                UINT dpi);
  void Hide();

  std::function<void(POINT)> OnContextMenu;  // 右键菜单回调
  HWND GetHwnd() const { return _hwnd; } 
  void RefreshFromConfig();
  void SetTitle(const std::wstring& t) { _title = t; }

 private:
  static LRESULT CALLBACK _WndProc(HWND, UINT, WPARAM, LPARAM);
  LRESULT _OnPaint();
  LRESULT _OnLButtonUp(LPARAM lParam);
  LRESULT _OnMouseMove(LPARAM lParam);
  LRESULT _OnMouseWheel(WPARAM wParam);
  LRESULT _OnDpiChanged(WPARAM wParam, LPARAM lParam);

  void _RebuildFont();
  void _UpdateSize();
  int _HitTest(int x, int y) const;  // 命中 → 候选绝对索引，否则 -1

  HWND _hwnd = nullptr;
  UINT _dpi = 96;
  int _selected = 0;
  int _hover = -1;
  int _pageStart = 0;
  std::vector<CandidateItem> _items;
  std::wstring _title;

  HFONT _font = nullptr;
  int _rowHeight = 0;
  int _padding = 0;
  // 横向布局：每个候选的精确像素宽度（下标 = i - _pageStart）
  std::vector<int> _cellWidths;
  int _cellGap = 0;  // 候选之间的间距（= 一个空格宽度）

  int _lastX = -1, _lastY = -1, _lastW = -1, _lastH = -1;

  bool _textMode = false;
  std::wstring _textContent;
  std::wstring _textHint;
  HFONT _hintFont = nullptr;
  int _hintHeight = 0;

  bool _textCursorVisible = true;
};