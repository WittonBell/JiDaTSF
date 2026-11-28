#include "SettingWindow.h"
// 设置窗口实现：配置加载、控件布局、虚拟键盘编辑和配置保存。
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
  PAGE_CHARACTER_SET,
  PAGE_TOGGLE_SHORTCUT,
  PAGE_SOFT_KEYBOARD,
  PAGE_CUSTOM_PUNCTUATION
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
  ID_BTN_SELCLR,
  ID_BTN_KEY_LABEL_CLR,
  ID_BTN_KEY_NORMAL_CLR,
  ID_BTN_KEY_SHIFTED_CLR,
  ID_VIRTUAL_KEY_BASE = 3000,
  ID_TOGGLE_SHORTCUT = 4000
};

constexpr wchar_t kClass[] = L"JiDa.SettingWindow";
constexpr wchar_t kPageClass[] = L"JiDa.SettingPage";
constexpr wchar_t kKeyEditClass[] = L"JiDa.KeyEditDialog";
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
    {L"设置快捷键", -1, 0},
    {L"输入选项", 0, PAGE_INPUT},
    {L"字库版本", 0, PAGE_CHARACTER_SET},
    {L"候选列表", 1, PAGE_CANDIDATES},
    {L"外观", 1, PAGE_APPEARANCE},
    {L"状态栏", -1, PAGE_STATUS},
    {L"中英文切换", 2, PAGE_TOGGLE_SHORTCUT},
    {L"定义软键盘", 2, PAGE_SOFT_KEYBOARD},
    {L"自定义标点", 2, PAGE_CUSTOM_PUNCTUATION},
};

struct KeyEditState {
  HWND parent = nullptr;
  HINSTANCE instance = nullptr;
  std::wstring title;
  KeyCharacterPair values;
  HWND normalEdit = nullptr;
  HWND shiftedEdit = nullptr;
  bool accepted = false;
};

LRESULT CALLBACK KeyEditWndProc(HWND window,
                                UINT message,
                                WPARAM wParam,
                                LPARAM lParam) {
  KeyEditState* state =
      reinterpret_cast<KeyEditState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
    state = static_cast<KeyEditState*>(create->lpCreateParams);
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
  }
  if (!state)
    return DefWindowProcW(window, message, wParam, lParam);

  switch (message) {
    case WM_CREATE: {
      HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
      HWND label = CreateWindowExW(0, L"STATIC", state->title.c_str(),
                                   WS_CHILD | WS_VISIBLE, 14, 12, 300, 22,
                                   window, nullptr, state->instance, nullptr);
      HWND normalLabel = CreateWindowExW(0, L"STATIC", L"正常字符",
                                         WS_CHILD | WS_VISIBLE, 18, 48, 80, 22,
                                         window, nullptr, state->instance,
                                         nullptr);
      state->normalEdit = CreateWindowExW(
          WS_EX_CLIENTEDGE, L"EDIT", state->values.normal.c_str(),
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 100, 46, 70, 24,
          window, (HMENU)(INT_PTR)10, state->instance, nullptr);
      HWND shiftedLabel = CreateWindowExW(0, L"STATIC", L"上档字符",
                                          WS_CHILD | WS_VISIBLE, 182, 48, 80,
                                          22, window, nullptr, state->instance,
                                          nullptr);
      state->shiftedEdit = CreateWindowExW(
          WS_EX_CLIENTEDGE, L"EDIT", state->values.shifted.c_str(),
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 264, 46, 45, 24,
          window, (HMENU)(INT_PTR)11, state->instance, nullptr);
      HWND ok = CreateWindowExW(0, L"BUTTON", L"确定",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                                142, 92, 75, 28, window, (HMENU)(INT_PTR)IDOK,
                                state->instance, nullptr);
      HWND cancel = CreateWindowExW(0, L"BUTTON", L"取消",
                                    WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                    226, 92, 75, 28, window,
                                    (HMENU)(INT_PTR)IDCANCEL, state->instance,
                                    nullptr);
      SendMessageW(label, WM_SETFONT, (WPARAM)font, TRUE);
      SendMessageW(normalLabel, WM_SETFONT, (WPARAM)font, TRUE);
      SendMessageW(state->normalEdit, WM_SETFONT, (WPARAM)font, TRUE);
      SendMessageW(shiftedLabel, WM_SETFONT, (WPARAM)font, TRUE);
      SendMessageW(state->shiftedEdit, WM_SETFONT, (WPARAM)font, TRUE);
      SendMessageW(ok, WM_SETFONT, (WPARAM)font, TRUE);
      SendMessageW(cancel, WM_SETFONT, (WPARAM)font, TRUE);
      SendMessageW(state->normalEdit, EM_SETLIMITTEXT, 1, 0);
      SendMessageW(state->shiftedEdit, EM_SETLIMITTEXT, 1, 0);
      return 0;
    }
    case WM_COMMAND:
      if (LOWORD(wParam) == IDOK) {
        wchar_t value[2] = {};
        GetWindowTextW(state->normalEdit, value, _countof(value));
        state->values.normal = value;
        GetWindowTextW(state->shiftedEdit, value, _countof(value));
        state->values.shifted = value;
        state->accepted = true;
        DestroyWindow(window);
        return 0;
      }
      if (LOWORD(wParam) == IDCANCEL) {
        DestroyWindow(window);
        return 0;
      }
      break;
    case WM_CLOSE:
      DestroyWindow(window);
      return 0;
    case WM_NCDESTROY:
      SetWindowLongPtrW(window, GWLP_USERDATA, 0);
      break;
  }
  return DefWindowProcW(window, message, wParam, lParam);
}

