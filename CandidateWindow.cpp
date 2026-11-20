#include "CandidateWindow.h"
#include <windowsx.h>
#include <algorithm>
#include "DpiHelper.h"

static const wchar_t* kCandClass = L"JiDaCandWnd";

static bool IsVertical() {
  return g_config.candLayout == CandLayout::Vertical;
}

bool CCandidateWindow::Create(HINSTANCE h) {
  WNDCLASSEX wc = {sizeof(wc)};
  wc.lpfnWndProc = _WndProc;
  wc.hInstance = h;
  wc.hCursor = LoadCursor(nullptr, IDC_HAND);
  wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
  wc.lpszClassName = kCandClass;
  RegisterClassEx(&wc);
  _hwnd = CreateWindowEx(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
                         kCandClass, L"", WS_POPUP, 0, 0, 10, 10, nullptr,
                         nullptr, h, this);
  if (_hwnd) {
    _dpi = 96;
    _RebuildFont();
  }
  return _hwnd != nullptr;
}

void CCandidateWindow::Destroy() {
  if (_font) {
    DeleteObject(_font);
    _font = nullptr;
  }
  if (_hwnd) {
    DestroyWindow(_hwnd);
    _hwnd = nullptr;
  }
}

void CCandidateWindow::Hide() {
  if (_hwnd)
    ShowWindow(_hwnd, SW_HIDE);
}

