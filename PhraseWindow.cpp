#include "PhraseWindow.h"
#include <shellscalingapi.h>
#include <windowsx.h>
#include <algorithm>
#pragma comment(lib, "Shcore.lib")

static const wchar_t* kPhraseClass = L"JiDa.PhraseWindow";

bool CPhraseWindow::Create(HINSTANCE inst) {
  WNDCLASSEX wc = {sizeof(wc)};
  wc.lpfnWndProc = _WndProc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
  wc.lpszClassName = kPhraseClass;
  RegisterClassEx(&wc);

  _hwnd = CreateWindowEx(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
                         kPhraseClass, L"", WS_POPUP, 0, 0, 100, 100, nullptr,
                         nullptr, inst, this);
  if (_hwnd) {
    _dpi = 96;
    _RebuildFont();
  }
  return _hwnd != nullptr;
}

void CPhraseWindow::Destroy() {
  if (_font) {
    DeleteObject(_font);
    _font = nullptr;
  }
  if (_hwnd) {
    DestroyWindow(_hwnd);
    _hwnd = nullptr;
  }
}

void CPhraseWindow::Hide() {
  if (_hwnd)
    ShowWindow(_hwnd, SW_HIDE);
}

void CPhraseWindow::_RebuildFont() {
  if (_font)
    DeleteObject(_font);
  _font = CreateFontW(-MulDiv(16, _dpi, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE,
                      FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                      CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
                      L"Microsoft YaHei");
  _rowHeight = MulDiv(40, _dpi, 96);
  _padding = MulDiv(12, _dpi, 96);
}

void CPhraseWindow::Show(const std::vector<PhraseEntry>& entries,
                         POINT anchor,
                         ConfirmHandler onConfirm) {
  if (entries.empty())
    return;

  _entries = entries;
  _cursor = 0;
  _onConfirm = std::move(onConfirm);
  _lastX = _lastY = _lastW = _lastH = -1;

  HMONITOR hmon = MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST);
  UINT dx = 96, dy = 96;
  if (SUCCEEDED(GetDpiForMonitor(hmon, MDT_EFFECTIVE_DPI, &dx, &dy)))
    _dpi = dx;
  _RebuildFont();

  RECT anchorRc = {anchor.x, anchor.y, anchor.x + 1, anchor.y + 1};
  _UpdateSize();
  _Reposition(anchorRc);
  ShowWindow(_hwnd, SW_SHOWNOACTIVATE);
  InvalidateRect(_hwnd, nullptr, TRUE);
}

void CPhraseWindow::_UpdateSize() {
  const int itemW = MulDiv(64, _dpi, 96);
  int w = _padding * 2 + (int)_entries.size() * itemW;
  int minW = MulDiv(300, _dpi, 96);
  if (w < minW)
    w = minW;
  int h = _rowHeight + _padding * 2 + MulDiv(24, _dpi, 96);
  SetWindowPos(_hwnd, nullptr, 0, 0, w, h,
               SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void CPhraseWindow::_Reposition(RECT anchor) {
  RECT wr;
  GetWindowRect(_hwnd, &wr);
  int w = wr.right - wr.left;
  int h = wr.bottom - wr.top;

  HMONITOR hmon = MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST);
  MONITORINFO mi = {sizeof(mi)};
  GetMonitorInfo(hmon, &mi);

  int x = anchor.left;
  int y = anchor.bottom + MulDiv(6, _dpi, 96);
  if (y + h > mi.rcWork.bottom)
    y = anchor.top - h - MulDiv(6, _dpi, 96);
  if (x + w > mi.rcWork.right)
    x = mi.rcWork.right - w;
  if (x < mi.rcWork.left)
    x = mi.rcWork.left;
  if (y < mi.rcWork.top)
    y = mi.rcWork.top;

  SetWindowPos(_hwnd, HWND_TOPMOST, x, y, w, h,
               SWP_NOACTIVATE | SWP_SHOWWINDOW);
  _lastX = x;
  _lastY = y;
  _lastW = w;
  _lastH = h;
}

std::wstring CPhraseWindow::_BuildCode() const {
  std::wstring code;
  for (auto& e : _entries)
    if (e.selected)
      code += e.code;
  if (code.size() > 4)
    code.resize(4);
  return code;
}

std::wstring CPhraseWindow::_BuildText() const {
  std::wstring text;
  for (auto& e : _entries)
    if (e.selected)
      text += e.text;
  return text;
}

void CPhraseWindow::_Commit() {
  std::vector<PhraseEntry> selected;
  for (auto& e : _entries)
    if (e.selected)
      selected.push_back(e);
  if (!selected.empty() && _onConfirm)
    _onConfirm(selected);
  Hide();
}

bool CPhraseWindow::HandleKey(WPARAM vk) {
  if (!IsVisible())
    return false;

  switch (vk) {
    case VK_LEFT:
      if (_cursor > 0) {
        --_cursor;
        InvalidateRect(_hwnd, nullptr, TRUE);
      }
      return true;
    case VK_RIGHT:
      if (_cursor + 1 < (int)_entries.size()) {
        ++_cursor;
        InvalidateRect(_hwnd, nullptr, TRUE);
      }
      return true;
    case VK_SPACE:
      if (_cursor >= 0 && _cursor < (int)_entries.size()) {
        _entries[_cursor].selected = !_entries[_cursor].selected;
        InvalidateRect(_hwnd, nullptr, TRUE);
      }
      return true;
    case VK_RETURN:
      _Commit();
      return true;
    case VK_ESCAPE:
      Hide();
      if (OnCancel)
        OnCancel();  // ← 通知外部清空历史
      return true;
  }
  return false;
}

LRESULT CALLBACK CPhraseWindow::_WndProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
  CPhraseWindow* self = nullptr;
  if (msg == WM_NCCREATE) {
    self = (CPhraseWindow*)((CREATESTRUCT*)l)->lpCreateParams;
    SetWindowLongPtr(h, GWLP_USERDATA, (LONG_PTR)self);
  } else
    self = (CPhraseWindow*)GetWindowLongPtr(h, GWLP_USERDATA);
  if (!self)
    return DefWindowProc(h, msg, w, l);

  switch (msg) {
    case WM_PAINT:
      return self->_OnPaint();
    case WM_LBUTTONUP:
      return self->_OnLButtonUp(l);
    case WM_DPICHANGED:
      return self->_OnDpiChanged(w, l);
    case WM_MOUSEACTIVATE:
      return MA_NOACTIVATE;
  }
  return DefWindowProc(h, msg, w, l);
}