bool ShowKeyEditDialog(HWND parent,
                       HINSTANCE instance,
                       const wchar_t* title,
                       KeyCharacterPair& values) {
  static bool registered = false;
  if (!registered) {
    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = KeyEditWndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = kKeyEditClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
      return false;
    registered = true;
  }

  KeyEditState state;
  state.parent = parent;
  state.instance = instance;
  state.title = title;
  state.values = values;
  RECT ownerRect = {};
  GetWindowRect(parent, &ownerRect);
  int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - 330) / 2;
  int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - 160) / 2;
  HWND dialog = CreateWindowExW(
      WS_EX_DLGMODALFRAME, kKeyEditClass, L"设置按键字符",
      WS_CAPTION | WS_SYSMENU | WS_POPUP, x, y, 350, 200, parent, nullptr,
      instance, &state);
  if (!dialog)
    return false;

  EnableWindow(parent, FALSE);
  ShowWindow(dialog, SW_SHOW);
  SetFocus(state.normalEdit);
  MSG message;
  while (IsWindow(dialog)) {
    BOOL result = GetMessageW(&message, nullptr, 0, 0);
    if (result <= 0)
      break;
    if (!IsDialogMessageW(dialog, &message)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
  }
  EnableWindow(parent, TRUE);
  SetForegroundWindow(parent);
  if (state.accepted)
    values = std::move(state.values);
  return state.accepted;
}

std::wstring DefaultPunctuationValue(wchar_t keyCharacter) {
  for (const auto& mapping : g_punctMap) {
    if (mapping.first == keyCharacter)
      return mapping.second;
  }
  return std::wstring(1, keyCharacter);
}

void SetCheck(HWND control, bool value) {
  SendMessageW(control, BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0);
}

