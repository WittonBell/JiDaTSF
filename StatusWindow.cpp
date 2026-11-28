#include "StatusWindow.h"
// 状态栏实现：绘制中英文、标点、全半角等状态按钮及悬浮位置逻辑。
#include "Globals.h"

#include <commctrl.h>
#include <strsafe.h>
#include <windowsx.h>
#include <utility>

const wchar_t* CStatusWindow::WindowClassName = L"TSF.CustomInputStatusWindow";

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")

namespace {
constexpr COLORREF kWindowBg = RGB(245, 246, 248);
constexpr COLORREF kBorder = RGB(175, 180, 188);
constexpr COLORREF kText = RGB(45, 48, 54);
constexpr COLORREF kMutedText = RGB(105, 110, 120);
constexpr COLORREF kButtonBg = RGB(255, 255, 255);
constexpr COLORREF kButtonHot = RGB(226, 237, 252);
constexpr COLORREF kButtonActive = RGB(211, 229, 251);
constexpr COLORREF kButtonBorder = RGB(205, 210, 218);
constexpr COLORREF kAccent = RGB(35, 105, 190);

constexpr int kBaseHeight = 34;
constexpr int kBaseTitleWidth = 42;
constexpr int kBaseButtonWidth = 34;
constexpr int kBaseGap = 3;
constexpr int kBaseMargin = 5;

void DrawFrameRect(HDC dc, const RECT& rc, COLORREF color) {
  // 绘制无填充边框，用于状态按钮和窗口轮廓。
  HPEN pen = CreatePen(PS_SOLID, 1, color);
  HGDIOBJ oldPen = SelectObject(dc, pen);
  HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
  Rectangle(dc, rc.left, rc.top, rc.right, rc.bottom);
  SelectObject(dc, oldBrush);
  SelectObject(dc, oldPen);
  DeleteObject(pen);
}
}  // namespace

CStatusWindow::~CStatusWindow() {
  Destroy();
}

bool CStatusWindow::Create(HINSTANCE instance,
                           HWND owner,
                           const wchar_t* imeName,
                           const InputStatus& initialStatus,
                           CommandHandler handler,
                           ActiveProfileCheck activeProfileCheck,
                           int x,
                           int y) {
  // 创建状态条、提示框、布局、位置并按配置决定是否显示。
  if (_hwnd)
    return true;

  _instance = instance;
  _owner = owner;
  _handler = std::move(handler);
  _activeProfileCheck = std::move(activeProfileCheck);
  if (_activeProfileCheck)
    _inputMethodActive = _activeProfileCheck();
  _status = initialStatus;

  if (imeName && *imeName)
    StringCchCopyW(_imeName, _countof(_imeName), imeName);

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = StaticWndProc;
  wc.hInstance = _instance;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = nullptr;
  wc.lpszClassName = WindowClassName;
  if (!RegisterClassExW(&wc)) {
    if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
      return false;
  }

  const DWORD exStyle = WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE;

  // 1) 创建（初始不可见，避免闪烁）
  _hwnd = CreateWindowExW(exStyle, WindowClassName, _imeName, WS_POPUP, 0, 0,
                          100, 100, _owner, nullptr, _instance, this);
  if (!_hwnd) {
    OutputDebugStringW(L"[JiDa] StatusWindow: CreateWindowExW failed\n");
    return false;
  }

  _created = true;

  // ---- 创建 tooltip 窗口 ----
  _tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                             WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
                             CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                             CW_USEDEFAULT, _hwnd, nullptr, _instance, nullptr);
  if (_tooltip) {
    SetWindowPos(_tooltip, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SendMessageW(_tooltip, TTM_SETMAXTIPWIDTH, 0, 400);
    SendMessageW(_tooltip, TTM_SETDELAYTIME, TTDT_INITIAL, 300);
    SendMessageW(_tooltip, TTM_SETDELAYTIME, TTDT_RESHOW, 100);
    SendMessageW(_tooltip, TTM_SETDELAYTIME, TTDT_AUTOPOP, 15000);
  }

  RebuildLayout();

  // 2) 取真实尺寸
  RECT wr{};
  GetWindowRect(_hwnd, &wr);
  int realW = wr.right - wr.left;
  int realH = wr.bottom - wr.top;
  if (realW <= 0 || realH <= 0) {
    realW = Scale(220);
    realH = Scale(kBaseHeight);
  }

  // 3) 目标显示器
  HWND refWnd = _owner ? _owner : GetForegroundWindow();
  HMONITOR hmon = refWnd
                      ? MonitorFromWindow(refWnd, MONITOR_DEFAULTTONEAREST)
                      : MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
  if (!hmon)
    hmon = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);

  MONITORINFO mi{sizeof(mi)};
  int workL = 0, workT = 0, workR = GetSystemMetrics(SM_CXSCREEN),
      workB = GetSystemMetrics(SM_CYSCREEN);
  if (GetMonitorInfoW(hmon, &mi)) {
    workL = mi.rcWork.left;
    workT = mi.rcWork.top;
    workR = mi.rcWork.right;
    workB = mi.rcWork.bottom;
  }

  // 4) 位置计算（严格限制在 work area 内）
  if (x == CW_USEDEFAULT)
    x = workR - realW;  // 向左移动多少 -Scale(16);
  if (y == CW_USEDEFAULT)
    y = workB - realH;  // 向上移动多少 - Scale(10);

  if (x < workL)
    x = workL;
  if (y < workT)
    y = workT;
  if (x + realW > workR)
    x = workR - realW;
  if (y + realH > workB)
    y = workB - realH;

  // 5) 显示（SWP_SHOWWINDOW 保证窗口真实可见）
  const UINT showFlags =
      _inputMethodActive && g_config.showStatusBar ? SWP_SHOWWINDOW : 0;
  SetWindowPos(_hwnd, HWND_TOPMOST, x, y, realW, realH,
               SWP_NOACTIVATE | showFlags);
  UpdateWindow(_hwnd);

  // 6) 日志：记录创建成功及坐标尺寸
  wchar_t dbg[200];
  swprintf_s(dbg, L"[JiDa] StatusWindow created at (%d,%d) size (%dx%d)\n", x,
             y, realW, realH);
  OutputDebugStringW(dbg);

  // 7) 定时器：仅在成为前台时"补显示"，不再主动隐藏
  SetTimer(_hwnd, 1, 300, nullptr);

  return true;
}

