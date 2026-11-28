#include "PhraseWindow.h"
// 造词窗口实现：根据最近提交内容生成五笔词组并提供键盘交互。
#include <shellscalingapi.h>
#include <windowsx.h>
#include <algorithm>
#include "Globals.h"

#pragma comment(lib, "Shcore.lib")

static const wchar_t* kPhraseClass = L"JiDa.PhraseWindow";

bool CPhraseWindow::Create(HINSTANCE inst) {
  // 创建无激活、置顶的造词悬浮窗口。
  WNDCLASSEX wc = {sizeof(wc)};
  wc.lpfnWndProc = _WndProc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = nullptr;
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
  if (_smallFont) {
    DeleteObject(_smallFont);
    _smallFont = nullptr;
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
  if (_smallFont)
    DeleteObject(_smallFont);

  // 正文字体：与候选框一致（磅值→像素用 dpi/72）
  _font = CreateFontW(-MulDiv(g_config.candFontSize, _dpi, 72), 0, 0, 0,
                      g_config.candFontWeight,
                      g_config.candFontItalic ? TRUE : FALSE, FALSE, FALSE,
                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                      CLEARTYPE_QUALITY, DEFAULT_PITCH, g_config.candFontFace);

  // 编码字体：稍小，同样用 candFontFace
  _smallFont = CreateFontW(
      -MulDiv(g_config.candFontSize - 2 > 9 ? g_config.candFontSize - 2 : 9,
              _dpi, 72),
      0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
      g_config.candFontFace);

  _rowHeight = MulDiv(g_config.candFontSize + 20, _dpi, 72);
  _hintHeight = MulDiv(22, _dpi, 96);
  _padding = MulDiv(14, _dpi, 96);
}

void CPhraseWindow::Show(const std::vector<PhraseEntry>& entries,
                         POINT anchor,
                         ConfirmHandler onConfirm) {
  // 至少需要两个汉字才能按照五笔规则生成词组。
  if (entries.size() < 2)
    return;

  _entries = entries;
  _selectedCount = 2;
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
  HDC dc = GetDC(_hwnd);
  HGDIOBJ oldFont = SelectObject(dc, _font);

  std::wstring text = _ComputeText();
  SIZE textSz = {0, 0};
  GetTextExtentPoint32W(dc, text.c_str(), (int)text.size(), &textSz);

  SelectObject(dc, _smallFont);
  std::wstring code = _ComputeCode();
  SIZE codeSz = {0, 0};
  if (!code.empty())
    GetTextExtentPoint32W(dc, code.c_str(), (int)code.size(), &codeSz);

  const wchar_t* hint = L"← → 加/减字   Shift+← → 快速   Enter 确认   Esc 取消";
  SIZE hintSz = {0, 0};
  GetTextExtentPoint32W(dc, hint, (int)wcslen(hint), &hintSz);

  SelectObject(dc, oldFont);
  ReleaseDC(_hwnd, dc);

  LONG contentW = (std::max)(textSz.cx, codeSz.cx);
  contentW = (std::max)(contentW, hintSz.cx);

  int w = contentW + _padding * 2;
  int minW = MulDiv(360, _dpi, 96);
  if (w < minW)
    w = minW;

  int h = _padding * 2 + _rowHeight + MulDiv(4, _dpi, 96) + _hintHeight;

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

std::wstring CPhraseWindow::_ComputeText() const {
  if (_selectedCount <= 0)
    return L"";
  std::wstring text;
  size_t start = _entries.size() - _selectedCount;
  for (size_t i = start; i < _entries.size(); ++i) {
    text += _entries[i].text;
  }
  return text;
}

std::wstring CPhraseWindow::_ComputeCode() const {
  if (_selectedCount < 2)
    return L"";
  std::vector<PhraseEntry> sel(_entries.end() - _selectedCount, _entries.end());
  return L"【" + _BuildCode(sel) + L"】";
}

std::wstring CPhraseWindow::_BuildCode(const std::vector<PhraseEntry>& sel) {
  // 按 2 字、3 字、4 字及多字词的五笔取码规则拼接编码。
  if (sel.empty())
    return L"";
  auto getFull = [](const PhraseEntry& e) -> const std::wstring& {
    return e.fullCode.empty() ? e.code : e.fullCode;
  };
  auto safeSub = [](const std::wstring& s, size_t pos, size_t len) {
    if (pos >= s.size())
      return std::wstring();
    return s.substr(pos, (std::min)(len, s.size() - pos));
  };

  size_t n = sel.size();
  std::wstring code;
  if (n == 2) {
    code = safeSub(getFull(sel[0]), 0, 2) + safeSub(getFull(sel[1]), 0, 2);
  } else if (n == 3) {
    code = safeSub(getFull(sel[0]), 0, 1) + safeSub(getFull(sel[1]), 0, 1) +
           safeSub(getFull(sel[2]), 0, 2);
  } else if (n == 4) {
    for (size_t i = 0; i < 4; ++i)
      code += safeSub(getFull(sel[i]), 0, 1);
  } else {
    code = safeSub(getFull(sel[0]), 0, 1) + safeSub(getFull(sel[1]), 0, 1) +
           safeSub(getFull(sel[2]), 0, 1) + safeSub(getFull(sel[n - 1]), 0, 1);
  }
  if (code.size() > 4)
    code.resize(4);
  return code;
}

void CPhraseWindow::_Commit() {
  if (_selectedCount < 2) {
    Hide();
    return;
  }
  std::vector<PhraseEntry> sel(_entries.end() - _selectedCount, _entries.end());
  std::wstring code = _BuildCode(sel);
  std::wstring text;
  for (auto& e : sel)
    text += e.text;
  if (_onConfirm && !code.empty() && !text.empty())
    _onConfirm(code, text);
  Hide();
}

bool CPhraseWindow::HandleKey(WPARAM vk) {
  // 左右键调整造词字数，Enter 确认，Esc 取消。
  if (!IsVisible())
    return false;

  // Shift 按住时一次移动 5 个，否则一次 1 个
  bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
  int step = shift ? 5 : 1;

  switch (vk) {
    case VK_LEFT: {
      int newCount = _selectedCount + step;
      if (newCount > (int)_entries.size())
        newCount = (int)_entries.size();
      if (newCount != _selectedCount) {
        _selectedCount = newCount;
        _UpdateSize();
        InvalidateRect(_hwnd, nullptr, TRUE);
      }
      return true;
    }
    case VK_RIGHT: {
      int newCount = _selectedCount - step;
      if (newCount < 2)
        newCount = 2;
      if (newCount != _selectedCount) {
        _selectedCount = newCount;
        _UpdateSize();
        InvalidateRect(_hwnd, nullptr, TRUE);
      }
      return true;
    }
    case VK_RETURN:
      _Commit();
      return true;
    case VK_ESCAPE:
      Hide();
      if (OnCancel)
        OnCancel();
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

  // 背景（浅色）
  HBRUSH bg = CreateSolidBrush(RGB(252, 252, 252));
  FillRect(mem, &rc, bg);
  DeleteObject(bg);

  // 边框
  HPEN pen = CreatePen(PS_SOLID, 1, RGB(180, 180, 180));
  HGDIOBJ oldPen = SelectObject(mem, pen);
  HGDIOBJ oldBr = SelectObject(mem, GetStockObject(NULL_BRUSH));
  Rectangle(mem, 0, 0, rc.right, rc.bottom);
  SelectObject(mem, oldBr);
  SelectObject(mem, oldPen);
  DeleteObject(pen);

  SetBkMode(mem, TRANSPARENT);

  // 大字：直接显示文字，无框、无编码
  HGDIOBJ oldF = SelectObject(mem, _font);
  SetTextColor(mem, RGB(30, 30, 30));
  std::wstring text = _ComputeText();
  RECT textRc = {_padding, _padding, rc.right - _padding,
                 _padding + _rowHeight};
  DrawTextW(mem, text.c_str(), (int)text.size(), &textRc,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

  // 右上角：整词编码
  SelectObject(mem, _smallFont);
  SetTextColor(mem, RGB(0, 120, 215));
  std::wstring code = _ComputeCode();
  RECT codeRc = {_padding, _padding, rc.right - _padding,
                 _padding + _rowHeight};
  DrawTextW(mem, code.c_str(), (int)code.size(), &codeRc,
            DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

  // 底部提示
  SetTextColor(mem, RGB(140, 140, 140));
  const wchar_t* hint = L"← → 加/减字   Shift+← → 快速   Enter 确认   Esc 取消";
  RECT hintRc = {_padding, rc.bottom - _padding - _hintHeight,
                 rc.right - _padding, rc.bottom - _padding};
  DrawTextW(mem, hint, -1, &hintRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

  SelectObject(mem, oldF);
  BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
  SelectObject(mem, oldB);
  DeleteObject(bmp);
  DeleteDC(mem);
  EndPaint(_hwnd, &ps);
  return 0;
}

LRESULT CPhraseWindow::_OnLButtonUp(LPARAM) {
  // 鼠标点击直接确认（简化交互，符合极点风格）
  _Commit();
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