bool IsKeyboardColorButton(int id) {
  return id >= ID_BTN_KEY_LABEL_CLR && id <= ID_BTN_KEY_SHIFTED_CLR;
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
  // 保存宿主、窗口及配置变更回调，后续在窗口线程中创建控件。
    : _instance(inst), _owner(owner), _onChange(std::move(cb)) {}

CSettingWindow::~CSettingWindow() {
  if (_font)
    DeleteObject(_font);
  if (_keyboardFont)
    DeleteObject(_keyboardFont);
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
  // 创建主窗口、左侧页面树、页面容器及默认控件布局。
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
  // 根据窗口和 DPI 尺寸重新排列页面容器与内容控件。
  _font = CreateFontW(-_S(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                      CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");
  _keyboardFont =
      CreateFontW(-_S(10), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
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
        _controls.chkPerKey = _MkCheck(L"启用编码逐渐提示", 292, 106, 220, ID_PERKEY_HINT);
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

      case PAGE_TOGGLE_SHORTCUT: {
        _MkLabel(L"中英文切换快捷键", 22, 18, 500, 28);
        _MkLabel(L"选择用于切换中英文输入状态的单键快捷键。", 22, 50, 520,
                 24);
        _MkLabel(L"切换按键", 24, 108, 100, 24);
        _controls.cboChineseToggleShortcut =
            _MkCombo(144, 104, 180, 180, ID_TOGGLE_SHORTCUT);
        const struct {
          const wchar_t* name;
          ChineseToggleShortcut shortcut;
        } options[] = {
            {L"禁用", ChineseToggleShortcut::Disabled},
            {L"左 Shift", ChineseToggleShortcut::LeftShift},
            {L"右 Shift", ChineseToggleShortcut::RightShift},
            {L"Shift", ChineseToggleShortcut::Shift},
            {L"左 Ctrl", ChineseToggleShortcut::LeftControl},
            {L"右 Ctrl", ChineseToggleShortcut::RightControl},
            {L"Ctrl", ChineseToggleShortcut::Control},
        };
        for (const auto& option : options) {
          LRESULT index = SendMessageW(_controls.cboChineseToggleShortcut,
                                       CB_ADDSTRING, 0,
                                       (LPARAM)option.name);
          if (index != CB_ERR && index != CB_ERRSPACE)
            SendMessageW(_controls.cboChineseToggleShortcut, CB_SETITEMDATA,
                         index, (LPARAM)option.shortcut);
        }
        break;
      }

      case PAGE_SOFT_KEYBOARD:
        _MkLabel(L"定义软键盘", 22, 18, 500, 28);
        _MkLabel(L"左侧灰色显示键位符，右侧显示字符；上下依次为上档和正常字符。",
                 22, 50, 548, 24);
        _MkLabel(L"下方可分别设置键位符、正常字符和上档字符的显示颜色。", 22, 76,
                 548, 24);
        _BuildVirtualKeyboard();
        break;

      case PAGE_CUSTOM_PUNCTUATION:
        _MkLabel(L"自定义标点", 22, 18, 500, 28);
        _MkLabel(L"左侧灰色显示键位符，右侧显示标点；上下依次为上档和正常字符。",
                 22, 50, 548, 24);
        _MkLabel(L"下方可分别设置键位符、正常字符和上档字符的显示颜色。", 22, 76,
                 548, 24);
        _BuildVirtualKeyboard();
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

void CSettingWindow::_BuildVirtualKeyboard() {
  // 构建虚拟键盘按键按钮及普通/Shift 字符编辑入口。
  static const UINT rows[][13] = {
      {VK_OEM_3, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0',
       VK_OEM_MINUS, VK_OEM_PLUS},
      {'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', VK_OEM_4,
       VK_OEM_6, VK_OEM_5},
      {'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', VK_OEM_1, VK_OEM_7},
      {'Z', 'X', 'C', 'V', 'B', 'N', 'M', VK_OEM_COMMA, VK_OEM_PERIOD,
       VK_OEM_2}};
  static const size_t rowSizes[] = {13, 13, 11, 10};
  for (size_t row = 0; row < _countof(rows); ++row) {
    int totalWidth = static_cast<int>(rowSizes[row]) * 38 +
                     static_cast<int>(rowSizes[row] - 1) * 2;
    int startX = 22 + (526 - totalWidth) / 2;
    for (size_t column = 0; column < rowSizes[row]; ++column) {
      UINT key = rows[row][column];
      const wchar_t* label = L"";
      for (size_t i = 0; i < g_virtualKeyboardKeyCount; ++i) {
        if (g_virtualKeyboardKeys[i].key == key) {
          label = g_virtualKeyboardKeys[i].label;
          break;
        }
      }
      HWND button = CreateWindowExW(
          0, L"BUTTON", label,
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
          _S(startX + static_cast<int>(column) * 40),
          _S(108 + static_cast<int>(row) * 42), _S(38), _S(38),
          _controlParent, (HMENU)(INT_PTR)(ID_VIRTUAL_KEY_BASE + key),
          _instance, nullptr);
      SendMessageW(button, WM_SETFONT, (WPARAM)_font, TRUE);
      _keyboardButtons.push_back(button);
    }
  }

  const struct {
    const wchar_t* label;
    int id;
    int x;
    int y;
    int buttonY;
  } colors[] = {
      {L"键位符颜色", ID_BTN_KEY_LABEL_CLR, 24, 286, 310},
      {L"正常字符颜色", ID_BTN_KEY_NORMAL_CLR, 204, 286, 310},
      {L"上档字符颜色", ID_BTN_KEY_SHIFTED_CLR, 384, 286, 310},
  };
  HWND* colorControls[] = {
      &_controls.btnKeyLabelClr, &_controls.btnKeyNormalClr,
      &_controls.btnKeyShiftedClr};
  for (size_t i = 0; i < _countof(colors); ++i) {
    _MkLabel(colors[i].label, colors[i].x, colors[i].y, 160, 20);
    *colorControls[i] =
        _MkColorButton(colors[i].x, colors[i].buttonY, 150, 28, colors[i].id);
    _keyboardColorButtons.push_back(*colorControls[i]);
  }
}

void CSettingWindow::_RefreshVirtualKeyboard() {
  for (HWND button : _keyboardButtons)
    InvalidateRect(button, nullptr, TRUE);
  for (HWND button : _keyboardColorButtons)
    InvalidateRect(button, nullptr, TRUE);
}

void CSettingWindow::_DrawVirtualKey(const DRAWITEMSTRUCT& draw) {
  UINT key = draw.CtlID - ID_VIRTUAL_KEY_BASE;
  const VirtualKeyDefinition* definition = nullptr;
  for (size_t i = 0; i < g_virtualKeyboardKeyCount; ++i) {
    if (g_virtualKeyboardKeys[i].key == key) {
      definition = &g_virtualKeyboardKeys[i];
      break;
    }
  }
  if (!definition)
    return;

  FillRect(draw.hDC, &draw.rcItem, GetSysColorBrush(COLOR_BTNFACE));
  DrawEdge(draw.hDC, const_cast<RECT*>(&draw.rcItem),
           (draw.itemState & ODS_SELECTED) ? EDGE_SUNKEN : EDGE_RAISED,
           BF_RECT);
  HGDIOBJ oldFont = SelectObject(draw.hDC, _keyboardFont);

  const bool punctuationPage = _activePageId == PAGE_CUSTOM_PUNCTUATION;
  std::wstring normal =
      punctuationPage ? DefaultPunctuationValue(definition->normal)
                      : std::wstring(1, definition->normal);
  std::wstring shifted =
      punctuationPage ? DefaultPunctuationValue(definition->shifted)
                      : std::wstring(1, definition->shifted);
  const auto& customMappings =
      punctuationPage ? _draft.customPunctuation : _draft.softKeyboard;
  auto custom = customMappings.find(key);

  std::wstring normalText = normal;
  std::wstring shiftedText = shifted;
  if (custom != customMappings.end()) {
    normalText = custom->second.normal;
    shiftedText = custom->second.shifted;
  }

  SetBkMode(draw.hDC, TRANSPARENT);
  const int left = draw.rcItem.left + _S(2);
  const int right = draw.rcItem.right - _S(2);
  const int top = draw.rcItem.top + _S(2);
  const int bottom = draw.rcItem.bottom - _S(2);
  const int rowMiddle = top + (bottom - top) / 2;
  const int keyLabelRight = left + _S(12);

  RECT shiftedKeyRect = {left, top, keyLabelRight, rowMiddle};
  RECT normalKeyRect = {left, rowMiddle, keyLabelRight, bottom};
  SetTextColor(draw.hDC, _draft.keyboardKeyLabelColor);
  DrawTextW(draw.hDC, &definition->shifted, 1, &shiftedKeyRect,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  DrawTextW(draw.hDC, &definition->normal, 1, &normalKeyRect,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

  RECT shiftedRect = {keyLabelRight + _S(3), top, right, rowMiddle};
  RECT normalRect = {keyLabelRight + _S(3), rowMiddle, right, bottom};
  SetTextColor(draw.hDC, _draft.keyboardShiftedColor);
  DrawTextW(draw.hDC, shiftedText.c_str(), static_cast<int>(shiftedText.size()),
            &shiftedRect,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  SetTextColor(draw.hDC, _draft.keyboardNormalColor);
  DrawTextW(draw.hDC, normalText.c_str(), static_cast<int>(normalText.size()),
            &normalRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  if (draw.itemState & ODS_FOCUS)
    DrawFocusRect(draw.hDC, &draw.rcItem);
  SelectObject(draw.hDC, oldFont);
}

void CSettingWindow::_EditVirtualKey(UINT key, int pageId) {
  const VirtualKeyDefinition* definition = nullptr;
  for (size_t i = 0; i < g_virtualKeyboardKeyCount; ++i) {
    if (g_virtualKeyboardKeys[i].key == key) {
      definition = &g_virtualKeyboardKeys[i];
      break;
    }
  }
  if (!definition)
    return;

  bool punctuationPage = pageId == PAGE_CUSTOM_PUNCTUATION;
  auto& mappings = punctuationPage ? _draft.customPunctuation
                                   : _draft.softKeyboard;
  KeyCharacterPair values;
  auto mapping = mappings.find(key);
  if (mapping != mappings.end()) {
    values = mapping->second;
  } else if (punctuationPage) {
    values.normal = DefaultPunctuationValue(definition->normal);
    values.shifted = DefaultPunctuationValue(definition->shifted);
  } else {
    values.normal = definition->normal;
    values.shifted = definition->shifted;
  }

  wchar_t title[128];
  swprintf_s(title, L"%ls 键 - 正常字符和上档字符", definition->label);
  if (!ShowKeyEditDialog(_hwnd, _instance, title, values))
    return;

  KeyCharacterPair defaults;
  if (punctuationPage) {
    defaults.normal = DefaultPunctuationValue(definition->normal);
    defaults.shifted = DefaultPunctuationValue(definition->shifted);
  } else {
    defaults.normal = definition->normal;
    defaults.shifted = definition->shifted;
  }
  if (values.normal == defaults.normal && values.shifted == defaults.shifted)
    mappings.erase(key);
  else
    mappings[key] = std::move(values);
  _RefreshVirtualKeyboard();
}

void CSettingWindow::_LoadFromConfig() {
  // 将全局配置复制到草稿并同步到各设置控件。
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
  SelectComboData(_controls.cboChineseToggleShortcut,
                  static_cast<int>(_draft.chineseToggleShortcut));

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
  _RefreshVirtualKeyboard();
}

void CSettingWindow::_SaveToConfig() {
  // 从控件读取草稿、校验数值并保存到全局配置和 INI。
  _draft.zWildcard = IsChecked(_controls.chkZWildcard);
  _draft.zRepeat = IsChecked(_controls.chkZRepeat);
  _draft.semicolonSecond = IsChecked(_controls.chkSemiSecond);
  _draft.punctuationTop = IsChecked(_controls.chkPunctTop);
  _draft.perKeyHint = IsChecked(_controls.chkPerKey);
  _draft.inlinePreedit = IsChecked(_controls.chkInline);
  _draft.shiftDirectCommit = IsChecked(_controls.chkShiftDirect);
  _draft.showStatusBar = IsChecked(_controls.chkShowBar);
  int shortcutIndex = (int)SendMessageW(
      _controls.cboChineseToggleShortcut, CB_GETCURSEL, 0, 0);
  if (shortcutIndex >= 0) {
    int shortcut = (int)SendMessageW(_controls.cboChineseToggleShortcut,
                                     CB_GETITEMDATA, shortcutIndex, 0);
    if (shortcut >= static_cast<int>(ChineseToggleShortcut::Disabled) &&
        shortcut <= static_cast<int>(ChineseToggleShortcut::Control))
      _draft.chineseToggleShortcut =
          static_cast<ChineseToggleShortcut>(shortcut);
  }
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
  // 将草稿恢复为默认配置并刷新界面控件。
  _draft = JiDaConfig();
  SetCheck(_controls.chkZWildcard, _draft.zWildcard);
  SetCheck(_controls.chkZRepeat, _draft.zRepeat);
  SetCheck(_controls.chkSemiSecond, _draft.semicolonSecond);
  SetCheck(_controls.chkPunctTop, _draft.punctuationTop);
  SetCheck(_controls.chkPerKey, _draft.perKeyHint);
  SetCheck(_controls.chkInline, _draft.inlinePreedit);
  SetCheck(_controls.chkShiftDirect, _draft.shiftDirectCommit);
  SetCheck(_controls.chkShowBar, _draft.showStatusBar);
  SelectComboData(_controls.cboChineseToggleShortcut,
                  static_cast<int>(_draft.chineseToggleShortcut));
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
  _RefreshVirtualKeyboard();
}

void CSettingWindow::_SelectPage(int pageId) {
  // 切换左侧选择项对应的设置页面。
  _activePageId = pageId;
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
      if (draw->CtlID >= ID_VIRTUAL_KEY_BASE &&
          draw->CtlID < ID_VIRTUAL_KEY_BASE + 256) {
        _DrawVirtualKey(*draw);
        return TRUE;
      }
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
        case ID_BTN_KEY_LABEL_CLR:
          color = _draft.keyboardKeyLabelColor;
          break;
        case ID_BTN_KEY_NORMAL_CLR:
          color = _draft.keyboardNormalColor;
          break;
        case ID_BTN_KEY_SHIFTED_CLR:
          color = _draft.keyboardShiftedColor;
          break;
        default:
          break;
      }
      if (draw->CtlID == ID_BTN_TEXTCLR || draw->CtlID == ID_BTN_BGCLR ||
          draw->CtlID == ID_BTN_SELCLR ||
          IsKeyboardColorButton(draw->CtlID)) {
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
      if (LOWORD(wParam) >= ID_VIRTUAL_KEY_BASE &&
          LOWORD(wParam) < ID_VIRTUAL_KEY_BASE + 256) {
        _EditVirtualKey(LOWORD(wParam) - ID_VIRTUAL_KEY_BASE, _activePageId);
        return 0;
      }
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
        case ID_BTN_KEY_LABEL_CLR:
        case ID_BTN_KEY_NORMAL_CLR:
        case ID_BTN_KEY_SHIFTED_CLR: {
          static COLORREF customColors[16] = {};
          CHOOSECOLORW chooseColor = {};
          chooseColor.lStructSize = sizeof(chooseColor);
          chooseColor.hwndOwner = _hwnd;
          chooseColor.lpCustColors = customColors;
          chooseColor.Flags = CC_FULLOPEN | CC_RGBINIT;
          COLORREF* selectedColor = nullptr;
          switch (LOWORD(wParam)) {
            case ID_BTN_KEY_LABEL_CLR:
              selectedColor = &_draft.keyboardKeyLabelColor;
              break;
            case ID_BTN_KEY_NORMAL_CLR:
              selectedColor = &_draft.keyboardNormalColor;
              break;
            case ID_BTN_KEY_SHIFTED_CLR:
              selectedColor = &_draft.keyboardShiftedColor;
              break;
          }
          if (selectedColor) {
            chooseColor.rgbResult = *selectedColor;
            if (ChooseColorW(&chooseColor)) {
              *selectedColor = chooseColor.rgbResult;
              _RefreshVirtualKeyboard();
            }
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