void CStatusWindow::Destroy() {
  if (_tooltip) {
    DestroyWindow(_tooltip);
    _tooltip = nullptr;
  }

  if (_hwnd) {
    HWND hwnd = _hwnd;
    _hwnd = nullptr;
    DestroyWindow(hwnd);
  }

  if (_font) {
    DeleteObject(_font);
    _font = nullptr;
  }
  if (_titleFont) {
    DeleteObject(_titleFont);
    _titleFont = nullptr;
  }

  _created = false;
}

void CStatusWindow::Show() {
  if (!_hwnd || !_inputMethodActive || !g_config.showStatusBar)
    return;
  ShowWindow(_hwnd, SW_SHOWNOACTIVATE);
}

void CStatusWindow::Hide() {
  if (!_hwnd)
    return;
  ShowWindow(_hwnd, SW_HIDE);
}

void CStatusWindow::SetInputMethodActive(bool active) {
  _inputMethodActive = active;
  if (!active)
    Hide();
}

void CStatusWindow::UpdateStatus(const InputStatus& status) {
  _status = status;
  if (_hwnd) {
    RebuildLayout();
    InvalidateRect(_hwnd, nullptr, TRUE);
  }
}

bool CStatusWindow::GetPosition(POINT* point) const {
  if (!_hwnd || !point)
    return false;

  RECT rc{};
  if (!GetWindowRect(_hwnd, &rc))
    return false;

  point->x = rc.left;
  point->y = rc.top;
  return true;
}

