#include "SettingWindow.h"
#include <commctrl.h>
#include <commdlg.h>
#include "Config.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")

static CSettingWindow* s_SettingInstance = nullptr;

namespace {
enum PageId {
  PAGE_INPUT = 1,
  PAGE_CANDIDATES,
  PAGE_APPEARANCE,
  PAGE_STATUS,
  PAGE_CHARACTER_SET
};

enum ControlId {
  ID_Z_WILDCARD = 1001,
  ID_Z_REPEAT,
  ID_SEMI_SECOND,
  ID_PUNCT_TOP,
  ID_PERKEY_HINT,
  ID_INLINE_PREEDIT,
  ID_SHIFT_DIRECT,
  ID_RAD_VERTICAL,
  ID_RAD_HORIZONTAL,
  ID_CBO_PAGESIZE,
  ID_CBO_CHARACTER_SET,
  ID_SHOW_STATUS,
  ID_BTN_RESET,
  ID_BTN_OK,
  ID_BTN_CANCEL,
  ID_BTN_FONT,
  ID_BTN_TEXTCLR,
  ID_BTN_BGCLR,
  ID_BTN_SELCLR
};

constexpr wchar_t kClass[] = L"JiDa.SettingWindow";
constexpr wchar_t kPageClass[] = L"JiDa.SettingPage";
constexpr int kClientWidth = 780;
constexpr int kClientHeight = 530;

struct SettingCategory {
  const wchar_t* name;
  int parentIndex;
  int pageId;
};

constexpr SettingCategory kCategories[] = {
    {L"输入法", -1, 0},
    {L"候选窗口", -1, 0},
    {L"输入选项", 0, PAGE_INPUT},
    {L"字库版本", 0, PAGE_CHARACTER_SET},
    {L"候选列表", 1, PAGE_CANDIDATES},
    {L"外观", 1, PAGE_APPEARANCE},
    {L"状态栏", -1, PAGE_STATUS},
};

void SetCheck(HWND control, bool value) {
  SendMessageW(control, BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0);
}

bool IsChecked(HWND control) {
  return SendMessageW(control, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

bool IsKnownCharacterSetVersion(int version) {
  return version >= static_cast<int>(CharacterSetVersion::GB2312) &&
         version <= static_cast<int>(CharacterSetVersion::GB18030_2005);
}

void SelectComboData(HWND combo, int value) {
  int count = (int)SendMessageW(combo, CB_GETCOUNT, 0, 0);
  for (int i = 0; i < count; ++i) {
    if ((int)SendMessageW(combo, CB_GETITEMDATA, i, 0) == value) {
      SendMessageW(combo, CB_SETCURSEL, i, 0);
      return;
    }
  }
  SendMessageW(combo, CB_SETCURSEL, (WPARAM)-1, 0);
}

HTREEITEM InsertTreeItem(HWND tree,
                         HTREEITEM parent,
                         const wchar_t* text,
                         int pageId) {
  TVINSERTSTRUCTW insert = {};
  insert.hParent = parent;
  insert.hInsertAfter = TVI_LAST;
  insert.item.mask = TVIF_TEXT | TVIF_PARAM;
  insert.item.pszText = const_cast<LPWSTR>(text);
  insert.item.lParam = pageId;
  return TreeView_InsertItem(tree, &insert);
}

HTREEITEM FindFirstPageItem(HWND tree, HTREEITEM item) {
  while (item) {
    TVITEMW data = {};
    data.mask = TVIF_PARAM;
    data.hItem = item;
    if (TreeView_GetItem(tree, &data) && data.lParam != 0)
      return item;
    HTREEITEM page = FindFirstPageItem(tree, TreeView_GetChild(tree, item));
    if (page)
      return page;
    item = TreeView_GetNextSibling(tree, item);
  }
  return nullptr;
}
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
  INITCOMMONCONTROLSEX controls = {sizeof(controls), ICC_TREEVIEW_CLASSES};
  if (!InitCommonControlsEx(&controls))
    return false;

  WNDCLASSEXW wc = {sizeof(wc)};
  wc.lpfnWndProc = _WndProc;
  wc.hInstance = _instance;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
  wc.lpszClassName = kClass;
  RegisterClassExW(&wc);

  WNDCLASSEXW pageClass = {sizeof(pageClass)};
  pageClass.lpfnWndProc = _PageWndProc;
  pageClass.hInstance = _instance;
  pageClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  pageClass.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
  pageClass.lpszClassName = kPageClass;
  RegisterClassExW(&pageClass);

  RECT rc = {0, 0, _S(kClientWidth), _S(kClientHeight)};
  DWORD style = WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_CLIPCHILDREN;
  AdjustWindowRectEx(&rc, style, FALSE, 0);
  int winW = rc.right - rc.left;
  int winH = rc.bottom - rc.top;

  HWND refWnd = _owner ? _owner : GetForegroundWindow();
  HMONITOR hmon =
      refWnd ? MonitorFromWindow(refWnd, MONITOR_DEFAULTTONEAREST) : nullptr;
  if (!hmon)
    hmon = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);

  MONITORINFO mi{sizeof(mi)};
  GetMonitorInfoW(hmon, &mi);
  int workW = mi.rcWork.right - mi.rcWork.left;
  int workH = mi.rcWork.bottom - mi.rcWork.top;
  winW = min(winW, workW);
  winH = min(winH, workH);
  int x = mi.rcWork.left + (workW - winW) / 2;
  int y = mi.rcWork.top + (workH - winH) / 2;

  _hwnd = CreateWindowExW(0, kClass, L"极达五笔 - 设置", style, x, y, winW,
                          winH, _owner, nullptr, _instance, this);
  if (!_hwnd)
    return false;

  _Layout();
  _LoadFromConfig();
  ShowWindow(_hwnd, SW_SHOW);
  UpdateWindow(_hwnd);
  return true;
}

HWND CSettingWindow::_MkCheck(const wchar_t* text,
                              int x,
                              int y,
                              int w,
                              int id) {
  HWND control = CreateWindowExW(
      0, L"BUTTON", text,
      WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP, _S(x), _S(y),
      _S(w), _S(24), _controlParent, (HMENU)(INT_PTR)id, _instance, nullptr);
  SendMessageW(control, WM_SETFONT, (WPARAM)_font, TRUE);
  return control;
}

HWND CSettingWindow::_MkRadio(const wchar_t* text,
                              int x,
                              int y,
                              int w,
                              int id,
                              bool firstInGroup) {
  DWORD style = WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON;
  if (firstInGroup)
    style |= WS_GROUP | WS_TABSTOP;
  HWND control = CreateWindowExW(0, L"BUTTON", text, style, _S(x), _S(y), _S(w),
                                 _S(24), _controlParent, (HMENU)(INT_PTR)id,
                                 _instance, nullptr);
  SendMessageW(control, WM_SETFONT, (WPARAM)_font, TRUE);
  return control;
}

HWND CSettingWindow::_MkLabel(const wchar_t* text,
                              int x,
                              int y,
                              int w,
                              int h) {
  HWND control = CreateWindowExW(0, L"STATIC", text,
                                 WS_CHILD | WS_VISIBLE | SS_LEFT, _S(x), _S(y),
                                 _S(w), _S(h), _controlParent, nullptr,
                                 _instance, nullptr);
  SendMessageW(control, WM_SETFONT, (WPARAM)_font, TRUE);
  return control;
}

HWND CSettingWindow::_MkButton(const wchar_t* text,
                               int x,
                               int y,
                               int w,
                               int h,
                               int id) {
  HWND control = CreateWindowExW(
      0, L"BUTTON", text,
      WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, _S(x), _S(y), _S(w),
      _S(h), _controlParent, (HMENU)(INT_PTR)id, _instance, nullptr);
  SendMessageW(control, WM_SETFONT, (WPARAM)_font, TRUE);
  return control;
}

HWND CSettingWindow::_MkColorButton(int x, int y, int w, int h, int id) {
  HWND control = CreateWindowExW(
      0, L"BUTTON", L"",
      WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP, _S(x), _S(y), _S(w),
      _S(h), _controlParent, (HMENU)(INT_PTR)id, _instance, nullptr);
  SendMessageW(control, WM_SETFONT, (WPARAM)_font, TRUE);
  return control;
}

HWND CSettingWindow::_MkCombo(int x, int y, int w, int h, int id) {
  HWND control = CreateWindowExW(
      0, L"COMBOBOX", L"",
      WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, _S(x),
      _S(y), _S(w), _S(h), _controlParent, (HMENU)(INT_PTR)id, _instance,
      nullptr);
  SendMessageW(control, WM_SETFONT, (WPARAM)_font, TRUE);
  return control;
}

void CSettingWindow::_Layout() {
  _font = CreateFontW(-_S(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                      CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");

  _tree = CreateWindowExW(
      WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | TVS_HASBUTTONS | TVS_HASLINES |
          TVS_LINESATROOT | TVS_SHOWSELALWAYS | TVS_NOHSCROLL,
      _S(12), _S(14), _S(170), _S(456), _hwnd, nullptr, _instance, nullptr);
  SendMessageW(_tree, WM_SETFONT, (WPARAM)_font, TRUE);

  _controlParent = _hwnd;
  HWND reset = _MkButton(L"恢复默认(&R)", 16, 488, 112, 28, ID_BTN_RESET);
  HWND cancel = _MkButton(L"取消(&C)", 580, 488, 88, 28, ID_BTN_CANCEL);
  HWND ok = _MkButton(L"确定(&O)", 680, 488, 88, 28, ID_BTN_OK);

  HTREEITEM categoryItems[sizeof(kCategories) / sizeof(kCategories[0])] = {};
  int firstPageIndex = -1;
  for (size_t i = 0; i < sizeof(kCategories) / sizeof(kCategories[0]); ++i) {
    const SettingCategory& category = kCategories[i];
    HTREEITEM parent = category.parentIndex >= 0
                           ? categoryItems[category.parentIndex]
                           : TVI_ROOT;
    categoryItems[i] =
        InsertTreeItem(_tree, parent, category.name, category.pageId);
    if (category.pageId == 0) {
      TreeView_Expand(_tree, categoryItems[i], TVE_EXPAND);
      continue;
    }
    if (category.pageId == PAGE_INPUT)
      firstPageIndex = (int)i;

    HWND page = CreateWindowExW(
        WS_EX_CONTROLPARENT, kPageClass, L"",
        WS_CHILD | WS_CLIPCHILDREN, _S(198), _S(14), _S(570), _S(456), _hwnd,
        nullptr, _instance, _hwnd);
    SendMessageW(page, WM_SETFONT, (WPARAM)_font, TRUE);
    _pages.push_back({category.pageId, page});
    _controlParent = page;

    switch (category.pageId) {
      case PAGE_INPUT:
        _MkLabel(L"输入选项", 22, 18, 500, 28);
        _MkLabel(L"自定义五笔输入行为。", 22, 50, 500, 24);
        _controls.chkZWildcard = _MkCheck(L"Z 键通配", 24, 106, 220, ID_Z_WILDCARD);
        _controls.chkZRepeat = _MkCheck(L"Z 键重复上次上屏", 24, 142, 240, ID_Z_REPEAT);
        _controls.chkSemiSecond =
            _MkCheck(L"分号选第二重码", 24, 178, 240, ID_SEMI_SECOND);
        _controls.chkPunctTop =
            _MkCheck(L"标点顶字上屏", 24, 214, 240, ID_PUNCT_TOP);
        _controls.chkPerKey = _MkCheck(L"逐键提示", 292, 106, 220, ID_PERKEY_HINT);
        _controls.chkInline = _MkCheck(L"嵌入预编辑", 292, 142, 220, ID_INLINE_PREEDIT);
        _controls.chkShiftDirect =
            _MkCheck(L"Shift 直接上屏", 292, 178, 240, ID_SHIFT_DIRECT);
        break;

      case PAGE_CHARACTER_SET: {
        _MkLabel(L"汉字库版本", 22, 18, 500, 28);
        _MkLabel(L"选择输入法使用的汉字字符集范围。", 22, 50, 520, 24);
        _MkLabel(L"字符集标准", 24, 108, 120, 24);
        _controls.cboCharacterSet =
            _MkCombo(144, 104, 260, 160, ID_CBO_CHARACTER_SET);
        const struct {
          const wchar_t* name;
          CharacterSetVersion version;
        } options[] = {
            {L"GB2312", CharacterSetVersion::GB2312},
            {L"GBK", CharacterSetVersion::GBK},
            {L"GB18030-2000", CharacterSetVersion::GB18030_2000},
            {L"GB18030-2005", CharacterSetVersion::GB18030_2005},
        };
        for (const auto& option : options) {
          LRESULT index = SendMessageW(_controls.cboCharacterSet, CB_ADDSTRING,
                                       0, (LPARAM)option.name);
          if (index != CB_ERR && index != CB_ERRSPACE)
            SendMessageW(_controls.cboCharacterSet, CB_SETITEMDATA, index,
                         (LPARAM)option.version);
        }
        _MkLabel(
            L"当前词库包含 GBK 扩展字；更高版本暂使用 GBK 词库，后续可添加对应词库文件。",
            24, 148, 510, 48);
        break;
      }

      case PAGE_CANDIDATES: {
        _MkLabel(L"候选列表", 22, 18, 500, 28);
        _MkLabel(L"设置候选窗口的排列方式和每页候选数量。", 22, 50, 520, 24);
        _controls.radVert =
            _MkRadio(L"竖向排列", 24, 108, 120, ID_RAD_VERTICAL, true);
        _controls.radHorz =
            _MkRadio(L"横向排列", 164, 108, 120, ID_RAD_HORIZONTAL);
        _MkLabel(L"每页候选数", 24, 166, 120, 24);
        _controls.cboPage = _MkCombo(144, 162, 90, 180, ID_CBO_PAGESIZE);
        for (int value = 3; value <= 9; ++value) {
          wchar_t text[4];
          swprintf_s(text, L"%d", value);
          SendMessageW(_controls.cboPage, CB_ADDSTRING, 0, (LPARAM)text);
        }
        break;
      }

      case PAGE_APPEARANCE:
        _MkLabel(L"候选窗口外观", 22, 18, 500, 28);
        _MkLabel(L"选择候选字体和窗口配色。", 22, 50, 520, 24);
        _MkLabel(L"候选字体", 24, 108, 100, 26);
        _controls.lblFont = _MkLabel(L"", 124, 108, 270, 26);
        _controls.btnFont =
            _MkButton(L"选择字体...", 410, 104, 120, 30, ID_BTN_FONT);
        _MkLabel(L"候选文字颜色", 24, 166, 140, 24);
        _MkLabel(L"窗口背景颜色", 204, 166, 140, 24);
        _MkLabel(L"选中项背景颜色", 384, 166, 160, 24);
        _controls.btnTextClr =
            _MkColorButton(24, 194, 150, 34, ID_BTN_TEXTCLR);
        _controls.btnBgClr =
            _MkColorButton(204, 194, 150, 34, ID_BTN_BGCLR);
        _controls.btnSelClr =
            _MkColorButton(384, 194, 150, 34, ID_BTN_SELCLR);
        break;

      case PAGE_STATUS:
        _MkLabel(L"状态栏", 22, 18, 500, 28);
        _MkLabel(L"控制输入法状态栏的显示。", 22, 50, 520, 24);
        _controls.chkShowBar =
            _MkCheck(L"显示状态栏", 24, 108, 240, ID_SHOW_STATUS);
        break;
    }
  }

  SendMessageW(_tree, WM_SETFONT, (WPARAM)_font, TRUE);
  SendMessageW(reset, WM_SETFONT, (WPARAM)_font, TRUE);
  SendMessageW(cancel, WM_SETFONT, (WPARAM)_font, TRUE);
  SendMessageW(ok, WM_SETFONT, (WPARAM)_font, TRUE);
  _controlParent = nullptr;

  if (firstPageIndex >= 0)
    TreeView_SelectItem(_tree, categoryItems[firstPageIndex]);
}

void CSettingWindow::_LoadFromConfig() {
  _draft = g_config;
  SetCheck(_controls.chkZWildcard, _draft.zWildcard);
  SetCheck(_controls.chkZRepeat, _draft.zRepeat);
  SetCheck(_controls.chkSemiSecond, _draft.semicolonSecond);
  SetCheck(_controls.chkPunctTop, _draft.punctuationTop);
  SetCheck(_controls.chkPerKey, _draft.perKeyHint);
  SetCheck(_controls.chkInline, _draft.inlinePreedit);
  SetCheck(_controls.chkShiftDirect, _draft.shiftDirectCommit);
  SetCheck(_controls.chkShowBar, _draft.showStatusBar);
  SetCheck(_controls.radVert, _draft.candLayout == CandLayout::Vertical);
  SetCheck(_controls.radHorz, _draft.candLayout == CandLayout::Horizontal);
  SelectComboData(_controls.cboCharacterSet,
                  static_cast<int>(_draft.characterSetVersion));

  wchar_t text[8];
  swprintf_s(text, L"%d", _draft.candPageSize);
  SendMessageW(_controls.cboPage, CB_SELECTSTRING, (WPARAM)-1, (LPARAM)text);

  wchar_t fontLabel[128];
  swprintf_s(fontLabel, L"%ls  %d pt", _draft.candFontFace,
             _draft.candFontSize);
  SetWindowTextW(_controls.lblFont, fontLabel);
  InvalidateRect(_controls.btnTextClr, nullptr, TRUE);
  InvalidateRect(_controls.btnBgClr, nullptr, TRUE);
  InvalidateRect(_controls.btnSelClr, nullptr, TRUE);
}

void CSettingWindow::_SaveToConfig() {
  _draft.zWildcard = IsChecked(_controls.chkZWildcard);
  _draft.zRepeat = IsChecked(_controls.chkZRepeat);
  _draft.semicolonSecond = IsChecked(_controls.chkSemiSecond);
  _draft.punctuationTop = IsChecked(_controls.chkPunctTop);
  _draft.perKeyHint = IsChecked(_controls.chkPerKey);
  _draft.inlinePreedit = IsChecked(_controls.chkInline);
  _draft.shiftDirectCommit = IsChecked(_controls.chkShiftDirect);
  _draft.showStatusBar = IsChecked(_controls.chkShowBar);
  _draft.candLayout =
      IsChecked(_controls.radVert) ? CandLayout::Vertical : CandLayout::Horizontal;

  int characterSetIndex =
      (int)SendMessageW(_controls.cboCharacterSet, CB_GETCURSEL, 0, 0);
  if (characterSetIndex >= 0) {
    int version = (int)SendMessageW(_controls.cboCharacterSet, CB_GETITEMDATA,
                                    characterSetIndex, 0);
    if (IsKnownCharacterSetVersion(version))
      _draft.characterSetVersion = static_cast<CharacterSetVersion>(version);
  }

  int pageIndex = (int)SendMessageW(_controls.cboPage, CB_GETCURSEL, 0, 0);
  if (pageIndex >= 0)
    _draft.candPageSize = 3 + pageIndex;

  g_config = _draft;
}

void CSettingWindow::_ResetToDefault() {
  _draft = JiDaConfig();
  SetCheck(_controls.chkZWildcard, _draft.zWildcard);
  SetCheck(_controls.chkZRepeat, _draft.zRepeat);
  SetCheck(_controls.chkSemiSecond, _draft.semicolonSecond);
  SetCheck(_controls.chkPunctTop, _draft.punctuationTop);
  SetCheck(_controls.chkPerKey, _draft.perKeyHint);
  SetCheck(_controls.chkInline, _draft.inlinePreedit);
  SetCheck(_controls.chkShiftDirect, _draft.shiftDirectCommit);
  SetCheck(_controls.chkShowBar, _draft.showStatusBar);
  SetCheck(_controls.radVert, _draft.candLayout == CandLayout::Vertical);
  SetCheck(_controls.radHorz, _draft.candLayout == CandLayout::Horizontal);
  SelectComboData(_controls.cboCharacterSet,
                  static_cast<int>(_draft.characterSetVersion));

  wchar_t text[8];
  swprintf_s(text, L"%d", _draft.candPageSize);
  SendMessageW(_controls.cboPage, CB_SELECTSTRING, (WPARAM)-1, (LPARAM)text);

  wchar_t fontLabel[128];
  swprintf_s(fontLabel, L"%ls  %d pt", _draft.candFontFace,
             _draft.candFontSize);
  SetWindowTextW(_controls.lblFont, fontLabel);
  InvalidateRect(_controls.btnTextClr, nullptr, TRUE);
  InvalidateRect(_controls.btnBgClr, nullptr, TRUE);
  InvalidateRect(_controls.btnSelClr, nullptr, TRUE);
}

void CSettingWindow::_SelectPage(int pageId) {
  for (const Page& page : _pages)
    ShowWindow(page.window, page.id == pageId ? SW_SHOW : SW_HIDE);
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

LRESULT CALLBACK CSettingWindow::_PageWndProc(HWND h,
                                              UINT msg,
                                              WPARAM wParam,
                                              LPARAM lParam) {
  if (msg == WM_NCCREATE) {
    auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
    SetWindowLongPtrW(h, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(create->lpCreateParams));
  }
  HWND owner =
      reinterpret_cast<HWND>(GetWindowLongPtrW(h, GWLP_USERDATA));
  if (owner && (msg == WM_COMMAND || msg == WM_DRAWITEM))
    return SendMessageW(owner, msg, wParam, lParam);
  return DefWindowProcW(h, msg, wParam, lParam);
}

LRESULT CSettingWindow::_Handle(UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_NOTIFY: {
      auto* header = reinterpret_cast<NMHDR*>(lParam);
      if (header->hwndFrom == _tree && header->code == TVN_SELCHANGEDW) {
        auto* change = reinterpret_cast<NMTREEVIEWW*>(lParam);
        int pageId = (int)change->itemNew.lParam;
        if (pageId) {
          _SelectPage(pageId);
        } else {
          HTREEITEM firstPage =
              FindFirstPageItem(_tree, TreeView_GetChild(_tree, change->itemNew.hItem));
          if (firstPage)
            TreeView_SelectItem(_tree, firstPage);
        }
        return 0;
      }
      break;
    }
    case WM_DRAWITEM: {
      auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
      COLORREF color = RGB(0, 0, 0);
      switch (draw->CtlID) {
        case ID_BTN_TEXTCLR:
          color = _draft.candTextColor;
          break;
        case ID_BTN_BGCLR:
          color = _draft.candBgColor;
          break;
        case ID_BTN_SELCLR:
          color = _draft.candSelBgColor;
          break;
        default:
          break;
      }
      if (draw->CtlID == ID_BTN_TEXTCLR || draw->CtlID == ID_BTN_BGCLR ||
          draw->CtlID == ID_BTN_SELCLR) {
        HBRUSH brush = CreateSolidBrush(color);
        FillRect(draw->hDC, &draw->rcItem, brush);
        DeleteObject(brush);
        DrawEdge(draw->hDC, &draw->rcItem,
                 (draw->itemState & ODS_SELECTED) ? EDGE_SUNKEN : EDGE_RAISED,
                 BF_RECT);
        int luminance = (GetRValue(color) * 299 + GetGValue(color) * 587 +
                         GetBValue(color) * 114) /
                        1000;
        SetTextColor(draw->hDC,
                     luminance < 128 ? RGB(255, 255, 255) : RGB(0, 0, 0));
        SetBkMode(draw->hDC, TRANSPARENT);
        HFONT oldFont = (HFONT)SelectObject(draw->hDC, _font);
        wchar_t label[64];
        swprintf_s(label, L"#%02X%02X%02X", GetRValue(color), GetGValue(color),
                   GetBValue(color));
        DrawTextW(draw->hDC, label, -1, &draw->rcItem,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(draw->hDC, oldFont);
        if (draw->itemState & ODS_FOCUS)
          DrawFocusRect(draw->hDC, &draw->rcItem);
        return TRUE;
      }
      break;
    }
    case WM_COMMAND:
      switch (LOWORD(wParam)) {
        case ID_BTN_OK:
          _Close(true);
          return 0;
        case ID_BTN_CANCEL:
          _Close(false);
          return 0;
        case ID_BTN_FONT: {
          LOGFONTW lf = {};
          wcscpy_s(lf.lfFaceName, _draft.candFontFace);
          HDC hdc = GetDC(_hwnd);
          int dpi = GetDeviceCaps(hdc, LOGPIXELSY);
          ReleaseDC(_hwnd, hdc);
          lf.lfHeight = -MulDiv(_draft.candFontSize, dpi, 72);
          lf.lfWeight = _draft.candFontWeight;
          lf.lfItalic = _draft.candFontItalic ? TRUE : FALSE;

          CHOOSEFONTW chooseFont = {};
          chooseFont.lStructSize = sizeof(chooseFont);
          chooseFont.hwndOwner = _hwnd;
          chooseFont.lpLogFont = &lf;
          chooseFont.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT | CF_EFFECTS;
          chooseFont.rgbColors = _draft.candTextColor;
          if (ChooseFontW(&chooseFont)) {
            wcscpy_s(_draft.candFontFace, lf.lfFaceName);
            _draft.candFontSize = max(8, min(48, chooseFont.iPointSize / 10));
            _draft.candFontWeight = lf.lfWeight;
            _draft.candFontItalic = lf.lfItalic != 0;
            _draft.candTextColor = chooseFont.rgbColors;

            wchar_t label[128];
            swprintf_s(label, L"%ls  %d pt", _draft.candFontFace,
                       _draft.candFontSize);
            SetWindowTextW(_controls.lblFont, label);
            InvalidateRect(_controls.btnTextClr, nullptr, TRUE);
          }
          return 0;
        }
        case ID_BTN_TEXTCLR:
        case ID_BTN_BGCLR:
        case ID_BTN_SELCLR: {
          static COLORREF customColors[16] = {};
          CHOOSECOLORW chooseColor = {};
          chooseColor.lStructSize = sizeof(chooseColor);
          chooseColor.hwndOwner = _hwnd;
          chooseColor.lpCustColors = customColors;
          chooseColor.Flags = CC_FULLOPEN | CC_RGBINIT;
          COLORREF* selectedColor = nullptr;
          if (LOWORD(wParam) == ID_BTN_TEXTCLR) {
            selectedColor = &_draft.candTextColor;
          } else if (LOWORD(wParam) == ID_BTN_BGCLR) {
            selectedColor = &_draft.candBgColor;
          } else {
            selectedColor = &_draft.candSelBgColor;
          }
          chooseColor.rgbResult = *selectedColor;
          if (ChooseColorW(&chooseColor)) {
            *selectedColor = chooseColor.rgbResult;
            if (LOWORD(wParam) == ID_BTN_SELCLR) {
              int luminance =
                  (GetRValue(chooseColor.rgbResult) * 299 +
                   GetGValue(chooseColor.rgbResult) * 587 +
                   GetBValue(chooseColor.rgbResult) * 114) /
                  1000;
              _draft.candSelTextColor =
                  luminance < 128 ? RGB(255, 255, 255) : RGB(0, 0, 0);
            }
            InvalidateRect((HWND)lParam, nullptr, TRUE);
          }
          return 0;
        }
        case ID_BTN_RESET:
          _ResetToDefault();
          return 0;
      }
      return 0;
    case WM_CLOSE:
      _Close(false);
      return 0;

    case WM_NCDESTROY: {
      HWND window = _hwnd;
      SetWindowLongPtrW(window, GWLP_USERDATA, 0);
      _hwnd = nullptr;
      if (_owner && IsWindow(_owner))
        EnableWindow(_owner, TRUE);
      if (s_SettingInstance == this)
        s_SettingInstance = nullptr;
      delete this;
      return DefWindowProcW(window, msg, wParam, lParam);
    }
  }
  return DefWindowProcW(_hwnd, msg, wParam, lParam);
}
