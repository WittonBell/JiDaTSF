#pragma once
// 输入法设置窗口及虚拟键盘配置的界面接口。
#include <windows.h>
#include <functional>
#include <vector>
#include "Globals.h"

class CSettingWindow {
 public:
  using ChangeCallback = std::function<void()>;

  static void Show(HINSTANCE instance, HWND owner, ChangeCallback onChange);

 private:
  CSettingWindow(HINSTANCE inst, HWND owner, ChangeCallback cb);
  ~CSettingWindow();

  bool _Create();
  void _Layout();
  void _LoadFromConfig();
  void _SaveToConfig();
  void _ResetToDefault();
  void _SelectPage(int pageId);
  void _Close(bool save);

  static LRESULT CALLBACK _WndProc(HWND, UINT, WPARAM, LPARAM);
  static LRESULT CALLBACK _PageWndProc(HWND, UINT, WPARAM, LPARAM);
  LRESULT _Handle(UINT msg, WPARAM wParam, LPARAM lParam);

  HWND _MkCheck(const wchar_t* text, int x, int y, int w, int id);
  HWND _MkRadio(const wchar_t* text,
                int x,
                int y,
                int w,
                int id,
                bool firstInGroup = false);
  HWND _MkLabel(const wchar_t* text, int x, int y, int w, int h);
  HWND _MkButton(const wchar_t* text, int x, int y, int w, int h, int id);
  HWND _MkColorButton(int x, int y, int w, int h, int id);
  HWND _MkCombo(int x, int y, int w, int h, int id);
  void _BuildVirtualKeyboard();
  void _EditVirtualKey(UINT key, int pageId);
  void _RefreshVirtualKeyboard();
  void _DrawVirtualKey(const DRAWITEMSTRUCT& draw);

  UINT _Dpi() const;
  int _S(int v) const { return MulDiv(v, (int)_Dpi(), 96); }

  HINSTANCE _instance = nullptr;
  HWND _owner = nullptr;
  HWND _hwnd = nullptr;
  HWND _controlParent = nullptr;
  HWND _tree = nullptr;
  HFONT _font = nullptr;
  HFONT _keyboardFont = nullptr;
  ChangeCallback _onChange;

  struct Controls {
    HWND chkZWildcard = nullptr;
    HWND chkZRepeat = nullptr;
    HWND chkSemiSecond = nullptr;
    HWND chkPunctTop = nullptr;
    HWND chkPerKey = nullptr;
    HWND chkInline = nullptr;
    HWND chkShiftDirect = nullptr;
    HWND chkShowBar = nullptr;
    HWND radVert = nullptr;
    HWND radHorz = nullptr;
    HWND cboPage = nullptr;
    HWND cboCharacterSet = nullptr;
    HWND cboChineseToggleShortcut = nullptr;
    HWND btnFont = nullptr;
    HWND btnTextClr = nullptr;
    HWND btnBgClr = nullptr;
    HWND btnSelClr = nullptr;
    HWND btnKeyLabelClr = nullptr;
    HWND btnKeyNormalClr = nullptr;
    HWND btnKeyShiftedClr = nullptr;
    HWND lblFont = nullptr;
  } _controls;

  struct Page {
    int id;
    HWND window;
  };
  std::vector<Page> _pages;
  std::vector<HWND> _keyboardButtons;
  std::vector<HWND> _keyboardColorButtons;
  int _activePageId = 0;
  JiDaConfig _draft;
};