void CStatusWindow::SetPosition(int x, int y) {
  if (!_hwnd)
    return;

  RECT wr{};
  GetWindowRect(_hwnd, &wr);
  int w = wr.right - wr.left;
  int h = wr.bottom - wr.top;

  HMONITOR hmon =
      MonitorFromPoint(POINT{x + w / 2, y + h / 2}, MONITOR_DEFAULTTONEAREST);
  MONITORINFO mi{sizeof(mi)};
  if (GetMonitorInfoW(hmon, &mi)) {
    if (x + w > mi.rcWork.right)
      x = mi.rcWork.right - w;
    if (y + h > mi.rcWork.bottom)
      y = mi.rcWork.bottom - h;
    if (x < mi.rcWork.left)
      x = mi.rcWork.left;
    if (y < mi.rcWork.top)
      y = mi.rcWork.top;
  }

  SetWindowPos(_hwnd, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
}

UINT CStatusWindow::Dpi() const {
  if (_hwnd) {
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    auto fn = reinterpret_cast<GetDpiForWindowFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    if (fn)
      return fn(_hwnd);
  }
  return 96;
}

int CStatusWindow::Scale(int value) const {
  return MulDiv(value, static_cast<int>(Dpi()), 96);
}

void CStatusWindow::RebuildLayout() {
  // 按 DPI 缩放标题、按钮和间距，并重新计算窗口尺寸。
  if (!_hwnd)
    return;

  const int margin = Scale(kBaseMargin);
  const int gap = Scale(kBaseGap);
  const int titleWidth = Scale(kBaseTitleWidth);
  const int buttonWidth = Scale(kBaseButtonWidth);
  const int height = Scale(kBaseHeight);

  int x = margin + titleWidth + gap;
  const int top = Scale(4);
  const int bottom = height - Scale(4);

  const wchar_t* labels[] = {_status.chinese ? L"中" : L"英",
                             _status.fullShape ? L"全" : L"半",
                             _status.chinesePunctuation ? L"标" : L"符",
                             _status.traditional ? L"繁" : L"简", L"设"};

  const StatusCommand commands[] = {
      StatusCommand::ToggleChinese, StatusCommand::ToggleFullShape,
      StatusCommand::TogglePunctuation, StatusCommand::ToggleTraditional,
      StatusCommand::OpenSettings};

  _buttonCount = 5;
  for (int i = 0; i < _buttonCount; ++i) {
    Button& b = _buttons[i];
    b.rect = {x, top, x + buttonWidth, bottom};
    b.command = commands[i];
    StringCchCopyW(b.text, _countof(b.text), labels[i]);
    x += buttonWidth + gap;
  }

  if (_font)
    DeleteObject(_font);
  if (_titleFont)
    DeleteObject(_titleFont);

  _font = CreateFontW(-Scale(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                      L"Microsoft YaHei");

  _titleFont = CreateFontW(-Scale(14), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");

  if (_hwnd) {
    const int width = x - gap + margin;
    SetWindowPos(_hwnd, nullptr, 0, 0, width, height,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  }

  _RebuildTooltips();
}

void CStatusWindow::Paint() {
  PAINTSTRUCT ps{};
  HDC hdc = BeginPaint(_hwnd, &ps);

  RECT client{};
  GetClientRect(_hwnd, &client);

  HDC mem = CreateCompatibleDC(hdc);
  HBITMAP bitmap = CreateCompatibleBitmap(hdc, client.right - client.left,
                                          client.bottom - client.top);
  HGDIOBJ oldBitmap = SelectObject(mem, bitmap);

  HBRUSH bg = CreateSolidBrush(kWindowBg);
  FillRect(mem, &client, bg);
  DeleteObject(bg);

  DrawFrameRect(mem, client, kBorder);

  RECT titleRc{Scale(kBaseMargin), 0, Scale(kBaseMargin + kBaseTitleWidth),
               client.bottom};

  SetBkMode(mem, TRANSPARENT);
  SetTextColor(mem, kAccent);
  HGDIOBJ oldFont = SelectObject(
      mem, _titleFont ? _titleFont : GetStockObject(DEFAULT_GUI_FONT));
  DrawTextW(mem, _imeName, -1, &titleRc,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
  SelectObject(mem, oldFont);

  for (int i = 0; i < _buttonCount; ++i)
    DrawButton(mem, _buttons[i], i == _hotButton);

  BitBlt(hdc, 0, 0, client.right, client.bottom, mem, 0, 0, SRCCOPY);

  SelectObject(mem, oldBitmap);
  DeleteObject(bitmap);
  DeleteDC(mem);
  EndPaint(_hwnd, &ps);
}

void CStatusWindow::DrawButton(HDC dc, const Button& button, bool hot) const {
  bool active = false;
  switch (button.command) {
    case StatusCommand::ToggleChinese:
      active = _status.chinese;
      break;
    case StatusCommand::ToggleFullShape:
      active = _status.fullShape;
      break;
    case StatusCommand::TogglePunctuation:
      active = _status.chinesePunctuation;
      break;
    case StatusCommand::ToggleTraditional:
      active = _status.traditional;
      break;
    default:
      break;
  }

  COLORREF fill = hot ? kButtonHot : (active ? kButtonActive : kButtonBg);
  HBRUSH brush = CreateSolidBrush(fill);
  FillRect(dc, &button.rect, brush);
  DeleteObject(brush);
  DrawFrameRect(dc, button.rect, hot ? kAccent : kButtonBorder);

  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, kText);
  HGDIOBJ oldFont =
      SelectObject(dc, _font ? _font : GetStockObject(DEFAULT_GUI_FONT));
  RECT textRc = button.rect;
  DrawTextW(dc, button.text, -1, &textRc,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  SelectObject(dc, oldFont);
}

int CStatusWindow::HitTestButton(POINT point) const {
  for (int i = 0; i < _buttonCount; ++i) {
    if (PtInRect(&_buttons[i].rect, point))
      return i;
  }
  return -1;
}

void CStatusWindow::Invoke(StatusCommand command) {
  if (_handler)
    _handler(command);
}

void CStatusWindow::ShowContextMenu(POINT screenPoint) {
  HMENU menu = CreatePopupMenu();
  if (!menu)
    return;

  AppendMenuW(menu, MF_STRING | (_status.chinese ? MF_CHECKED : 0), 1001,
              _status.chinese ? L"中文输入" : L"英文输入");
  AppendMenuW(menu, MF_STRING | (_status.fullShape ? MF_CHECKED : 0), 1002,
              _status.fullShape ? L"全角" : L"半角");
  AppendMenuW(menu, MF_STRING | (_status.chinesePunctuation ? MF_CHECKED : 0),
              1003, _status.chinesePunctuation ? L"中文标点" : L"英文标点");
  AppendMenuW(menu, MF_STRING | (_status.traditional ? MF_CHECKED : 0), 1004,
              _status.traditional ? L"繁体" : L"简体");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, 1005, L"设置...");
  AppendMenuW(menu, MF_STRING, 1006, L"隐藏状态栏");

  const UINT cmd =
      TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                     screenPoint.x, screenPoint.y, 0, _hwnd, nullptr);

  DestroyMenu(menu);

  switch (cmd) {
    case 1001:
      Invoke(StatusCommand::ToggleChinese);
      break;
    case 1002:
      Invoke(StatusCommand::ToggleFullShape);
      break;
    case 1003:
      Invoke(StatusCommand::TogglePunctuation);
      break;
    case 1004:
      Invoke(StatusCommand::ToggleTraditional);
      break;
    case 1005:
      Invoke(StatusCommand::OpenSettings);
      break;
    case 1006:
      Invoke(StatusCommand::HideStatusWindow);
      break;
    default:
      break;
  }
}

LRESULT CALLBACK CStatusWindow::StaticWndProc(HWND hwnd,
                                              UINT msg,
                                              WPARAM wParam,
                                              LPARAM lParam) {
  CStatusWindow* self =
      reinterpret_cast<CStatusWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

  if (msg == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
    self = static_cast<CStatusWindow*>(cs->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    self->_hwnd = hwnd;
  }

  return self ? self->WndProc(msg, wParam, lParam)
              : DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CStatusWindow::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_MOUSEACTIVATE:
      return MA_NOACTIVATE;

    case WM_ERASEBKGND:
      return 1;

    case WM_PAINT:
      Paint();
      return 0;

    case WM_MOUSEMOVE: {
      if (!_trackingMouse) {
        TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, _hwnd, 0};
        TrackMouseEvent(&tme);
        _trackingMouse = true;
      }

      POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
      const int hit = HitTestButton(pt);
      if (hit != _hotButton) {
        _hotButton = hit;
        InvalidateRect(_hwnd, nullptr, FALSE);
      }

      // 转发给 tooltip，由系统处理命中、延时、显示
      if (_tooltip) {
        MSG msg{_hwnd, WM_MOUSEMOVE, wParam, lParam};
        SendMessageW(_tooltip, TTM_RELAYEVENT, 0, (LPARAM)&msg);
      }
      return 0;
    }

    case WM_MOUSELEAVE:
      _trackingMouse = false;
      if (_pressedButton < 0)
        _hotButton = -1;
      InvalidateRect(_hwnd, nullptr, FALSE);
      if (_tooltip)
        SendMessageW(_tooltip, TTM_POP, 0, 0);
      return 0;

    case WM_LBUTTONDOWN: {
      POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
      const int hit = HitTestButton(pt);
      if (hit >= 0) {
        SetCapture(_hwnd);
        _pressedButton = hit;
        _hotButton = hit;
        InvalidateRect(_hwnd, nullptr, FALSE);
      } else {
        // Drag the blank/title area without activating the window.
        ReleaseCapture();
        SendMessageW(_hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
      }
      return 0;
    }

    case WM_LBUTTONUP: {
      const int pressed = _pressedButton;
      POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
      const int released = HitTestButton(pt);
      _pressedButton = -1;
      if (GetCapture() == _hwnd)
        ReleaseCapture();

      if (pressed >= 0 && pressed == released)
        Invoke(_buttons[pressed].command);

      _hotButton = released;
      InvalidateRect(_hwnd, nullptr, FALSE);
      return 0;
    }

    case WM_CAPTURECHANGED:
      _pressedButton = -1;
      return 0;

    case WM_RBUTTONUP: {
      POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
      ClientToScreen(_hwnd, &pt);
      ShowContextMenu(pt);
      return 0;
    }

    case WM_DPICHANGED: {
      const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
      SetWindowPos(_hwnd, HWND_TOPMOST, suggested->left, suggested->top,
                   suggested->right - suggested->left,
                   suggested->bottom - suggested->top, SWP_NOACTIVATE);
      RebuildLayout();
      InvalidateRect(_hwnd, nullptr, TRUE);
      return 0;
    }
    case WM_TIMER:
      if (wParam == 1 && _hwnd) {
        static bool lastVisible = true;
        if (_activeProfileCheck)
          _inputMethodActive = _activeProfileCheck();

        HWND fg = GetForegroundWindow();
        DWORD fgPid = 0;
        if (fg)
          GetWindowThreadProcessId(fg, &fgPid);
        const bool isFg = (fgPid == GetCurrentProcessId());
        const bool vis = IsWindowVisible(_hwnd) != FALSE;

        if ((!isFg || !_inputMethodActive || !g_config.showStatusBar) &&
            vis) {
          // 失去前台、极达输入法未激活或用户关闭状态栏时隐藏
          ShowWindow(_hwnd, SW_HIDE);
        } else if (isFg && _inputMethodActive && !vis &&
                   g_config.showStatusBar) {
          // 本进程位于前台且极达输入法激活时显示
          ShowWindow(_hwnd, SW_SHOWNOACTIVATE);
        }

        const bool nowVis = IsWindowVisible(_hwnd) != FALSE;
        if (nowVis != lastVisible) {
          lastVisible = nowVis;
          wchar_t dbg[200];
          swprintf_s(dbg,
                     L"[JiDa] StatusBar visibility changed: fgPid=%u "
                     L"selfPid=%u isFg=%d vis=%d cfg=%d\n",
                     fgPid, GetCurrentProcessId(), isFg ? 1 : 0, nowVis ? 1 : 0,
                     g_config.showStatusBar ? 1 : 0);
          OutputDebugStringW(dbg);
        }
      }
      return 0;
    case WM_NCDESTROY: {
      KillTimer(_hwnd, 1);
      HWND hwnd = _hwnd;
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
      _hwnd = nullptr;
      return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    case WM_NOTIFY: {
      LPNMHDR hdr = reinterpret_cast<LPNMHDR>(lParam);
      if (hdr && hdr->hwndFrom == _tooltip && hdr->code == TTN_SHOW) {
        RECT rcStatus;
        GetWindowRect(_hwnd, &rcStatus);
        RECT rcTip;
        GetWindowRect(_tooltip, &rcTip);

        int tipW = rcTip.right - rcTip.left;
        int tipH = rcTip.bottom - rcTip.top;

        // 水平居中于状态栏
        int x = rcStatus.left + ((rcStatus.right - rcStatus.left) - tipW) / 2;
        // 默认放状态栏上方，间隔 6px
        int y = rcStatus.top - tipH - 6;

        // 约束在所在显示器 work area 内
        HMONITOR hmon = MonitorFromWindow(_hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = {sizeof(mi)};
        GetMonitorInfo(hmon, &mi);

        if (y < mi.rcWork.top) {
          // 上方放不下，翻到状态栏下方
          y = rcStatus.bottom + 6;
        }
        if (x < mi.rcWork.left)
          x = mi.rcWork.left;
        if (x + tipW > mi.rcWork.right)
          x = mi.rcWork.right - tipW;

        SetWindowPos(_tooltip, HWND_TOPMOST, x, y, 0, 0,
                     SWP_NOSIZE | SWP_NOACTIVATE);
        return TRUE;  // 阻止标准 tooltip 的默认定位
      }
      break;
    }
    default:
      break;
  }
  return DefWindowProcW(_hwnd, msg, wParam, lParam);
}

std::wstring CStatusWindow::_TooltipText(int index) const {
  if (index < 0 || index >= _buttonCount)
    return L"";
  switch (_buttons[index].command) {
    case StatusCommand::ToggleChinese: {
      std::wstring text = _status.chinese ? L"中文输入（点击"
                                          : L"英文输入（点击";
      const wchar_t* shortcut = nullptr;
      switch (g_config.chineseToggleShortcut) {
        case ChineseToggleShortcut::LeftShift:
          shortcut = L"左 Shift";
          break;
        case ChineseToggleShortcut::RightShift:
          shortcut = L"右 Shift";
          break;
        case ChineseToggleShortcut::Shift:
          shortcut = L"Shift";
          break;
        case ChineseToggleShortcut::LeftControl:
          shortcut = L"左 Ctrl";
          break;
        case ChineseToggleShortcut::RightControl:
          shortcut = L"右 Ctrl";
          break;
        case ChineseToggleShortcut::Control:
          shortcut = L"Ctrl";
          break;
        default:
          break;
      }
      if (shortcut) {
        text += L"或按";
        text += shortcut;
        text += L" ";
      }
      text += _status.chinese ? L"切换到英文）" : L"切换到中文）";
      return text;
    }
    case StatusCommand::ToggleFullShape:
      return _status.fullShape ? L"全角（点击或按 Shift+空格 切换到半角）"
                               : L"半角（点击或按 Shift+空格 切换到全角）";
    case StatusCommand::TogglePunctuation:
      return _status.chinesePunctuation
                 ? L"中文标点（点击或按 Ctrl+. 切换到英文标点）"
                 : L"英文标点（点击或按 Ctrl+. 切换到中文标点）";
    case StatusCommand::ToggleTraditional:
      return _status.traditional ? L"繁体输出（点击切换到简体）"
                                 : L"简体输出（点击切换到繁体）";
    case StatusCommand::OpenSettings:
      return L"设置...";
    default:
      return L"";
  }
}

void CStatusWindow::_RebuildTooltips() {
  if (!_tooltip || !_hwnd)
    return;

  for (int i = 0; i < _buttonCount; ++i) {
    TOOLINFOW ti = {sizeof(ti)};
    ti.hwnd = _hwnd;
    ti.uId = (UINT_PTR)(i + 1);  // uId 从 1 开始，0 保留
    ti.rect = _buttons[i].rect;
    ti.uFlags = TTF_SUBCLASS | TTF_TRANSPARENT;

    std::wstring text = _TooltipText(i);
    ti.lpszText = const_cast<LPWSTR>(text.c_str());

    // 先尝试更新已存在的 tool
    if (!SendMessageW(_tooltip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&ti)) {
      // 不存在则新增
      SendMessageW(_tooltip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
    }
    // 同步区域（布局变化时按钮位置会变）
    SendMessageW(_tooltip, TTM_NEWTOOLRECTW, 0, (LPARAM)&ti);
  }
}