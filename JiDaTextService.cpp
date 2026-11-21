#include "JiDaTextService.h"
#include <ctfutb.h>
#include <cwctype>
#include "DpiHelper.h"
#include "EditSessions.h"
#include "SettingWindow.h"

static wchar_t VkToChar(WPARAM vk, LPARAM lParam) {
  BYTE ks[256];
  if (!GetKeyboardState(ks))
    return 0;
  wchar_t buf[4] = {0};
  int n = ToUnicode((UINT)vk, (UINT)((lParam >> 16) & 0xFF), ks, buf, 2, 0);
  return (n == 1) ? buf[0] : 0;
}

CJiDaTextService::CJiDaTextService() = default;
CJiDaTextService::~CJiDaTextService() {
  if (_activeCtx) {
    _activeCtx->Release();
    _activeCtx = nullptr;
  }
}

STDMETHODIMP CJiDaTextService::QueryInterface(REFIID riid, void** ppv) {
  if (!ppv)
    return E_INVALIDARG;
  *ppv = nullptr;
  if (riid == IID_IUnknown || riid == IID_ITfTextInputProcessor)
    *ppv = static_cast<ITfTextInputProcessor*>(this);
  else if (riid == IID_ITfKeyEventSink)
    *ppv = static_cast<ITfKeyEventSink*>(this);
  else if (riid == IID_ITfThreadMgrEventSink)
    *ppv = static_cast<ITfThreadMgrEventSink*>(this);
  else
    return E_NOINTERFACE;
  AddRef();
  return S_OK;
}
STDMETHODIMP_(ULONG) CJiDaTextService::AddRef() {
  return InterlockedIncrement(&_ref);
}
STDMETHODIMP_(ULONG) CJiDaTextService::Release() {
  ULONG u = InterlockedDecrement(&_ref);
  if (!u)
    delete this;
  return u;
}

STDMETHODIMP CJiDaTextService::Activate(ITfThreadMgr* ptim, TfClientId tid) {
  if (!ptim)
    return E_INVALIDARG;
  if (_threadMgr)
    return S_OK;  // 防止重复 Activate

  _threadMgr = ptim;
  _threadMgr->AddRef();
  _clientId = tid;

  EnablePerMonitorV2();

  wchar_t buf[128];
  swprintf_s(buf, L"[JiDa] Activate tid=%u\n", (unsigned)_clientId);
  OutputDebugStringW(buf);

  // ---------------- 1. 按键事件 sink ----------------
  ITfKeystrokeMgr* keyMgr = nullptr;
  if (SUCCEEDED(
          _threadMgr->QueryInterface(IID_ITfKeystrokeMgr, (void**)&keyMgr))) {
    keyMgr->AdviseKeyEventSink(_clientId, static_cast<ITfKeyEventSink*>(this),
                               TRUE);
    keyMgr->Release();
  } else {
    OutputDebugStringW(L"[JiDa] Activate: QI ITfKeystrokeMgr FAILED\n");
  }

  // ---------------- 2. 线程事件 sink（必须走 ITfSource） ----------------
  ITfSource* source = nullptr;
  if (SUCCEEDED(_threadMgr->QueryInterface(IID_ITfSource, (void**)&source))) {
    HRESULT hrSink = source->AdviseSink(
        IID_ITfThreadMgrEventSink, static_cast<ITfThreadMgrEventSink*>(this),
        &_sinkCookie);
    if (FAILED(hrSink)) {
      OutputDebugStringW(L"[JiDa] Activate: AdviseSink FAILED\n");
      _sinkCookie = TF_INVALID_COOKIE;
    }
    source->Release();
  }

  // ---------------- 3. 候选窗口 ----------------
  _candWnd = std::make_unique<CCandidateWindow>();
  if (!_candWnd->Create(g_hInstance)) {
    OutputDebugStringW(L"[JiDa] Activate: candidate window create FAILED\n");
    // 候选窗口失败不致命，继续尝试
  }

  // 鼠标点选候选：提交该项并清空编码
  _candWnd->OnPick = [this](int idx) {
    if (!_activeCtx)
      return;
    if (idx < 0 || idx >= (int)_engine.Candidates().size())
      return;
    std::wstring text = _engine.Candidates()[idx].text;
    _CommitAndEnd(_activeCtx, text);
    _engine.Reset();
    _HideCandidates();
  };

  // ---------------- 4. 状态栏 ----------------
  _status = InputStatus{};
  _statusWnd = std::make_unique<CStatusWindow>();

  bool statusCreated =
      _statusWnd->Create(g_hInstance, nullptr, L"极达", _status,
                         [this](StatusCommand cmd) { _OnStatusCommand(cmd); });

  if (!statusCreated) {
    OutputDebugStringW(L"[JiDa] Activate: status window create FAILED\n");
    _statusWnd.reset();
  } else {
    OutputDebugStringW(L"[JiDa] Activate: status window created OK\n");
    // Create 内部已经显示。
    // 如果配置为隐藏（用户主动关闭过），才在这里藏起来
    if (!g_config.showStatusBar)
      _statusWnd->Hide();
  }

  // ---------------- 5. 初始化引擎状态 ----------------
  _engine.Reset();
  if (_activeCtx) {
    _activeCtx->Release();
    _activeCtx = nullptr;
  }

  return S_OK;
}

