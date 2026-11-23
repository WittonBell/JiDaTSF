// SettingWindow.cpp
#include "SettingWindow.h"
#include <string>
#include "Config.h"
#include "Globals.h"

// 只允许单实例
static CSettingWindow* s_SettingInstance = nullptr;

namespace {
enum : int {
  ID_AUTO_COMMIT = 1001,
  ID_Z_WILDCARD,
  ID_Z_REPEAT,
  ID_SEMI_SECOND,
  ID_PUNCT_TOP,
  ID_PERKEY_HINT,
  ID_INLINE_PREEDIT,
  ID_RAD_VERTICAL,
  ID_RAD_HORIZONTAL,
  ID_CBO_PAGESIZE,
  ID_CBO_FONTSIZE,
  ID_SHOW_STATUS,
  ID_BTN_RESET,
  ID_BTN_OK,
  ID_BTN_CANCEL,
  ID_BTN_FONT,
  ID_BTN_TEXTCLR,
  ID_BTN_BGCLR,
  ID_BTN_SELCLR,
  ID_CHK_SHIFT_DIRECT
};
constexpr wchar_t kClass[] = L"JiDa.SettingWindow";
}  // namespace

CSettingWindow::CSettingWindow(HINSTANCE inst, HWND owner, ChangeCallback cb)
    : _instance(inst), _owner(owner), _onChange(std::move(cb)) {}

CSettingWindow::~CSettingWindow() {
  if (_font)
    DeleteObject(_font);
}

UINT CSettingWindow::_Dpi() const {
  using Fn = UINT(WINAPI*)(HWND);
  static Fn fn = reinterpret_cast<Fn>(
      GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
  if (fn && _hwnd)
    return fn(_hwnd);
  if (fn && _owner)
    return fn(_owner);
  return 96;
}

void CSettingWindow::Show(HINSTANCE inst, HWND owner, ChangeCallback onChange) {
  if (s_SettingInstance) {
    if (IsWindowVisible(s_SettingInstance->_hwnd))
      SetForegroundWindow(s_SettingInstance->_hwnd);
    else
      ShowWindow(s_SettingInstance->_hwnd, SW_SHOW);
    return;
  }
  auto* sw = new CSettingWindow(inst, owner, std::move(onChange));
  if (!sw->_Create()) {
    delete sw;
    return;
  }
  s_SettingInstance = sw;
  if (owner)
    EnableWindow(owner, FALSE);
}

bool CSettingWindow::_Create() {
  WNDCLASSEXW wc = {sizeof(wc)};
  wc.lpfnWndProc = _WndProc;
  wc.hInstance = _instance;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
  wc.lpszClassName = kClass;
  RegisterClassExW(&wc);

  RECT rc = {0, 0, _S(360), _S(385)};
  AdjustWindowRectEx(&rc, WS_CAPTION | WS_SYSMENU | WS_POPUP, FALSE, 0);
  int winW = rc.right - rc.left;
  int winH = rc.bottom - rc.top;

  // 目标显示器：owner 所在屏 → 前台窗口 → 主屏
  HWND refWnd = _owner ? _owner : GetForegroundWindow();
  HMONITOR hmon =
      refWnd ? MonitorFromWindow(refWnd, MONITOR_DEFAULTTONEAREST) : nullptr;
  if (!hmon)
    hmon = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);

  MONITORINFO mi{sizeof(mi)};
  GetMonitorInfoW(hmon, &mi);

  int workW = mi.rcWork.right - mi.rcWork.left;
  int workH = mi.rcWork.bottom - mi.rcWork.top;

  // 若 work area 装不下，压缩窗口尺寸（内容自适应会被裁，但至少能看到）
  if (winH > workH)
    winH = workH;
  if (winW > workW)
    winW = workW;

  // 居中于 work area，而不是居中于 owner（owner 在右下角时会跑出屏幕）
  int x = mi.rcWork.left + (workW - winW) / 2;
  int y = mi.rcWork.top + (workH - winH) / 2;

  // 兜底
  if (x < mi.rcWork.left)
    x = mi.rcWork.left;
  if (y < mi.rcWork.top)
    y = mi.rcWork.top;
  if (x + winW > mi.rcWork.right)
    x = mi.rcWork.right - winW;
  if (y + winH > mi.rcWork.bottom)
    y = mi.rcWork.bottom - winH;

  _hwnd = CreateWindowExW(0, kClass, L"极达五笔 - 设置",
                          WS_CAPTION | WS_SYSMENU | WS_POPUP, x, y, winW, winH,
                          _owner, nullptr, _instance, this);
  if (!_hwnd)
    return false;

  _Layout();
  _LoadFromConfig();
  ShowWindow(_hwnd, SW_SHOW);
  UpdateWindow(_hwnd);
  return true;
}

