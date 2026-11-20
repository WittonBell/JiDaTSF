#include "CandidateWindow.h"
#include <windowsx.h>
#include <algorithm>
#include "DpiHelper.h"

static const wchar_t* kCandClass = L"JidianCandWndV2";

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

void CCandidateWindow::PageUp() {
  _pageStart = (_pageStart >= kPageSize) ? _pageStart - kPageSize : 0;
  _selected = 0;
}

void CCandidateWindow::PageDown() {
  if (_pageStart + kPageSize <= (int)_items.size())
    _pageStart += kPageSize;
  _selected = 0;
}

void CCandidateWindow::Show(const std::vector<CandidateItem>& items,
                            RECT rc,
                            UINT dpi) {
  // 无候选且无标题才隐藏；否则即使无候选也显示窗口（只画标题）
  if (items.empty() && _title.empty()) {
    Hide();
    return;
  }
  _items = items;
  if (_selected >= (int)_items.size())
    _selected = 0;
  if (_pageStart >= (int)_items.size())
    _pageStart = 0;
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
  SetWindowPos(_hwnd, HWND_TOPMOST, x, y, w, h,
               SWP_NOACTIVATE | SWP_SHOWWINDOW);
  InvalidateRect(_hwnd, nullptr, TRUE);
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
}

void CCandidateWindow::_UpdateSize() {
  int maxChars = (int)_title.size() + 2;
  int end = (std::min)((int)_items.size(), _pageStart + kPageSize);
  for (int i = _pageStart; i < end; ++i) {
    maxChars = (std::max)(maxChars, (int)_items[i].text.size() + 3);
  }
  _maxWidth = MulDiv(maxChars * 18, _dpi, 96) + _padding * 2;

  int vis = end - _pageStart;
  if (vis < 1)
    vis = 1;
  int titleHeight = _title.empty() ? 0 : (_rowHeight + MulDiv(6, _dpi, 96));
  int h = titleHeight + _rowHeight * vis + _padding * 2;

  SetWindowPos(_hwnd, nullptr, 0, 0, _maxWidth, h,
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
  FillRect(mem, &rc, (HBRUSH)(COLOR_WINDOW + 1));
  SetBkMode(mem, TRANSPARENT);
  HGDIOBJ oldF = SelectObject(mem, _font);

  int y = _padding;

  // 标题（当前编码）
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

  // 候选行（可能为空）
  int end = (std::min)((int)_items.size(), _pageStart + kPageSize);
  if (_items.empty() && !_title.empty()) {
    // 无候选：提示文字
    RECT hintRc = {_padding, y, rc.right - _padding, y + _rowHeight};
    SetTextColor(mem, RGB(160, 160, 160));
    const wchar_t* hint = L"（无候选）";
    DrawTextW(mem, hint, (int)wcslen(hint), &hintRc,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    y += _rowHeight;
  } else {
    for (int i = _pageStart; i < end; ++i) {
      int local = i - _pageStart;
      std::wstring line = std::to_wstring(local + 1) + L". " + _items[i].text;
      RECT row = {_padding, y, rc.right - _padding, y + _rowHeight};
      if (local == _selected) {
        FillRect(mem, &row, (HBRUSH)(COLOR_HIGHLIGHT + 1));
        SetTextColor(mem, GetSysColor(COLOR_HIGHLIGHTTEXT));
      } else {
        SetTextColor(mem, GetSysColor(COLOR_WINDOWTEXT));
      }
      DrawTextW(mem, line.c_str(), (int)line.size(), &row,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
      y += _rowHeight;
    }
  }

  SelectObject(mem, oldF);
  BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
  SelectObject(mem, oldB);
  DeleteObject(bmp);
  DeleteDC(mem);
  EndPaint(_hwnd, &ps);
  return 0;
}

LRESULT CCandidateWindow::_OnLButtonUp(LPARAM l) {
  POINT pt = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
  int yStart =
      _padding + (_title.empty() ? 0 : (_rowHeight + MulDiv(4, _dpi, 96)));
  int row = (pt.y - yStart) / _rowHeight;
  int idx = _pageStart + row;
  if (row >= 0 && idx >= 0 && idx < (int)_items.size()) {
    _selected = row;
    InvalidateRect(_hwnd, nullptr, TRUE);
    if (OnPick)
      OnPick(_items[idx].text);
  }
  return 0;
}

LRESULT CCandidateWindow::_OnMouseMove(LPARAM l) {
  POINT pt = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
  int yStart =
      _padding + (_title.empty() ? 0 : (_rowHeight + MulDiv(4, _dpi, 96)));
  int row = (pt.y - yStart) / _rowHeight;
  if (row >= 0 && row < kPageSize && _pageStart + row < (int)_items.size()) {
    if (_selected != row) {
      _selected = row;
      InvalidateRect(_hwnd, nullptr, TRUE);
    }
  }
  return 0;
}

LRESULT CCandidateWindow::_OnMouseWheel(WPARAM w) {
  if (GET_WHEEL_DELTA_WPARAM(w) > 0)
    PageUp();
  else
    PageDown();
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