STDMETHODIMP CJiDaTextService::Deactivate() {
  _Cancel();

  if (_threadMgr) {
    ITfKeystrokeMgr* keyMgr = nullptr;
    if (SUCCEEDED(
            _threadMgr->QueryInterface(IID_ITfKeystrokeMgr, (void**)&keyMgr))) {
      keyMgr->UnadviseKeyEventSink(_clientId);
      keyMgr->Release();
    }

    if (_sinkCookie != TF_INVALID_COOKIE) {
      ITfSource* source = nullptr;
      if (SUCCEEDED(
              _threadMgr->QueryInterface(IID_ITfSource, (void**)&source))) {
        source->UnadviseSink(_sinkCookie);
        source->Release();
      }
      _sinkCookie = TF_INVALID_COOKIE;
    }

    _threadMgr->Release();
    _threadMgr = nullptr;
  }

  if (_candWnd) {
    _candWnd->Destroy();
    _candWnd.reset();
  }
  if (_statusWnd) {
    _statusWnd->Destroy();
    _statusWnd.reset();
  }

  if (_activeCtx) {
    _activeCtx->Release();
    _activeCtx = nullptr;
  }

  return S_OK;
}

STDMETHODIMP CJiDaTextService::OnSetFocus(BOOL) {
  return S_OK;
}

