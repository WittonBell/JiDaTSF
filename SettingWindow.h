// SettingWindow.h
#pragma once
#include <windows.h>
#include <functional>

class CSettingWindow {
 public:
  using ChangeCallback = std::function<void()>;

  // 模态打开，关闭后返回；如果用户点“确定”，onChange 会被调用
  static void Show(HINSTANCE instance, HWND owner, ChangeCallback onChange);

 private:
  CSettingWindow(HINSTANCE inst, HWND owner, ChangeCallback cb);
  ~CSettingWindow();

  bool _Create();
  void _Layout();
  void _LoadFromConfig();
  void _SaveToConfig();
  void _ResetToDefault();
  void _Close(bool save);

  static LRESULT CALLBACK _WndProc(HWND, UINT, WPARAM, LPARAM);
  LRESULT _Handle(UINT msg, WPARAM wParam, LPARAM lParam);

  HWND _MkGroup(const wchar_t* text, int x, int y, int w, int h);
  HWND _MkCheck(const wchar_t* text, int x, int y, int w, int id, bool checked);
  HWND _MkRadio(const wchar_t* text,
                int x,
                int y,
                int w,
                int id,
                bool checked,
                bool firstInGroup = false);
  HWND _MkLabel(const wchar_t* text, int x, int y, int w, int h);
  HWND _MkButton(const wchar_t* text, int x, int y, int w, int h, int id);
  HWND _MkCombo(int x, int y, int w, int h, int id);

  UINT _Dpi() const;
  int _S(int v) const { return MulDiv(v, (int)_Dpi(), 96); }

  HINSTANCE _instance = nullptr;
  HWND _owner = nullptr;
  HWND _hwnd = nullptr;
  HFONT _font = nullptr;
  ChangeCallback _onChange;

  HWND _chkAutoCommit = nullptr, _chkZWildcard = nullptr, _chkZRepeat = nullptr;
  HWND _chkSemiSecond = nullptr, _chkPunctTop = nullptr, _chkPerKey = nullptr;
  HWND _chkInline = nullptr, _chkShowBar = nullptr;
  HWND _radVert = nullptr, _radHorz = nullptr;
  HWND _cboPage = nullptr;
  HWND _btnReset = nullptr, _btnOk = nullptr, _btnCancel = nullptr;

 private:
  HWND _btnFont = nullptr;
  HWND _btnTextClr = nullptr;
  HWND _btnBgClr = nullptr;
  HWND _btnSelClr = nullptr;
  HWND _lblFont = nullptr;
};