#pragma once

#include <windows.h>
#include <functional>
#include <string>

enum class StatusCommand {
  ToggleChinese,      // 中文 / 英文
  ToggleFullShape,    // 全角 / 半角
  TogglePunctuation,  // 中文标点 / 英文标点
  ToggleTraditional,  // 简体 / 繁体
  OpenSettings,
  HideStatusWindow
};

struct InputStatus {
  bool chinese = true;
  bool fullShape = false;
  bool chinesePunctuation = true;
  bool traditional = false;
};

// A standalone, non-activating TSF input-method status window.
// The owner (normally CTextService) remains the source of truth for all states.
class CStatusWindow final {
 public:
  using CommandHandler = std::function<void(StatusCommand)>;

  CStatusWindow() = default;
  ~CStatusWindow();

  CStatusWindow(const CStatusWindow&) = delete;
  CStatusWindow& operator=(const CStatusWindow&) = delete;

  // Call on the UI thread that owns the TSF text service.
  // owner may be nullptr. x/y are screen coordinates; CW_USEDEFAULT is
  // accepted.
  bool Create(HINSTANCE instance,
              HWND owner,
              const wchar_t* imeName,
              const InputStatus& initialStatus,
              CommandHandler handler,
              int x = CW_USEDEFAULT,
              int y = CW_USEDEFAULT);

  void Destroy();
  void Show();
  void Hide();
  bool IsVisible() const;

  // Update visual state from CTextService. This does not invoke the handler.
  void UpdateStatus(const InputStatus& status);

  // Save/restore the floating position in the caller's configuration.
  bool GetPosition(POINT* point) const;
  void SetPosition(int x, int y);

  HWND GetHwnd() const { return _hwnd; }

 private:
  static const wchar_t* WindowClassName;

  static LRESULT CALLBACK StaticWndProc(HWND hwnd,
                                        UINT msg,
                                        WPARAM wParam,
                                        LPARAM lParam);
  LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);

  struct Button {
    RECT rect{};
    StatusCommand command{};
    wchar_t text[16]{};
    bool enabled = true;
  };

  void RebuildLayout();
  void Paint();
  void DrawButton(HDC dc, const Button& button, bool hot) const;
  int HitTestButton(POINT point) const;
  void Invoke(StatusCommand command);
  void ShowContextMenu(POINT screenPoint);
  UINT Dpi() const;
  int Scale(int value) const;

  HWND _hwnd = nullptr;
  HWND _owner = nullptr;
  HINSTANCE _instance = nullptr;
  HFONT _font = nullptr;
  HFONT _titleFont = nullptr;
  CommandHandler _handler;
  InputStatus _status;
  wchar_t _imeName[64] = L"输入法";
  Button _buttons[5]{};
  int _buttonCount = 0;
  int _hotButton = -1;
  int _pressedButton = -1;
  bool _trackingMouse = false;
  bool _created = false;

  HWND _tooltip = nullptr;
  void _RebuildTooltips();
  std::wstring _TooltipText(int index) const;
};