STDMETHODIMP CJiDaTextService::OnTestKeyDown(ITfContext*,
                                             WPARAM wParam,
                                             LPARAM,
                                             BOOL* pfEaten) {
  *pfEaten = FALSE;
  // 英文模式：任何按键都交给宿主处理
  if (!_status.chinese)
    return S_OK;

  bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
  if (shift && wParam >= 'A' && wParam <= 'Z')
    return S_OK;

  bool hasInput = !_engine.Buffer().empty() || _engine.IsWaitingDateOrAmount();
  bool hasCand = !_engine.Candidates().empty();

  // 有输入上下文时，这些键可能被消费
  if (wParam >= 'A' && wParam <= 'Z') {
    *pfEaten = TRUE;
    return S_OK;
  }
  if (wParam >= '0' && wParam <= '9') {
    if (hasInput && hasCand)
      *pfEaten = TRUE;
    return S_OK;
  }

  // ESC 和回车：只有在有编码/等待态时才吞，否则交给宿主
  if (wParam == VK_ESCAPE || wParam == VK_RETURN || wParam == VK_SPACE ||
      wParam == VK_BACK) {
    if (hasInput)
      *pfEaten = TRUE;
    return S_OK;
  }
  // 上下左右：有候选时用于导航
  if (wParam == VK_UP || wParam == VK_DOWN || wParam == VK_LEFT ||
      wParam == VK_RIGHT) {
    if (hasInput && hasCand)
      *pfEaten = TRUE;
    return S_OK;
  }
  // 翻页
  if (wParam == VK_PRIOR || wParam == VK_NEXT) {
    if (hasInput && hasCand)
      *pfEaten = TRUE;
    return S_OK;
  }
  if (wParam == VK_OEM_MINUS || wParam == VK_OEM_PLUS) {
    if (hasInput && hasCand)
      *pfEaten = TRUE;
    return S_OK;
  }
  // 标点 / 分号
  if (wParam == VK_OEM_1 || wParam == VK_OEM_COMMA || wParam == VK_OEM_PERIOD ||
      wParam == VK_OEM_2 || wParam == VK_OEM_3 || wParam == VK_OEM_4 ||
      wParam == VK_OEM_5 || wParam == VK_OEM_6 || wParam == VK_OEM_7) {
    if (hasInput)
      *pfEaten = TRUE;
    return S_OK;
  }
  return S_OK;
}