void CCandidateWindow::_RebuildFont() {
  if (_font)
    DeleteObject(_font);
  _font = CreateFontW(-MulDiv(14, _dpi, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE,
                      FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                      CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
                      L"Microsoft YaHei");
  _rowHeight = MulDiv(28, _dpi, 96);
  _padding = MulDiv(8, _dpi, 96);
  // _cellWidth 由 _UpdateSize 根据当前页内容动态计算
}

void CCandidateWindow::Show(const std::vector<CandidateItem>& items,
                            int selected,
                            RECT rc,
                            UINT dpi) {
  if (items.empty() && _title.empty()) {
    Hide();
    return;
  }

  _items = items;
  if (selected < 0)
    selected = 0;
  if (selected >= (int)_items.size() && !_items.empty())
    selected = (int)_items.size() - 1;
  _selected = selected;

  // 页起点按选中项自动调整
  _pageStart = (_selected / kPageSize) * kPageSize;

  if (dpi != _dpi) {
    _dpi = dpi;
    _RebuildFont();
  }
  _UpdateSize();

  HMONITOR hmon = MonitorFromRect(&rc, MONITOR_DEFAULTTONEAREST);
  MONITORINFO mi = {sizeof(mi)};
  GetMonitorInfo(hmon, &mi);
  RECT wr;
  GetWindowRect(_hwnd, &wr);
  int w = wr.right - wr.left, h = wr.bottom - wr.top;
  int x = rc.left;
  int y = rc.bottom + MulDiv(4, _dpi, 96);
  if (y + h > mi.rcWork.bottom)
    y = rc.top - h - MulDiv(4, _dpi, 96);
  if (x + w > mi.rcWork.right)
    x = mi.rcWork.right - w;
  if (x < mi.rcWork.left)
    x = mi.rcWork.left;
  if (y < mi.rcWork.top)
    y = mi.rcWork.top;

  bool needRepos = (x != _lastX || y != _lastY || w != _lastW || h != _lastH);
  if (needRepos) {
    SetWindowPos(_hwnd, HWND_TOPMOST, x, y, w, h,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    _lastX = x;
    _lastY = y;
    _lastW = w;
    _lastH = h;
  } else if (!IsWindowVisible(_hwnd)) {
    ShowWindow(_hwnd, SW_SHOWNOACTIVATE);
  }
  InvalidateRect(_hwnd, nullptr, FALSE);
}

void CCandidateWindow::_UpdateSize() {
  int titleH = _title.empty() ? 0 : (_rowHeight + MulDiv(6, _dpi, 96));
  int pageEnd = (std::min)(_pageStart + kPageSize, (int)_items.size());
  int visible = pageEnd - _pageStart;
  if (visible < 0)
    visible = 0;

  int w, h;
  if (IsVertical()) {
    int maxChars = (int)_title.size() + 2;
    for (int i = _pageStart; i < pageEnd; ++i)
      maxChars = (std::max)(maxChars, (int)_items[i].text.size() + 3);
    w = MulDiv(maxChars * 18, _dpi, 96) + _padding * 2;
    if (visible < 1)
      visible = 1;
    h = titleH + _rowHeight * visible + _padding * 2;
  } else {
    // 横向：按当前页最长候选动态计算每格宽度，与实际绘制保持一致
    int n = (visible < 1) ? 1 : visible;

    int maxCellChars = 3;  // 至少容纳 "N.字"
    for (int i = _pageStart; i < pageEnd; ++i) {
      int len = (int)_items[i].text.size() + 2;  // "N." 前缀
      maxCellChars = (std::max)(maxCellChars, len);
    }
    // 18px/字符估算，下限 60px 避免单字时格子过窄
    _cellWidth =
        (std::max)(MulDiv(maxCellChars * 18, _dpi, 96), MulDiv(60, _dpi, 96));

    int gridWidth = _cellWidth * n;
    int titleWidth =
        _title.empty() ? 0 : MulDiv(((int)_title.size() + 2) * 18, _dpi, 96);

    // 窗口宽度 = max(格子总宽, 标题宽) + 左右内边距
    w = (std::max)(gridWidth, titleWidth) + _padding * 2;
    h = titleH + _rowHeight + _padding * 2;
  }

  SetWindowPos(_hwnd, nullptr, 0, 0, w, h,
               SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

LRESULT CALLBACK CCandidateWindow::_WndProc(HWND h,
                                            UINT msg,
                                            WPARAM w,
                                            LPARAM l) {
  CCandidateWindow* self = nullptr;
  if (msg == WM_NCCREATE) {
    self = (CCandidateWindow*)((CREATESTRUCT*)l)->lpCreateParams;
    SetWindowLongPtr(h, GWLP_USERDATA, (LONG_PTR)self);
  } else
    self = (CCandidateWindow*)GetWindowLongPtr(h, GWLP_USERDATA);
  if (!self)
    return DefWindowProc(h, msg, w, l);
  switch (msg) {
    case WM_PAINT:
      return self->_OnPaint();
    case WM_LBUTTONUP:
      return self->_OnLButtonUp(l);
    case WM_MOUSEMOVE:
      return self->_OnMouseMove(l);
    case WM_MOUSEWHEEL:
      return self->_OnMouseWheel(w);
    case WM_DPICHANGED:
      return self->_OnDpiChanged(w, l);
    case WM_MOUSEACTIVATE:
      return MA_NOACTIVATE;
  }
  return DefWindowProc(h, msg, w, l);
}

LRESULT CCandidateWindow::_OnPaint() {
  PAINTSTRUCT ps;
  HDC hdc = BeginPaint(_hwnd, &ps);
  RECT rc;
  GetClientRect(_hwnd, &rc);

  HDC mem = CreateCompatibleDC(hdc);
  HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
  HGDIOBJ oldB = SelectObject(mem, bmp);
  // 设置背景色
  HBRUSH hBrush = CreateSolidBrush(RGB(251,255,247));
  FillRect(mem, &rc, hBrush);
  SetBkMode(mem, TRANSPARENT);
  HGDIOBJ oldF = SelectObject(mem, _font);

  int y = _padding;

  if (!_title.empty()) {
    RECT titleRc = {_padding, y, rc.right - _padding, y + _rowHeight};
    SetTextColor(mem, RGB(0, 100, 200));
    DrawTextW(mem, _title.c_str(), (int)_title.size(), &titleRc,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    y += _rowHeight;

    HPEN pen = CreatePen(PS_SOLID, 1, RGB(210, 210, 210));
    HGDIOBJ oldPen = SelectObject(mem, pen);
    MoveToEx(mem, _padding, y + MulDiv(1, _dpi, 96), nullptr);
    LineTo(mem, rc.right - _padding, y + MulDiv(1, _dpi, 96));
    SelectObject(mem, oldPen);
    DeleteObject(pen);
    y += MulDiv(4, _dpi, 96);
  }

  int pageEnd = (std::min)(_pageStart + kPageSize, (int)_items.size());
  if (_items.empty()) {
    RECT hintRc = {_padding, y, rc.right - _padding, y + _rowHeight};
    SetTextColor(mem, RGB(160, 160, 160));
    const wchar_t* hint = L"（无候选）";
    DrawTextW(mem, hint, (int)wcslen(hint), &hintRc,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  } else if (IsVertical()) {
    for (int i = _pageStart; i < pageEnd; ++i) {
      int local = i - _pageStart;
      std::wstring line = std::to_wstring(local + 1) + L". " + _items[i].text;
      RECT row = {_padding, y, rc.right - _padding, y + _rowHeight};
      if (i == _selected) {
        FillRect(mem, &row, (HBRUSH)(COLOR_HIGHLIGHT + 1));
        SetTextColor(mem, GetSysColor(COLOR_HIGHLIGHTTEXT));
      } else
        SetTextColor(mem, GetSysColor(COLOR_WINDOWTEXT));
      DrawTextW(mem, line.c_str(), (int)line.size(), &row,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
      y += _rowHeight;
    }
  } else {
    // 横向：每格 _cellWidth 宽
    int x = _padding;
    for (int i = _pageStart; i < pageEnd; ++i) {
      int local = i - _pageStart;
      std::wstring line = std::to_wstring(local + 1) + L"." + _items[i].text;
      RECT cell = {x, y, x + _cellWidth, y + _rowHeight};
      if (i == _selected) {
        FillRect(mem, &cell, (HBRUSH)(COLOR_HIGHLIGHT + 1));
        SetTextColor(mem, GetSysColor(COLOR_HIGHLIGHTTEXT));
      } else
        SetTextColor(mem, GetSysColor(COLOR_WINDOWTEXT));
      DrawTextW(mem, line.c_str(), (int)line.size(), &cell,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
      x += _cellWidth;
    }
  }

  SelectObject(mem, oldF);
  BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
  SelectObject(mem, oldB);
  DeleteObject(bmp);
  DeleteDC(mem);
  DeleteObject(hBrush);
  EndPaint(_hwnd, &ps);
  return 0;
}

int CCandidateWindow::_HitTest(int mx, int my) const {
  int titleH = _title.empty() ? 0 : (_rowHeight + MulDiv(6, _dpi, 96));
  int pageEnd = (std::min)(_pageStart + kPageSize, (int)_items.size());
  if (IsVertical()) {
    int base = _padding + titleH;
    int row = (my - base) / _rowHeight;
    if (row < 0 || row >= pageEnd - _pageStart)
      return -1;
    return _pageStart + row;
  } else {
    int y0 = _padding + titleH;
    if (my < y0 || my >= y0 + _rowHeight)
      return -1;
    int col = (mx - _padding) / _cellWidth;
    if (col < 0 || col >= pageEnd - _pageStart)
      return -1;
    return _pageStart + col;
  }
}

LRESULT CCandidateWindow::_OnLButtonUp(LPARAM l) {
  int idx = _HitTest(GET_X_LPARAM(l), GET_Y_LPARAM(l));
  if (idx >= 0) {
    _selected = idx;
    InvalidateRect(_hwnd, nullptr, TRUE);
    if (OnPick)
      OnPick(idx);
  }
  return 0;
}

LRESULT CCandidateWindow::_OnMouseMove(LPARAM l) {
  int idx = _HitTest(GET_X_LPARAM(l), GET_Y_LPARAM(l));
  if (idx != _hover) {
    _hover = idx;
    InvalidateRect(_hwnd, nullptr, TRUE);
  }
  return 0;
}

LRESULT CCandidateWindow::_OnMouseWheel(WPARAM w) {
  if (GET_WHEEL_DELTA_WPARAM(w) > 0) {
    if (_pageStart >= kPageSize)
      _pageStart -= kPageSize;
    else
      _pageStart = 0;
  } else {
    if (_pageStart + kPageSize < (int)_items.size())
      _pageStart += kPageSize;
  }
  _selected = _pageStart;
  InvalidateRect(_hwnd, nullptr, TRUE);
  return 0;
}

LRESULT CCandidateWindow::_OnDpiChanged(WPARAM w, LPARAM l) {
  _dpi = HIWORD(w);
  _RebuildFont();
  RECT* prc = (RECT*)l;
  SetWindowPos(_hwnd, nullptr, prc->left, prc->top, prc->right - prc->left,
               prc->bottom - prc->top, SWP_NOZORDER | SWP_NOACTIVATE);
  _UpdateSize();
  InvalidateRect(_hwnd, nullptr, TRUE);
  return 0;
}