LRESULT CPhraseWindow::_OnPaint() {
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

  int itemW = MulDiv(64, _dpi, 96);
  int x = _padding;
  int y = _padding;

  for (int i = 0; i < (int)_entries.size(); ++i) {
    auto& e = _entries[i];
    RECT item = {x, y, x + itemW - MulDiv(2, _dpi, 96), y + _rowHeight};

    COLORREF bg = e.selected ? RGB(224, 244, 224) : RGB(240, 240, 240);
    HBRUSH br = CreateSolidBrush(bg);
    FillRect(mem, &item, br);
    DeleteObject(br);

    if (i == _cursor) {
      HPEN pen = CreatePen(PS_SOLID, 2, RGB(0, 120, 215));
      HGDIOBJ oldP = SelectObject(mem, pen);
      HGDIOBJ oldBr = SelectObject(mem, GetStockObject(NULL_BRUSH));
      Rectangle(mem, item.left, item.top, item.right, item.bottom);
      SelectObject(mem, oldBr);
      SelectObject(mem, oldP);
      DeleteObject(pen);
    }

    // 上半：字
    SetTextColor(mem, RGB(30, 30, 30));
    RECT textRc = item;
    textRc.bottom = item.top + _rowHeight / 2 + MulDiv(6, _dpi, 96);
    DrawTextW(mem, e.text.c_str(), -1, &textRc,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // 下半：编码
    HFONT smallFont = CreateFontW(
        -MulDiv(11, _dpi, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");
    HGDIOBJ oldSmall = SelectObject(mem, smallFont);
    SetTextColor(mem, RGB(120, 120, 120));
    RECT codeRc = {item.left, item.top + _rowHeight / 2, item.right,
                   item.bottom};
    DrawTextW(mem, e.code.c_str(), -1, &codeRc,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(mem, oldSmall);
    DeleteObject(smallFont);

    x += itemW;
  }

  // 底部提示行
  HFONT hintFont = CreateFontW(
      -MulDiv(11, _dpi, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
      CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");
  HGDIOBJ oldHint = SelectObject(mem, hintFont);
  SetTextColor(mem, RGB(100, 100, 100));
  RECT hintRc = {_padding, y + _rowHeight + MulDiv(2, _dpi, 96),
                 rc.right - _padding, rc.bottom - _padding};
  DrawTextW(mem, L"← → 移动   空格 切换选中   回车 确认   ESC 取消", -1,
            &hintRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
  SelectObject(mem, oldHint);
  DeleteObject(hintFont);

  SelectObject(mem, oldF);
  BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
  SelectObject(mem, oldB);
  DeleteObject(bmp);
  DeleteDC(mem);
  EndPaint(_hwnd, &ps);
  return 0;
}

LRESULT CPhraseWindow::_OnLButtonUp(LPARAM l) {
  POINT pt = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
  int itemW = MulDiv(64, _dpi, 96);
  int idx = (pt.x - _padding) / itemW;
  if (idx >= 0 && idx < (int)_entries.size()) {
    _cursor = idx;
    _entries[idx].selected = !_entries[idx].selected;
    InvalidateRect(_hwnd, nullptr, TRUE);
  }
  return 0;
}

LRESULT CPhraseWindow::_OnDpiChanged(WPARAM w, LPARAM l) {
  _dpi = HIWORD(w);
  _RebuildFont();
  RECT* prc = (RECT*)l;
  SetWindowPos(_hwnd, nullptr, prc->left, prc->top, prc->right - prc->left,
               prc->bottom - prc->top, SWP_NOZORDER | SWP_NOACTIVATE);
  _UpdateSize();
  InvalidateRect(_hwnd, nullptr, TRUE);
  return 0;
}