STDMETHODIMP CJiDaTextService::OnKeyDown(ITfContext* pic,
                                         WPARAM wParam,
                                         LPARAM lParam,
                                         BOOL* pfEaten) {
  *pfEaten = FALSE;
  if (!_status.chinese)
    return S_OK;

  if (_activeCtx) {
    _activeCtx->Release();
    _activeCtx = nullptr;
  }
  _activeCtx = pic;
  _activeCtx->AddRef();

  bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
  if (shift && wParam >= 'A' && wParam <= 'Z')
    return S_OK;

  // 上下键：竖向布局时移动选中，横向布局时翻页
  if (wParam == VK_UP || wParam == VK_DOWN) {
    if (!_engine.Candidates().empty()) {
      *pfEaten = TRUE;
      bool vert = (g_config.candLayout == CandLayout::Vertical);
      if (vert)
        _engine.MoveSelection(wParam == VK_UP ? -1 : 1);
      else
        _engine.MovePage(wParam == VK_UP ? -1 : 1);
      _ShowCandidates(pic);
    }
    return S_OK;
  }
  // 左右键：横向布局时移动选中，竖向布局时翻页
  if (wParam == VK_LEFT || wParam == VK_RIGHT) {
    if (!_engine.Candidates().empty()) {
      *pfEaten = TRUE;
      bool vert = (g_config.candLayout == CandLayout::Vertical);
      if (!vert)
        _engine.MoveSelection(wParam == VK_LEFT ? -1 : 1);
      else
        _engine.MovePage(wParam == VK_LEFT ? -1 : 1);
      _ShowCandidates(pic);
    }
    return S_OK;
  }
  // 翻页
  if (wParam == VK_PRIOR || wParam == VK_OEM_MINUS) {
    if (!_engine.Candidates().empty()) {
      *pfEaten = TRUE;
      _engine.MovePage(-1);
      _ShowCandidates(pic);
    }
    return S_OK;
  }
  if (wParam == VK_NEXT || wParam == VK_OEM_PLUS) {
    if (!_engine.Candidates().empty()) {
      *pfEaten = TRUE;
      _engine.MovePage(1);
      _ShowCandidates(pic);
    }
    return S_OK;
  }

  // 字母
  if (wParam >= 'A' && wParam <= 'Z') {
    std::wstring committed, newBuffer;
    EngineAction act = _engine.OnLetter((wchar_t)wParam, committed, newBuffer);
    if (act != EngineAction::None)
      *pfEaten = TRUE;
    _HandleLetterResult(pic, act, committed, newBuffer);
    return S_OK;
  }

  // 数字选词
  if (wParam >= '1' && wParam <= '9') {
    if (_engine.IsWaitingDateOrAmount()) {
      std::wstring committed;
      EngineAction act = _engine.OnSemicolonTail((wchar_t)wParam, committed);
      if (act == EngineAction::Commit) {
        *pfEaten = TRUE;
        _CommitAndEnd(pic, committed);
        _HideCandidates();
      } else if (act == EngineAction::UpdateCandidates) {
        *pfEaten = TRUE;
        _SyncComposition(pic);
        _ShowCandidates(pic);
      }
      return S_OK;
    }
    std::wstring committed;
    if (_engine.OnDigit((int)(wParam - '0'), committed)) {
      *pfEaten = TRUE;
      _CommitAndEnd(pic, committed);
      _HideCandidates();
    }
    return S_OK;
  }

  // 空格：提交高亮项
  if (wParam == VK_SPACE) {
    if (_engine.IsWaitingDateOrAmount()) {
      *pfEaten = TRUE;
      return S_OK;
    }
    std::wstring committed;
    if (_engine.OnSpace(committed)) {
      *pfEaten = TRUE;
      _CommitAndEnd(pic, committed);
      _HideCandidates();
    }
    return S_OK;
  }

  // ESC / 回车：取消输入
  if (wParam == VK_ESCAPE || wParam == VK_RETURN) {
    if (!_engine.Buffer().empty() || _engine.IsWaitingDateOrAmount()) {
      *pfEaten = TRUE;
      _CancelWithContext(pic);
      _HideCandidates();
    }
    return S_OK;
  }

  // 退格
  if (wParam == VK_BACK) {
    if (!_engine.Buffer().empty() && !_engine.Candidates().empty()) {
      *pfEaten = TRUE;
      _engine.OnBackspace();
      if (_engine.Buffer().empty()) {
        _CancelWithContext(pic);
        _HideCandidates();
      } else {
        _SyncComposition(pic);
        _ShowCandidates(pic);
      }
    } else if (!_engine.Buffer().empty() || _engine.IsWaitingDateOrAmount()) {
      // 无候选但有编码：一次性清空编码，不吞键让宿主删除文档字
      *pfEaten = FALSE;
      _CancelWithContext(pic);
      _HideCandidates();
    }
    // buffer 为空：pfEaten 保持 FALSE，宿主正常处理退格
    return S_OK;
  }

  // 分号：有编码 -> 选第二重码；无编码 -> 交给宿主输入分号
  if (wParam == VK_OEM_1) {
    if (_engine.IsWaitingDateOrAmount()) {
      std::wstring committed;
      EngineAction act = _engine.OnSemicolonTail(L';', committed);
      if (act == EngineAction::Commit) {
        *pfEaten = TRUE;
        _CommitAndEnd(pic, committed);
        _HideCandidates();
      } else {
        *pfEaten = TRUE;
        _SyncComposition(pic);
      }
      return S_OK;
    }
    if (!_engine.Buffer().empty()) {
      std::wstring committed;
      bool wait = false;
      bool consumed = _engine.OnSemicolon(committed, wait);
      if (consumed) {
        *pfEaten = TRUE;
        _CommitAndEnd(pic, committed);
        _HideCandidates();
        return S_OK;
      }
      _CancelWithContext(pic);
      _HideCandidates();
    }
    return S_OK;
  }

  // 日期/金额等待态下的数字/点
  if (_engine.IsWaitingDateOrAmount() &&
      (iswdigit((wchar_t)wParam) || wParam == VK_OEM_PERIOD)) {
    std::wstring committed;
    EngineAction act = _engine.OnSemicolonTail((wchar_t)wParam, committed);
    if (act == EngineAction::Commit) {
      *pfEaten = TRUE;
      _CommitAndEnd(pic, committed);
      _HideCandidates();
    } else {
      *pfEaten = TRUE;
      _SyncComposition(pic);
    }
    return S_OK;
  }

  // 标点顶屏：用真实字符，不传 VK 码
  if (!_engine.Buffer().empty()) {
    wchar_t ch = VkToChar(wParam, lParam);
    std::wstring punct, firstCand;
    if (ch && _engine.OnPunctuation(ch, punct, firstCand)) {
      *pfEaten = TRUE;
      if (!firstCand.empty())
        _CommitAndEnd(pic, firstCand);
      else
        _CancelWithContext(pic);
      _CommitAndEnd(pic, punct);
      _engine.Reset();
      _HideCandidates();
      return S_OK;
    }
    _CancelWithContext(pic);
    _HideCandidates();
  }
  return S_OK;
}

