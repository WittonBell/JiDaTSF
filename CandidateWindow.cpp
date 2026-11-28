#include "CandidateWindow.h"
// 候选窗口实现：负责候选绘制、候选布局、鼠标命中及 DPI 缩放。
#include <windowsx.h>
#include <algorithm>
#include "DpiHelper.h"
#include "Globals.h"

static const wchar_t* kCandClass = L"JiDaCandWnd";
static constexpr UINT_PTR kTableLoadTimer = 4;

static bool IsVertical() {
  return g_config.candLayout == CandLayout::Vertical;
}

static std::wstring GetCandidateKeyHint(const CandidateItem& item,
                                        const std::wstring& inputCode) {
  if (!g_config.perKeyHint || inputCode.empty() ||
      item.code.size() <= inputCode.size() ||
      item.code.compare(0, inputCode.size(), inputCode) != 0)
    return L"";
  return item.code.substr(inputCode.size());
}

static COLORREF GetCandidateKeyHintColor(COLORREF foreground) {
  const int redDifference = GetRValue(foreground) - 128;
  const int greenDifference = GetGValue(foreground);
  const int blueDifference = GetBValue(foreground);
  // 前景色在 RGB 欧氏距离 48 以内视为接近暗红色，提示码改用深蓝色。
  if (redDifference * redDifference + greenDifference * greenDifference +
          blueDifference * blueDifference <=
      48 * 48)
    return RGB(0, 0, 192);
  return RGB(192, 0, 0);
}

bool CCandidateWindow::Create(HINSTANCE h) {
  // 注册窗口类并以 NOACTIVATE 风格创建悬浮候选框。
  WNDCLASSEX wc = {sizeof(wc)};
  wc.lpfnWndProc = _WndProc;
  wc.hInstance = h;
  wc.hCursor = LoadCursor(nullptr, IDC_HAND);
  wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
  wc.lpszClassName = kCandClass;
  RegisterClassEx(&wc);
  _hwnd = CreateWindowEx(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
                         kCandClass, L"", WS_POPUP | WS_BORDER, 0, 0, 10, 10,
                         nullptr, nullptr, h, this);
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
  if (_hintFont) {
    DeleteObject(_hintFont);
    _hintFont = nullptr;
  }
  if (_hwnd) {
    KillTimer(_hwnd, 3);
    KillTimer(_hwnd, kTableLoadTimer);
    DestroyWindow(_hwnd);
    _hwnd = nullptr;
  }
}

void CCandidateWindow::WatchForTableLoad() {
  if (_hwnd && !SetTimer(_hwnd, kTableLoadTimer, 100, nullptr))
    OutputDebugStringW(L"[JiDa] Candidate: failed to start table-load timer\n");
}

void CCandidateWindow::Hide() {
  if (!_hwnd)
    return;
  KillTimer(_hwnd, 3);
  KillTimer(_hwnd, kTableLoadTimer);
  ShowWindow(_hwnd, SW_HIDE);
}

void CCandidateWindow::_RebuildFont() {
  // 候选和提示文字字体会随配置与 DPI 一起重建。
  if (_font)
    DeleteObject(_font);
  if (_hintFont)
    DeleteObject(_hintFont);

  _font = CreateFontW(-MulDiv(g_config.candFontSize, _dpi, 72), 0, 0, 0,
                      g_config.candFontWeight,
                      g_config.candFontItalic ? TRUE : FALSE, FALSE, FALSE,
                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                      CLEARTYPE_QUALITY, DEFAULT_PITCH, g_config.candFontFace);

  _hintFont = CreateFontW(-MulDiv(11, _dpi, 72), 0, 0, 0, FW_NORMAL, FALSE,
                          FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                          CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
                          L"Microsoft YaHei");

  _rowHeight = MulDiv(g_config.candFontSize + 14, _dpi, 72);
  _padding = MulDiv(8, _dpi, 96);
  _hintHeight = MulDiv(20, _dpi, 96);
}

