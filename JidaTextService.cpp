#include "JiDaTextService.h"
#include <ctfutb.h>
#include <cwctype>
#include "DpiHelper.h"
#include "EditSessions.h"

static wchar_t VkToChar(WPARAM vk, LPARAM lParam) {
  BYTE ks[256];
  if (!GetKeyboardState(ks))
    return 0;
  wchar_t buf[4] = {0};
  int n = ToUnicode((UINT)vk, (UINT)((lParam >> 16) & 0xFF), ks, buf, 2, 0);
  return (n == 1) ? buf[0] : 0;
}

// 判断 GetTextExt 返回的矩形是否是一个可用于定位的真实光标位置
static bool IsUsableCaretExtent(const RECT& rc) {
  // 1. 矩形不能全为零 (最典型的无效情况)
  if (rc.left == 0 && rc.top == 0 && rc.right == 0 && rc.bottom == 0)
    return false;

  // 2. 宽度和高度至少有一个不能为零 (光标通常至少有一个维度大于0)
  //    注意：在折叠选区时，宽度可能为0，但高度不应为0。
  //    一个更严格的检查是要求宽度和高度都大于0，但这可能会在
  //    某些零宽字符场景下误判。对于输入法定位，我们至少需要
  //    有一个维度是有效的。
  if (rc.right <= rc.left && rc.bottom <= rc.top)
    return false;  // 宽高都无效，彻底不可用

  // 3. (可选) 检查坐标是否在屏幕范围内，避免窗口跑到屏幕外
  //    GetTextExt 返回的是屏幕坐标，可以用 MonitorFromRect 验证
  HMONITOR hMon = MonitorFromRect(&rc, MONITOR_DEFAULTTONULL);
  if (hMon == nullptr)
    return false;  // 坐标不在任何显示器上，不可用

  return true;
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
  _threadMgr = ptim;
  _threadMgr->AddRef();
  _clientId = tid;
  EnablePerMonitorV2();

  ITfKeystrokeMgr* keyMgr = nullptr;
  if (SUCCEEDED(
          _threadMgr->QueryInterface(IID_ITfKeystrokeMgr, (void**)&keyMgr))) {
    keyMgr->AdviseKeyEventSink(_clientId, static_cast<ITfKeyEventSink*>(this),
                               TRUE);
    keyMgr->Release();
  }
  ITfSource* source = nullptr;
  if (SUCCEEDED(_threadMgr->QueryInterface(IID_ITfSource, (void**)&source))) {
    source->AdviseSink(IID_ITfThreadMgrEventSink,
                       static_cast<ITfThreadMgrEventSink*>(this), &_sinkCookie);
    source->Release();
  }

  _candWnd = std::make_unique<CCandidateWindow>();
  _candWnd->Create(g_hInstance);
  // 鼠标点击候选：选中该候选并提交
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
  if (_candWnd)
    _candWnd->Destroy();
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

  RECT rcCaret = {0, 0, 0, 0};
  bool bPositioned = false;  // 标记是否成功获得了有效的光标位置

  ITfContextView* view = nullptr;
  if (SUCCEEDED(pic->GetActiveView(&view)) && view) {
    ITfRange* range = nullptr;
    TF_SELECTION sel;
    ULONG n = 0;
    // 获取当前选择范围（光标位置）
    if (SUCCEEDED(pic->GetSelection(TF_INVALID_EDIT_COOKIE,
                                    TF_DEFAULT_SELECTION, 1, &sel, &n)) &&
        n) {
      range = sel.range;
    }

    if (range) {
      RECT r = {0, 0, 0, 0};
      BOOL clipped = FALSE;
      HRESULT hrExt =
          view->GetTextExt(TF_INVALID_EDIT_COOKIE, range, &r, &clipped);

      // 核心判断：只有返回成功 且 矩形可用 时，才认为定位有效
      if (SUCCEEDED(hrExt) && IsUsableCaretExtent(r)) {
        rcCaret = r;
        bPositioned = true;
      } else {
        // 可以在这里加日志，方便调试是哪个宿主/场景导致失效
        // OutputDebugStringW(L"[JiDa] GetTextExt returned invalid rect or
        // failed. Falling back.\n");
      }
      range->Release();
    }
    view->Release();
  }

  // --- 兜底定位策略 ---
  if (!bPositioned) {
    // 策略 A (推荐): 使用宿主窗口的客户区左上角作为后备位置
    // 这需要你能拿到宿主窗口的 HWND。在 TSF 中可以通过 ITfContextView::GetWnd
    // 获取。
    HWND hwndHost = nullptr;
    if (SUCCEEDED(pic->GetActiveView(&view)) && view) {  // 重新获取 view
      view->GetWnd(&hwndHost);
      view->Release();
    }

    if (hwndHost != nullptr) {
      POINT pt = {0, 0};
      ClientToScreen(hwndHost, &pt);  // 转换为屏幕坐标
      rcCaret.left = pt.x;
      rcCaret.top = pt.y;
      rcCaret.right = pt.x + 1;  // 给一个 1x1 的小区域
      rcCaret.bottom = pt.y + 1;
    } else {
      // 策略 B (终极兜底): 放在屏幕工作区的左上角稍靠内，避免完全贴边
      SystemParametersInfo(SPI_GETWORKAREA, 0, &rcCaret, 0);
      // 在左上角基础上偏移一点，避免和系统 UI 重叠
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