void CJiDaTextService::_HandleLetterResult(ITfContext* pic,
                                           EngineAction act,
                                           std::wstring& committed,
                                           std::wstring& newBuffer) {
  switch (act) {
    case EngineAction::Commit:
      _CommitAndEnd(pic, committed);
      _HideCandidates();
      break;
    case EngineAction::TopCommitAndContinue:
      _CommitAndEnd(pic, committed);
      _EnsureComposition(pic);
      _SyncComposition(pic);
      _ShowCandidates(pic);
      break;
    case EngineAction::UpdateCandidates:
      _EnsureComposition(pic);
      _SyncComposition(pic);
      _ShowCandidates(pic);
      break;
    default:
      break;
  }
}

void CJiDaTextService::_CommitAndEnd(ITfContext* pic,
                                     const std::wstring& text) {
  if (text.empty())
    return;
  if (_clientId == TF_CLIENTID_NULL)
    return;

  auto* s = new CCommitSession(pic, _composition, text);
  HRESULT phrSession = E_FAIL;
  pic->RequestEditSession(_clientId, s, TF_ES_SYNC | TF_ES_READWRITE,
                          &phrSession);
  s->Release();
  if (_composition) {
    _composition->Release();
    _composition = nullptr;
  }
}

void CJiDaTextService::_EnsureComposition(ITfContext* pic) {
  if (_composition)
    return;
  if (_clientId == TF_CLIENTID_NULL)
    return;
  ITfComposition* comp = nullptr;
  auto* s = new CStartCompositionSession(pic, _engine.Buffer(), &comp);
  HRESULT phrSession = E_FAIL;
  pic->RequestEditSession(_clientId, s, TF_ES_SYNC | TF_ES_READWRITE,
                          &phrSession);
  s->Release();
  _composition = comp;
}

void CJiDaTextService::_SyncComposition(ITfContext* pic) {
  if (!g_config.inlinePreedit) {
    if (!_composition)
      _EnsureComposition(pic);
    return;
  }
  if (!_composition) {
    _EnsureComposition(pic);
    return;
  }
  auto* s = new CUpdateCompositionSession(pic, _composition, _engine.Buffer());
  HRESULT phrSession = E_FAIL;
  pic->RequestEditSession(_clientId, s, TF_ES_SYNC | TF_ES_READWRITE,
                          &phrSession);
  s->Release();
}