void CCandidateWindow::Show(const std::vector<CandidateItem>& items,
                            int selected,
                            RECT rc,
                            UINT dpi) {
  if (items.empty() && _title.empty()) {
    Hide();
    return;
  }
  _textMode = false;
  _textContent.clear();
  _textHint.clear();
  if (_hwnd)
    KillTimer(_hwnd, 3);
  _items = items;
  if (selected < 0)
    selected = 0;
  if (selected >= (int)_items.size() && !_items.empty())
    selected = (int)_items.size() - 1;
  _selected = selected;

  // 页起点按选中项自动调整
  _pageStart = (_selected / CandPageSize()) * CandPageSize();

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

void CCandidateWindow::ShowText(const std::wstring& text,
                                const std::wstring& hint,
                                RECT rc,
                                UINT dpi) {
  if (text.empty()) {
    Hide();
    return;
  }

  _textMode = true;
  _textContent = text;
  _textHint = hint;
  _items.clear();
  _title.clear();

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
  if (_textMode) {
    KillTimer(_hwnd, 3);
    SetTimer(_hwnd, 3, GetCaretBlinkTime(), nullptr);
    _textCursorVisible = true;
  }
  InvalidateRect(_hwnd, nullptr, FALSE);
}

void CCandidateWindow::_UpdateSize() {
  // 根据候选文字宽度、页大小和当前布局计算窗口尺寸。
  if (!_hwnd)
    return;

  if (_textMode) {
    HDC dc = GetDC(_hwnd);
    HGDIOBJ oldFont = SelectObject(dc, _font);

    SIZE sz = {0, 0};
    GetTextExtentPoint32W(dc, _textContent.c_str(), (int)_textContent.size(),
                          &sz);
    int contentW = (int)sz.cx;

    if (!_textHint.empty()) {
      SelectObject(dc, _hintFont);
      SIZE hintSz = {0, 0};
      GetTextExtentPoint32W(dc, _textHint.c_str(), (int)_textHint.size(),
                            &hintSz);
      if ((int)hintSz.cx > contentW)
        contentW = (int)hintSz.cx;
    }

    SelectObject(dc, oldFont);
    ReleaseDC(_hwnd, dc);

    int w = contentW + _padding * 2;
    int minW = MulDiv(220, _dpi, 96);
    if (w < minW)
      w = minW;

    int h = _padding * 2 + _rowHeight + MulDiv(6, _dpi, 96) + _hintHeight;
    SetWindowPos(_hwnd, nullptr, 0, 0, w, h,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    return;
  }

  int titleH = _title.empty() ? 0 : (_rowHeight + MulDiv(6, _dpi, 96));
  int pageEnd = (std::min)(_pageStart + CandPageSize(), (int)_items.size());
  int visible = pageEnd - _pageStart;
  if (visible < 0)
    visible = 0;

  // 用当前字体精确测量像素宽度
  HDC dc = GetDC(_hwnd);
  HGDIOBJ oldFont =
      SelectObject(dc, _font ? _font : GetStockObject(DEFAULT_GUI_FONT));

  // 空格宽度作为候选间距（极点风格：仅一个英文空格宽）
  SIZE spaceSz = {0, 0};
  GetTextExtentPoint32W(dc, L" ", 1, &spaceSz);
  _cellGap =
      (std::max)((int)spaceSz.cx, MulDiv(4, _dpi, 96));  // 防止字体异常时贴太紧

  // 标题实际宽度
  int titleW = 0;
  if (!_title.empty()) {
    SIZE tsz = {0, 0};
    GetTextExtentPoint32W(dc, _title.c_str(), (int)_title.size(), &tsz);
    titleW = (int)tsz.cx;
  }

  int w = 0, h = 0;

  if (IsVertical()) {
    _cellWidths.clear();
    int maxLineW = 0;
    for (int i = _pageStart; i < pageEnd; ++i) {
      std::wstring line =
          std::to_wstring(i - _pageStart + 1) + L". " + _items[i].text;
      SIZE sz = {0, 0};
      GetTextExtentPoint32W(dc, line.c_str(), (int)line.size(), &sz);
      int lineWidth = (int)sz.cx;
      std::wstring hint = GetCandidateKeyHint(_items[i], _title);
      if (!hint.empty()) {
        HGDIOBJ oldHintFont = SelectObject(dc, _hintFont);
        SIZE hintSz = {0, 0};
        GetTextExtentPoint32W(dc, hint.c_str(), (int)hint.size(), &hintSz);
        SelectObject(dc, oldHintFont);
        lineWidth += (int)hintSz.cx;
      }
      maxLineW = (std::max)(maxLineW, lineWidth);
    }
    w = (std::max)(maxLineW, titleW) + _padding * 2;
    if (visible < 1)
      visible = 1;
    h = titleH + _rowHeight * visible + _padding * 2;
  } else {
    // 横向：逐项精确测量，紧凑排列
    _cellWidths.clear();
    _cellWidths.reserve(visible);

    int totalCellW = 0;
    for (int i = _pageStart; i < pageEnd; ++i) {
      std::wstring line =
          std::to_wstring(i - _pageStart + 1) + L"." + _items[i].text;
      SIZE sz = {0, 0};
      GetTextExtentPoint32W(dc, line.c_str(), (int)line.size(), &sz);
      int cellWidth = (int)sz.cx;
      std::wstring hint = GetCandidateKeyHint(_items[i], _title);
      if (!hint.empty()) {
        HGDIOBJ oldHintFont = SelectObject(dc, _hintFont);
        SIZE hintSz = {0, 0};
        GetTextExtentPoint32W(dc, hint.c_str(), (int)hint.size(), &hintSz);
        SelectObject(dc, oldHintFont);
        cellWidth += (int)hintSz.cx;
      }
      _cellWidths.push_back(cellWidth);
      totalCellW += cellWidth;
    }

    int n = (int)_cellWidths.size();
    int gridW = totalCellW + _cellGap * (std::max)(0, n - 1);

    w = (std::max)(gridW, titleW) + _padding * 2;
    h = titleH + _rowHeight + _padding * 2;
  }

  SelectObject(dc, oldFont);
  ReleaseDC(_hwnd, dc);

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
    case WM_RBUTTONUP: {
      POINT pt = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
      ClientToScreen(h, &pt);
      if (self->OnContextMenu)
        self->OnContextMenu(pt);
      return 0;
    }
    case WM_TIMER:
      if (w == kTableLoadTimer) {
        if (IsJiDaTableLoaded()) {
          KillTimer(h, kTableLoadTimer);
          if (self->OnTableLoaded)
            self->OnTableLoaded();
        } else if (!IsJiDaTableLoading()) {
          KillTimer(h, kTableLoadTimer);
        }
        return 0;
      }
      if (w == 3 && self->_textMode) {
        self->_textCursorVisible = !self->_textCursorVisible;
        InvalidateRect(h, nullptr, FALSE);
        return 0;
      }
      break;
  }
  return DefWindowProc(h, msg, w, l);
}

LRESULT CCandidateWindow::_OnPaint() {
  // 绘制候选页、当前选择项及文本提示模式。
  PAINTSTRUCT ps;
  HDC hdc = BeginPaint(_hwnd, &ps);
  RECT rc;
  GetClientRect(_hwnd, &rc);

  HDC mem = CreateCompatibleDC(hdc);
  HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
  HGDIOBJ oldB = SelectObject(mem, bmp);
  // 设置背景色
  HBRUSH bg = CreateSolidBrush(g_config.candBgColor);
  FillRect(mem, &rc, bg);
  DeleteObject(bg);
  SetBkMode(mem, TRANSPARENT);
  HGDIOBJ oldF = SelectObject(mem, _font);

  if (_textMode) {
    // 文本模式：上行正文 + 下行提示
    HGDIOBJ oldF = SelectObject(mem, _font);
    SetTextColor(mem, RGB(30, 30, 30));
    RECT textRc = {_padding, _padding, rc.right - _padding,
                   _padding + _rowHeight};
    DrawTextW(mem, _textContent.c_str(), (int)_textContent.size(), &textRc,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(mem, oldF);

    HGDIOBJ oldH = SelectObject(mem, _hintFont);
    SetTextColor(mem, RGB(140, 140, 140));
    RECT hintRc = {_padding, rc.bottom - _padding - _hintHeight,
                   rc.right - _padding, rc.bottom - _padding};
    DrawTextW(mem, _textHint.c_str(), (int)_textHint.size(), &hintRc,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(mem, oldH);

    // ---- 闪烁光标 ----
    if (_textCursorVisible) {
      SIZE sz = {0, 0};
      HGDIOBJ cf = SelectObject(mem, _font);
      GetTextExtentPoint32W(mem, _textContent.c_str(), (int)_textContent.size(),
                            &sz);
      SelectObject(mem, cf);

      int cx = _padding + (int)sz.cx + MulDiv(2, _dpi, 96);
      int cTop = _padding + MulDiv(4, _dpi, 96);
      int cBot = _padding + _rowHeight - MulDiv(4, _dpi, 96);

      HPEN pen = CreatePen(PS_SOLID, MulDiv(1, _dpi, 96), RGB(30, 30, 30));
      HGDIOBJ op = SelectObject(mem, pen);
      MoveToEx(mem, cx, cTop, nullptr);
      LineTo(mem, cx, cBot);
      SelectObject(mem, op);
      DeleteObject(pen);
    }
    // ---- 光标结束 ----

    BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldB);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(_hwnd, &ps);
    return 0;
  }

  int y = _padding;

  if (!_title.empty()) {
    RECT titleRc = {_padding, y, rc.right - _padding, y + _rowHeight};
    SetTextColor(mem, g_config.candTextColor);
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

  int pageEnd = (std::min)(_pageStart + CandPageSize(), (int)_items.size());
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
      // 选中行
      COLORREF foreground;
      if (i == _selected) {
        HBRUSH selBg = CreateSolidBrush(g_config.candSelBgColor);
        FillRect(mem, &row, selBg);
        DeleteObject(selBg);
        foreground = g_config.candSelTextColor;
      } else {
        foreground = g_config.candTextColor;
      }
      SetTextColor(mem, foreground);
      DrawTextW(mem, line.c_str(), (int)line.size(), &row,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
      std::wstring hint = GetCandidateKeyHint(_items[i], _title);
      if (!hint.empty()) {
        SIZE lineSz = {0, 0};
        GetTextExtentPoint32W(mem, line.c_str(), (int)line.size(), &lineSz);
        HGDIOBJ oldHintFont = SelectObject(mem, _hintFont);
        RECT hintRc = {row.left + (LONG)lineSz.cx, row.top, row.right,
                       row.bottom};
        SetTextColor(mem, GetCandidateKeyHintColor(foreground));
        DrawTextW(mem, hint.c_str(), (int)hint.size(), &hintRc,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SetTextColor(mem, foreground);
        SelectObject(mem, oldHintFont);
      }
      y += _rowHeight;
    }
  } else {
    // 横向：每个候选按自身实测宽度紧凑排列，中间只隔 _cellGap
    int x = _padding;
    for (int i = _pageStart; i < pageEnd; ++i) {
      int local = i - _pageStart;
      int cw =
          (local < (int)_cellWidths.size()) ? _cellWidths[local] : _rowHeight;
      std::wstring line = std::to_wstring(local + 1) + L"." + _items[i].text;
      RECT cell = {x, y, x + cw, y + _rowHeight};
      // 选中行
      COLORREF foreground;
      if (i == _selected) {
        HBRUSH selBg = CreateSolidBrush(g_config.candSelBgColor);
        FillRect(mem, &cell, selBg);
        DeleteObject(selBg);
        foreground = g_config.candSelTextColor;
      } else {
        foreground = g_config.candTextColor;
      }
      SetTextColor(mem, foreground);
      DrawTextW(mem, line.c_str(), (int)line.size(), &cell,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
      std::wstring hint = GetCandidateKeyHint(_items[i], _title);
      if (!hint.empty()) {
        SIZE lineSz = {0, 0};
        GetTextExtentPoint32W(mem, line.c_str(), (int)line.size(), &lineSz);
        HGDIOBJ oldHintFont = SelectObject(mem, _hintFont);
        RECT hintRc = {cell.left + (LONG)lineSz.cx, cell.top, cell.right,
                       cell.bottom};
        SetTextColor(mem, GetCandidateKeyHintColor(foreground));
        DrawTextW(mem, hint.c_str(), (int)hint.size(), &hintRc,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SetTextColor(mem, foreground);
        SelectObject(mem, oldHintFont);
      }
      x += cw + _cellGap;
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

int CCandidateWindow::_HitTest(int mx, int my) const {
  // 将鼠标坐标转换为候选列表中的绝对索引。
  if (_textMode)
    return -1;

  int titleH = _title.empty() ? 0 : (_rowHeight + MulDiv(6, _dpi, 96));
  int pageEnd = (std::min)(_pageStart + CandPageSize(), (int)_items.size());
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
    int x = _padding;
    for (int local = 0; local < (int)_cellWidths.size(); ++local) {
      int cw = _cellWidths[local];
      if (mx >= x && mx < x + cw)
        return _pageStart + local;
      x += cw + _cellGap;
    }
    return -1;
  }
}

LRESULT CCandidateWindow::_OnLButtonUp(LPARAM l) {
  // 左键命中候选后通知输入引擎提交对应项。
  if (_textMode)
    return 0;

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
  if (_textMode)
    return 0;

  int idx = _HitTest(GET_X_LPARAM(l), GET_Y_LPARAM(l));
  if (idx != _hover) {
    _hover = idx;
    InvalidateRect(_hwnd, nullptr, TRUE);
  }
  return 0;
}

LRESULT CCandidateWindow::_OnMouseWheel(WPARAM w) {
  // 鼠标滚轮切换候选页并刷新悬浮窗口。
  if (GET_WHEEL_DELTA_WPARAM(w) > 0) {
    if (_pageStart >= CandPageSize())
      _pageStart -= CandPageSize();
    else
      _pageStart = 0;
  } else {
    if (_pageStart + CandPageSize() < (int)_items.size())
      _pageStart += CandPageSize();
  }
  _selected = _pageStart;
  _UpdateSize();  // 关键：刷新 _cellWidths 与窗口宽
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

void CCandidateWindow::RefreshFromConfig() {
  _RebuildFont();
  if (_hwnd && IsWindowVisible(_hwnd)) {
    _UpdateSize();
    InvalidateRect(_hwnd, nullptr, TRUE);
  }
}