HWND CSettingWindow::_MkGroup(const wchar_t* text, int x, int y, int w, int h) {
  return CreateWindowExW(0, L"BUTTON", text,
                         WS_CHILD | WS_VISIBLE | BS_GROUPBOX, _S(x), _S(y),
                         _S(w), _S(h), _hwnd, nullptr, _instance, nullptr);
}

HWND CSettingWindow::_MkCheck(const wchar_t* text,
                              int x,
                              int y,
                              int w,
                              int id,
                              bool checked) {
  HWND h = CreateWindowExW(0, L"BUTTON", text,
                           WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
                           _S(x), _S(y), _S(w), _S(20), _hwnd,
                           (HMENU)(INT_PTR)id, _instance, nullptr);
  SendMessageW(h, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
  return h;
}

HWND CSettingWindow::_MkRadio(const wchar_t* text,
                              int x,
                              int y,
                              int w,
                              int id,
                              bool checked,
                              bool firstInGroup) {
  DWORD style = WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON;
  if (firstInGroup)
    style |= WS_GROUP | WS_TABSTOP;

  HWND h =
      CreateWindowExW(0, L"BUTTON", text, style, _S(x), _S(y), _S(w), _S(20),
                      _hwnd, (HMENU)(INT_PTR)id, _instance, nullptr);
  SendMessageW(h, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
  return h;
}

HWND CSettingWindow::_MkLabel(const wchar_t* text, int x, int y, int w, int h) {
  return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT,
                         _S(x), _S(y), _S(w), _S(h), _hwnd, nullptr, _instance,
                         nullptr);
}

HWND CSettingWindow::_MkButton(const wchar_t* text,
                               int x,
                               int y,
                               int w,
                               int h,
                               int id) {
  return CreateWindowExW(0, L"BUTTON", text,
                         WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
                         _S(x), _S(y), _S(w), _S(h), _hwnd, (HMENU)(INT_PTR)id,
                         _instance, nullptr);
}

HWND CSettingWindow::_MkCombo(int x, int y, int w, int h, int id) {
  return CreateWindowExW(
      0, L"COMBOBOX", L"",
      WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, _S(x),
      _S(y), _S(w), _S(h), _hwnd, (HMENU)(INT_PTR)id, _instance, nullptr);
}

void CSettingWindow::_Layout() {
  if (_font)
    DeleteObject(_font);
  _font = CreateFontW(-_S(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                      CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");

  // ===== 输入选项 =====
  HWND g1 = _MkGroup(L"输入选项", 10, 10, 340, 125);
  _chkAutoCommit =
      _MkCheck(L"四码唯一时自动上屏", 22, 32, 150, ID_AUTO_COMMIT, true);
  _chkZWildcard = _MkCheck(L"Z 键通配", 22, 56, 150, ID_Z_WILDCARD, true);
  _chkZRepeat = _MkCheck(L"Z 键重复上次", 22, 80, 150, ID_Z_REPEAT, true);
  _chkPunctTop = _MkCheck(L"标点顶字上屏", 22, 104, 150, ID_PUNCT_TOP, true);
  _chkSemiSecond =
      _MkCheck(L"分号选第二重码", 185, 32, 150, ID_SEMI_SECOND, true);
  _chkPerKey = _MkCheck(L"逐键提示", 185, 56, 150, ID_PERKEY_HINT, true);
  _chkInline = _MkCheck(L"嵌入预编辑", 185, 80, 150, ID_INLINE_PREEDIT, false);
  _chkShiftDirect =
      _MkCheck(L"Shift 直接上屏", 185, 104, 150, ID_CHK_SHIFT_DIRECT, true);

  // ===== 显示选项 =====
  HWND g2 = _MkGroup(L"显示选项", 10, 145, 340, 140);
  _radVert = _MkRadio(L"竖向候选", 22, 165, 90, ID_RAD_VERTICAL, true, true);
  _radHorz =
      _MkRadio(L"横向候选", 120, 165, 90, ID_RAD_HORIZONTAL, false, false);

  HWND l1 = _MkLabel(L"每页候选数", 22, 195, 80, 20);
  _cboPage = _MkCombo(110, 193, 60, 190, ID_CBO_PAGESIZE);
  for (int v = 3; v <= 9; ++v) {
    wchar_t buf[4];
    swprintf_s(buf, L"%d", v);
    SendMessageW(_cboPage, CB_ADDSTRING, 0, (LPARAM)buf);
  }

  HWND l2 = _MkLabel(L"候选字体", 22, 223, 60, 20);
  _lblFont = _MkLabel(L"", 100, 223, 120, 20);
  _btnFont = _MkButton(L"选择字体...", 250, 218, 86, 24, ID_BTN_FONT);

  _btnTextClr = _MkButton(L"文字色", 22, 250, 70, 24, ID_BTN_TEXTCLR);
  _btnBgClr = _MkButton(L"背景色", 110, 250, 70, 24, ID_BTN_BGCLR);
  _btnSelClr = _MkButton(L"高亮色", 198, 250, 70, 24, ID_BTN_SELCLR);

  HWND g3 = _MkGroup(L"状态栏", 10, 295, 340, 44);
  _chkShowBar = _MkCheck(L"显示状态栏", 22, 312, 150, ID_SHOW_STATUS, true);

  _btnReset = _MkButton(L"恢复默认(&R)", 14, 346, 100, 26, ID_BTN_RESET);
  _btnOk = _MkButton(L"确定(&O)", 245, 346, 100, 26, ID_BTN_OK);
  _btnCancel = _MkButton(L"取消(&C)", 155, 346, 80, 26, ID_BTN_CANCEL);

  // 统一字体
  HWND all[] = {_chkAutoCommit,
                _chkZWildcard,
                _chkZRepeat,
                _chkSemiSecond,
                _chkPunctTop,
                _chkPerKey,
                _chkInline,
                _chkShiftDirect,
                _chkShowBar,
                _radVert,
                _radHorz,
                _cboPage,
                _btnReset,
                _btnOk,
                _btnCancel,
                _lblFont,
                _btnFont,
                _btnTextClr,
                _btnBgClr,
                _btnSelClr,
                g1,
                g2,
                g3,
                l1,
                l2};
  for (HWND h : all)
    SendMessageW(h, WM_SETFONT, (WPARAM)_font, TRUE);
}

void CSettingWindow::_LoadFromConfig() {
  SendMessageW(_chkAutoCommit, BM_SETCHECK, g_config.autoCommitUnique ? 1 : 0,
               0);
  SendMessageW(_chkZWildcard, BM_SETCHECK, g_config.zWildcard ? 1 : 0, 0);
  SendMessageW(_chkZRepeat, BM_SETCHECK, g_config.zRepeat ? 1 : 0, 0);
  SendMessageW(_chkSemiSecond, BM_SETCHECK, g_config.semicolonSecond ? 1 : 0,
               0);
  SendMessageW(_chkPunctTop, BM_SETCHECK, g_config.punctuationTop ? 1 : 0, 0);
  SendMessageW(_chkPerKey, BM_SETCHECK, g_config.perKeyHint ? 1 : 0, 0);
  SendMessageW(_chkInline, BM_SETCHECK, g_config.inlinePreedit ? 1 : 0, 0);
  SendMessageW(_chkShowBar, BM_SETCHECK, g_config.showStatusBar ? 1 : 0, 0);
  SendMessageW(_chkShiftDirect, BM_SETCHECK, g_config.shiftDirectCommit ? 1 : 0,
               0);

  SendMessageW(_radVert, BM_SETCHECK,
               g_config.candLayout == CandLayout::Vertical ? 1 : 0, 0);
  SendMessageW(_radHorz, BM_SETCHECK,
               g_config.candLayout == CandLayout::Horizontal ? 1 : 0, 0);

  wchar_t buf[8];
  swprintf_s(buf, L"%d", g_config.candPageSize);
  SendMessageW(_cboPage, CB_SELECTSTRING, (WPARAM)-1, (LPARAM)buf);
  // 字体标签
  wchar_t label[128];
  swprintf_s(label, L"%ls %d", g_config.candFontFace, g_config.candFontSize);
  SetWindowTextW(_lblFont, label);
}

void CSettingWindow::_SaveToConfig() {
  g_config.autoCommitUnique =
      SendMessageW(_chkAutoCommit, BM_GETCHECK, 0, 0) == BST_CHECKED;
  g_config.zWildcard =
      SendMessageW(_chkZWildcard, BM_GETCHECK, 0, 0) == BST_CHECKED;
  g_config.zRepeat =
      SendMessageW(_chkZRepeat, BM_GETCHECK, 0, 0) == BST_CHECKED;
  g_config.semicolonSecond =
      SendMessageW(_chkSemiSecond, BM_GETCHECK, 0, 0) == BST_CHECKED;
  g_config.punctuationTop =
      SendMessageW(_chkPunctTop, BM_GETCHECK, 0, 0) == BST_CHECKED;
  g_config.perKeyHint =
      SendMessageW(_chkPerKey, BM_GETCHECK, 0, 0) == BST_CHECKED;
  g_config.inlinePreedit =
      SendMessageW(_chkInline, BM_GETCHECK, 0, 0) == BST_CHECKED;
  g_config.showStatusBar =
      SendMessageW(_chkShowBar, BM_GETCHECK, 0, 0) == BST_CHECKED;
  g_config.shiftDirectCommit =
      SendMessageW(_chkShiftDirect, BM_GETCHECK, 0, 0) == BST_CHECKED;

  bool vert = SendMessageW(_radVert, BM_GETCHECK, 0, 0) == BST_CHECKED;
  g_config.candLayout = vert ? CandLayout::Vertical : CandLayout::Horizontal;

  int idx = (int)SendMessageW(_cboPage, CB_GETCURSEL, 0, 0);
  if (idx >= 0)
    g_config.candPageSize = 3 + idx;
}

void CSettingWindow::_ResetToDefault() {
  JiDaConfig d;
  SendMessageW(_chkAutoCommit, BM_SETCHECK, d.autoCommitUnique ? 1 : 0, 0);
  SendMessageW(_chkZWildcard, BM_SETCHECK, d.zWildcard ? 1 : 0, 0);
  SendMessageW(_chkZRepeat, BM_SETCHECK, d.zRepeat ? 1 : 0, 0);
  SendMessageW(_chkSemiSecond, BM_SETCHECK, d.semicolonSecond ? 1 : 0, 0);
  SendMessageW(_chkPunctTop, BM_SETCHECK, d.punctuationTop ? 1 : 0, 0);
  SendMessageW(_chkPerKey, BM_SETCHECK, d.perKeyHint ? 1 : 0, 0);
  SendMessageW(_chkInline, BM_SETCHECK, d.inlinePreedit ? 1 : 0, 0);
  SendMessageW(_chkShowBar, BM_SETCHECK, d.showStatusBar ? 1 : 0, 0);
  SendMessageW(_radVert, BM_SETCHECK,
               d.candLayout == CandLayout::Vertical ? 1 : 0, 0);
  SendMessageW(_radHorz, BM_SETCHECK,
               d.candLayout == CandLayout::Horizontal ? 1 : 0, 0);
  SendMessageW(_chkShiftDirect, BM_SETCHECK, d.shiftDirectCommit ? 1 : 0, 0);

  wchar_t buf[8];
  swprintf_s(buf, L"%d", d.candPageSize);
  SendMessageW(_cboPage, CB_SELECTSTRING, (WPARAM)-1, (LPARAM)buf);
}

void CSettingWindow::_Close(bool save) {
  if (save) {
    _SaveToConfig();
    SaveJiDaConfig();
    if (_onChange)
      _onChange();
  }
  DestroyWindow(_hwnd);
}

LRESULT CALLBACK CSettingWindow::_WndProc(HWND h,
                                          UINT msg,
                                          WPARAM w,
                                          LPARAM l) {
  CSettingWindow* self =
      reinterpret_cast<CSettingWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
  if (msg == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(l);
    self = static_cast<CSettingWindow*>(cs->lpCreateParams);
    SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    self->_hwnd = h;
  }
  return self ? self->_Handle(msg, w, l) : DefWindowProcW(h, msg, w, l);
}

LRESULT CSettingWindow::_Handle(UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_COMMAND: {
      switch (LOWORD(wParam)) {
        case ID_BTN_OK:
          _Close(true);
          return 0;
        case ID_BTN_CANCEL:
          _Close(false);
          return 0;
        case ID_BTN_FONT: {
          LOGFONTW lf = {};
          wcscpy_s(lf.lfFaceName, g_config.candFontFace);
          HDC hdc = GetDC(_hwnd);
          int dpi = GetDeviceCaps(hdc, LOGPIXELSY);
          ReleaseDC(_hwnd, hdc);
          lf.lfHeight = -MulDiv(g_config.candFontSize, dpi, 72);
          lf.lfWeight = g_config.candFontWeight;
          lf.lfItalic = g_config.candFontItalic ? TRUE : FALSE;

          CHOOSEFONTW cf = {};
          cf.lStructSize = sizeof(cf);
          cf.hwndOwner = _hwnd;
          cf.lpLogFont = &lf;
          cf.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT | CF_EFFECTS;
          cf.rgbColors = g_config.candTextColor;

          if (ChooseFontW(&cf)) {
            wcscpy_s(g_config.candFontFace, lf.lfFaceName);
            g_config.candFontSize = cf.iPointSize / 10;
            if (g_config.candFontSize < 8)
              g_config.candFontSize = 8;
            if (g_config.candFontSize > 48)
              g_config.candFontSize = 48;
            g_config.candFontWeight = lf.lfWeight;
            g_config.candFontItalic = lf.lfItalic != 0;
            g_config.candTextColor = cf.rgbColors;

            wchar_t label[128];
            swprintf_s(label, L"%ls %d", g_config.candFontFace,
                       g_config.candFontSize);
            SetWindowTextW(_lblFont, label);
          }
          return 0;
        }
        case ID_BTN_TEXTCLR: {
          static COLORREF cust[16] = {};
          CHOOSECOLORW cc = {};
          cc.lStructSize = sizeof(cc);
          cc.hwndOwner = _hwnd;
          cc.lpCustColors = cust;
          cc.rgbResult = g_config.candTextColor;
          cc.Flags = CC_FULLOPEN | CC_RGBINIT;
          if (ChooseColorW(&cc))
            g_config.candTextColor = cc.rgbResult;
          return 0;
        }
        case ID_BTN_BGCLR: {
          static COLORREF cust[16] = {};
          CHOOSECOLORW cc = {};
          cc.lStructSize = sizeof(cc);
          cc.hwndOwner = _hwnd;
          cc.lpCustColors = cust;
          cc.rgbResult = g_config.candBgColor;
          cc.Flags = CC_FULLOPEN | CC_RGBINIT;
          if (ChooseColorW(&cc))
            g_config.candBgColor = cc.rgbResult;
          return 0;
        }
        case ID_BTN_SELCLR: {
          static COLORREF cust[16] = {};
          CHOOSECOLORW cc = {};
          cc.lStructSize = sizeof(cc);
          cc.hwndOwner = _hwnd;
          cc.lpCustColors = cust;
          cc.rgbResult = g_config.candSelBgColor;
          cc.Flags = CC_FULLOPEN | CC_RGBINIT;
          if (ChooseColorW(&cc)) {
            g_config.candSelBgColor = cc.rgbResult;
            // 自动计算高亮文字色：暗背景用白字，亮背景用黑字
            int r = GetRValue(cc.rgbResult);
            int g = GetGValue(cc.rgbResult);
            int b = GetBValue(cc.rgbResult);
            int luminance = (r * 299 + g * 587 + b * 114) / 1000;
            g_config.candSelTextColor =
                (luminance < 128) ? RGB(255, 255, 255) : RGB(0, 0, 0);
          }
          return 0;
        }
        case ID_BTN_RESET:
          _ResetToDefault();
          return 0;
      }
      return 0;
    }
    case WM_CLOSE:
      _Close(false);
      return 0;

    case WM_NCDESTROY: {
      HWND h = _hwnd;
      SetWindowLongPtrW(h, GWLP_USERDATA, 0);
      _hwnd = nullptr;
      if (_owner && IsWindow(_owner))
        EnableWindow(_owner, TRUE);
      if (s_SettingInstance == this)
        s_SettingInstance = nullptr;
      delete this;  // 自删除，不走 PostQuitMessage
      return DefWindowProcW(h, msg, wParam, lParam);
    }
  }
  return DefWindowProcW(_hwnd, msg, wParam, lParam);
}