void CJiDaTextService::_ShowCandidates(ITfContext* pic) {
  if (!_candWnd)
    return;

  if (_engine.Buffer().empty()) {
    _candWnd->Hide();
    return;
  }

  _candWnd->SetTitle(_engine.Buffer());

  RECT rcCaret = {};
  bool bPositioned = false;

  // 创建 Edit Session
  auto* session = new CCaretPositionEditSession(pic);
  if (session) {
    HRESULT hrSession = E_FAIL;

    // 请求同步只读 Edit Session
    HRESULT hr = pic->RequestEditSession(_clientId, session,
                                         TF_ES_READ | TF_ES_SYNC, &hrSession);

    if (SUCCEEDED(hr) && SUCCEEDED(hrSession)) {
      bPositioned = session->GetPosition(&rcCaret);
    }

    session->Release();
  }

  // 获取失败时才使用兜底位置
  if (!bPositioned) {
    HWND hwndHost = nullptr;
    ITfContextView* view = nullptr;

    if (SUCCEEDED(pic->GetActiveView(&view)) && view) {
      view->GetWnd(&hwndHost);
      view->Release();
    }

    if (hwndHost) {
      POINT pt = {};
      ClientToScreen(hwndHost, &pt);

      rcCaret = {pt.x, pt.y, pt.x + 1, pt.y + 1};
    } else {
      SystemParametersInfo(SPI_GETWORKAREA, 0, &rcCaret, 0);
      rcCaret.left += 20;
      rcCaret.top += 20;
      rcCaret.right = rcCaret.left + 1;
      rcCaret.bottom = rcCaret.top + 1;
    }
  }

  UINT dpi = GetDpiForRect(rcCaret);
  _candWnd->Show(_engine.Candidates(), _engine.SelectedIndex(), rcCaret, dpi);
}

void CJiDaTextService::_HideCandidates() {
  if (_candWnd)
    _candWnd->Hide();
}

void CJiDaTextService::_Cancel() {
  if (_composition) {
    _composition->Release();
    _composition = nullptr;
  }
  _engine.Reset();
}

void CJiDaTextService::_CancelWithContext(ITfContext* pic) {
  // 结束组合（提交空文本）——纯 Release 只减引用，不会把组合从文档里摘掉
  if (_composition) {
    auto* s = new CCommitSession(pic, _composition, L"");
    pic->RequestEditSession(_clientId, s, TF_ES_SYNC | TF_ES_READWRITE,
                            nullptr);
    s->Release();
    _composition->Release();
    _composition = nullptr;
  }
  _engine.Reset();
}

STDMETHODIMP CJiDaTextService::OnSetFocus(ITfDocumentMgr*, ITfDocumentMgr*) {
  _Cancel();
  _HideCandidates();
  if (_activeCtx) {
    _activeCtx->Release();
    _activeCtx = nullptr;
  }
  return S_OK;
}

void CJiDaTextService::_OnStatusCommand(StatusCommand cmd) {
  switch (cmd) {
    case StatusCommand::ToggleChinese:
      _status.chinese = !_status.chinese;
      break;
    case StatusCommand::ToggleFullShape:
      _status.fullShape = !_status.fullShape;
      break;
    case StatusCommand::TogglePunctuation:
      _status.chinesePunctuation = !_status.chinesePunctuation;
      break;
    case StatusCommand::ToggleTraditional:
      _status.traditional = !_status.traditional;
      break;
    case StatusCommand::OpenSettings:
      _OpenSettingsWindow();
      return;
    case StatusCommand::HideStatusWindow:
      g_config.showStatusBar = false;
      SaveJiDaConfig();
      if (_statusWnd)
        _statusWnd->Hide();
      return;
  }
  if (_statusWnd)
    _statusWnd->UpdateStatus(_status);
}

void CJiDaTextService::_OpenSettingsWindow() {
  CSettingWindow::Show(g_hInstance,
                       _statusWnd ? _statusWnd->GetHwnd() : nullptr, [this]() {
                         // 配置已写入 g_config 并持久化
                         // 1. 重建候选窗口字体与布局
                         if (_candWnd)
                           _candWnd->RefreshFromConfig();

                         // 2. 状态栏可见性
                         if (_statusWnd) {
                           if (g_config.showStatusBar)
                             _statusWnd->Show();
                           else
                             _statusWnd->Hide();
                         }

                         // 3. 丢弃当前编码，下次按键按新配置开始
                         _Cancel();
                         _HideCandidates();
                       });
}

void CJiDaTextService::_SyncStatusBarVisibility() {
  if (!_statusWnd)
    return;
  if (g_config.showStatusBar)
    _statusWnd->Show();
  else
    _statusWnd->Hide();
}