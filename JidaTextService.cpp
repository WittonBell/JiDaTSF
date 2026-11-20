#include "JidaTextService.h"
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
  if (n == 1)
    return buf[0];
  return 0;
}

CJidaTextService::CJidaTextService() = default;
CJidaTextService::~CJidaTextService() {
  if (_activeCtx) {
    _activeCtx->Release();
    _activeCtx = nullptr;
  }
}

STDMETHODIMP CJidaTextService::QueryInterface(REFIID riid, void** ppv) {
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
STDMETHODIMP_(ULONG) CJidaTextService::AddRef() {
  return InterlockedIncrement(&_ref);
}
STDMETHODIMP_(ULONG) CJidaTextService::Release() {
  ULONG u = InterlockedDecrement(&_ref);
  if (!u)
    delete this;
  return u;
}

STDMETHODIMP CJidaTextService::Activate(ITfThreadMgr* ptim, TfClientId tid) {
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
  _candWnd->OnPick = [this](const std::wstring& text) {
    if (_activeCtx)
      _CommitAndEnd(_activeCtx, text);
    _engine.Reset();
    _HideCandidates();
  };
  return S_OK;
}

STDMETHODIMP CJidaTextService::Deactivate() {
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

STDMETHODIMP CJidaTextService::OnSetFocus(BOOL) {
  return S_OK;
}

STDMETHODIMP CJidaTextService::OnTestKeyDown(ITfContext*,
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
  if (wParam == VK_SPACE || wParam == VK_RETURN) {
    if (hasInput)
      *pfEaten = TRUE;
    return S_OK;
  }
  if (wParam == VK_BACK) {
    if (hasInput)
      *pfEaten = TRUE;
    return S_OK;
  }

  // 分号、标点：仅在已有编码时才可能被消费（顶字上屏）
  if (wParam == VK_OEM_1 || wParam == VK_OEM_COMMA || wParam == VK_OEM_PERIOD ||
      wParam == VK_OEM_2 || wParam == VK_OEM_3 || wParam == VK_OEM_4 ||
      wParam == VK_OEM_5 || wParam == VK_OEM_6 || wParam == VK_OEM_7) {
    if (hasInput)
      *pfEaten = TRUE;
    return S_OK;
  }

  // 翻页：有候选才吞
  if (wParam == VK_OEM_MINUS || wParam == VK_OEM_PLUS || wParam == VK_PRIOR ||
      wParam == VK_NEXT) {
    if (hasInput && hasCand)
      *pfEaten = TRUE;
    return S_OK;
  }
  return S_OK;
}

STDMETHODIMP CJidaTextService::OnKeyDown(ITfContext* pic,
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

  // 翻页
  if (wParam == VK_OEM_MINUS || wParam == VK_PRIOR) {
    if (_candWnd) {
      _candWnd->PageUp();
      _engine.SetPage(_candWnd->PageStart());
      _ShowCandidates(pic);
    }
    *pfEaten = TRUE;
    return S_OK;
  }
  if (wParam == VK_OEM_PLUS || wParam == VK_NEXT) {
    if (_candWnd) {
      _candWnd->PageDown();
      _engine.SetPage(_candWnd->PageStart());
      _ShowCandidates(pic);
    }
    *pfEaten = TRUE;
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

  // 空格
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

  // 回车
  if (wParam == VK_RETURN) {
    if (!_engine.Buffer().empty() && !_engine.IsWaitingDateOrAmount()) {
      std::wstring committed;
      if (_engine.OnEnter(committed)) {
        *pfEaten = TRUE;
        _CommitAndEnd(pic, committed);
        _HideCandidates();
      }
    }
    return S_OK;
  }

  // 退格
  if (wParam == VK_BACK) {
    if (!_engine.Buffer().empty() && !_engine.Candidates().empty()) {
      // 有候选：吞掉，逐个删字母
      *pfEaten = TRUE;
      _engine.OnBackspace();
      if (_engine.Buffer().empty()) {
        _Cancel();
        _HideCandidates();
      } else {
        _SyncComposition(pic);
        _ShowCandidates(pic);
      }
    } else if (!_engine.Buffer().empty() || _engine.IsWaitingDateOrAmount()) {
      // 无候选但有编码：一次性清空编码，不吞键让宿主删除文档字
      *pfEaten = FALSE;
      _engine.Reset();
      _Cancel();
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
      // 无第二重码：清空编码，让宿主输入分号
      _engine.Reset();
      _Cancel();
      _HideCandidates();
    }
    // pfEaten = FALSE，宿主输入分号
    return S_OK;
  }

  // 日期/金额等待态下的数字和点（保留）
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
        _Cancel();
      _CommitAndEnd(pic, punct);
      _engine.Reset();
      _HideCandidates();
      return S_OK;
    }
    // 编码存在但标点不在映射里：清空编码，让宿主正常输入这个标点
    _engine.Reset();
    _Cancel();
    _HideCandidates();
  }
  return S_OK;  // pfEaten = FALSE，宿主处理标点
}

void CJidaTextService::_HandleLetterResult(ITfContext* pic,
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

void CJidaTextService::_EnsureComposition(ITfContext* pic) {
  if (_composition)
    return;
  if (_clientId == TF_CLIENTID_NULL) {
    return;
  }
  ITfComposition* comp = nullptr;
  auto* s = new CStartCompositionSession(pic, _engine.Buffer(), &comp);

  HRESULT phrSession = E_FAIL;
  HRESULT hr = pic->RequestEditSession(
      _clientId, s, TF_ES_SYNC | TF_ES_READWRITE, &phrSession);
  s->Release();
  _composition = comp;
}

void CJidaTextService::_CommitAndEnd(ITfContext* pic,
                                     const std::wstring& text) {
  if (text.empty())
    return;
  if (_clientId == TF_CLIENTID_NULL)
    return;

  auto* s = new CCommitSession(pic, _composition, text);
  HRESULT phrSession = E_FAIL;
  HRESULT hr = pic->RequestEditSession(
      _clientId, s, TF_ES_SYNC | TF_ES_READWRITE, &phrSession);
  s->Release();

  if (_composition) {
    _composition->Release();
    _composition = nullptr;
  }
}

void CJidaTextService::_SyncComposition(ITfContext* pic) {
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

void CJidaTextService::_ShowCandidates(ITfContext* pic) {
  if (!_candWnd)
    return;
  // 只要 buffer 非空，就显示候选窗口（即使无候选，也显示编码标题）
  if (_engine.Buffer().empty()) {
    _candWnd->Hide();
    return;
  }

  _candWnd->SetTitle(_engine.Buffer());

  RECT rcCaret = {0, 0, 0, 0};
  ITfContextView* view = nullptr;
  if (SUCCEEDED(pic->GetActiveView(&view)) && view) {
    ITfRange* range = nullptr;
    if (_composition) {
      _composition->GetRange(&range);
    } else {
      TF_SELECTION sel;
      ULONG n = 0;
      if (SUCCEEDED(pic->GetSelection(TF_INVALID_EDIT_COOKIE,
                                      TF_DEFAULT_SELECTION, 1, &sel, &n)) &&
          n)
        range = sel.range;
    }
    if (range) {
      RECT r;
      BOOL clipped = FALSE;
      view->GetTextExt(TF_INVALID_EDIT_COOKIE, range, &r, &clipped);
      rcCaret = r;
      range->Release();
    }
    view->Release();
  }
  UINT dpi = GetDpiForRect(rcCaret);
  _candWnd->Show(_engine.Candidates(), rcCaret, dpi);
}

void CJidaTextService::_HideCandidates() {
  if (_candWnd)
    _candWnd->Hide();
}

void CJidaTextService::_Cancel() {
  if (_composition) {
    _composition->Release();
    _composition = nullptr;
  }
  _engine.Reset();
}

STDMETHODIMP CJidaTextService::OnSetFocus(ITfDocumentMgr*, ITfDocumentMgr*) {
  _Cancel();
  _HideCandidates();
  if (_activeCtx) {
    _activeCtx->Release();
    _activeCtx = nullptr;
  }
  return S_OK;
}
