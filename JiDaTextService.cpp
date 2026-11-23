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

// 按五笔规则生成词组编码：
//   2 字：各取前 2 码
//   3 字：第 1 字 1 码 + 第 2 字 1 码 + 第 3 字 2 码
//   4 字：各取前 1 码
//   5+ 字：第 1、2、3、末字各 1 码
static std::wstring BuildPhraseCode(const std::vector<PhraseEntry>& entries) {
  if (entries.empty())
    return L"";

  auto getFull = [](const PhraseEntry& e) -> const std::wstring& {
    return e.fullCode.empty() ? e.code : e.fullCode;
  };
  auto safeSub = [](const std::wstring& s, size_t pos, size_t len) {
    if (pos >= s.size())
      return std::wstring();
    return s.substr(pos, (std::min)(len, s.size() - pos));
  };

  size_t n = entries.size();
  std::wstring code;

  if (n == 1) {
    code = getFull(entries[0]);
  } else if (n == 2) {
    code =
        safeSub(getFull(entries[0]), 0, 2) + safeSub(getFull(entries[1]), 0, 2);
  } else if (n == 3) {
    code = safeSub(getFull(entries[0]), 0, 1) +
           safeSub(getFull(entries[1]), 0, 1) +
           safeSub(getFull(entries[2]), 0, 2);
  } else if (n == 4) {
    for (size_t i = 0; i < 4; ++i)
      code += safeSub(getFull(entries[i]), 0, 1);
  } else {
    code = safeSub(getFull(entries[0]), 0, 1) +
           safeSub(getFull(entries[1]), 0, 1) +
           safeSub(getFull(entries[2]), 0, 1) +
           safeSub(getFull(entries[n - 1]), 0, 1);
  }

  if (code.size() > 4)
    code.resize(4);
  return code;
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

  LoadJiDaTableAsync();

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

  _candWnd->OnContextMenu = [this](POINT pt) { _ShowStatusContextMenu(pt); };

  _phraseWnd = std::make_unique<CPhraseWindow>();
  if (!_phraseWnd->Create(g_hInstance)) {
    OutputDebugStringW(L"[JiDa] Activate: phrase window create FAILED\n");
    _phraseWnd.reset();
  }

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

  if (_phraseWnd) {
    _phraseWnd->Destroy();
    _phraseWnd.reset();
  }

  return S_OK;
}

STDMETHODIMP CJiDaTextService::OnSetFocus(BOOL) {
  return S_OK;
}

STDMETHODIMP CJiDaTextService::OnTestKeyDown(ITfContext* pic,
                                             WPARAM wParam,
                                             LPARAM,
                                             BOOL* pfEaten) {
  *pfEaten = FALSE;

  // 1. 造词窗口可见 → 独占
  if (_phraseWnd && _phraseWnd->IsVisible()) {
    *pfEaten = TRUE;
    return S_OK;
  }

  // 2. Ctrl+= → 造词（临时英文模式下不接管）
  if (!_tempEnglishMode && (GetKeyState(VK_CONTROL) & 0x8000) &&
      (wParam == VK_OEM_PLUS || wParam == VK_ADD)) {
    *pfEaten = TRUE;
    return S_OK;
  }

  // 3. Ctrl / Alt / Win 组合键 → 交给宿主
  if (GetKeyState(VK_CONTROL) & 0x8000)
    return S_OK;
  if (GetKeyState(VK_MENU) & 0x8000)
    return S_OK;
  if (GetKeyState(VK_LWIN) & 0x8000)
    return S_OK;
  if (GetKeyState(VK_RWIN) & 0x8000)
    return S_OK;

  // 4. 临时英文模式：吞下字母 / 数字 / 空格 / 退格 / 回车 / ESC
  if (_tempEnglishMode) {
    if ((wParam >= 'A' && wParam <= 'Z') || (wParam >= '0' && wParam <= '9') ||
        wParam == VK_SPACE || wParam == VK_BACK || wParam == VK_RETURN ||
        wParam == VK_ESCAPE) {
      *pfEaten = TRUE;
    } else if (wParam == VK_OEM_COMMA || wParam == VK_OEM_PERIOD ||
               wParam == VK_OEM_1 || wParam == VK_OEM_2 || wParam == VK_OEM_3 ||
               wParam == VK_OEM_4 || wParam == VK_OEM_5 || wParam == VK_OEM_6 ||
               wParam == VK_OEM_7) {
      *pfEaten = TRUE;
    }
    return S_OK;
  }

  // 5. Shift + 字母
  bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
  if (shift && wParam >= 'A' && wParam <= 'Z') {
    if (g_config.shiftDirectCommit)
      return S_OK;    // 开关打开 → 宿主输出大写
    *pfEaten = TRUE;  // 开关关闭 → 进入临时英文（在 OnKeyDown 处理）
    return S_OK;
  }

  // 右Shift键切换中英文
  if ((GetKeyState(VK_RSHIFT) & 0x8000) != 0) {
    _OnStatusCommand(StatusCommand::ToggleChinese);
    if (!_status.chinese) {
      // 如果不再是中文模式，但有候选框，则关闭
      if (!_engine.Buffer().empty() || _engine.IsWaitingDateOrAmount()) {
        _CancelWithContext(pic);
        _HideCandidates();
      }
    }
    return S_OK;
  }

  // 6. 英文模式
  if (!_status.chinese) {
    if (_status.fullShape) {
      bool eat = false;
      if (wParam >= 'A' && wParam <= 'Z')
        eat = true;
      else if (wParam >= '0' && wParam <= '9')
        eat = true;
      else if (wParam == VK_SPACE)
        eat = true;
      else if (wParam == VK_OEM_COMMA || wParam == VK_OEM_PERIOD ||
               wParam == VK_OEM_1 || wParam == VK_OEM_2 || wParam == VK_OEM_3 ||
               wParam == VK_OEM_4 || wParam == VK_OEM_5 || wParam == VK_OEM_6 ||
               wParam == VK_OEM_7)
        eat = true;
      if (eat)
        *pfEaten = TRUE;
    }
    return S_OK;
  }

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
  if (wParam == VK_PRIOR || wParam == VK_NEXT || wParam == VK_OEM_MINUS ||
      wParam == VK_OEM_PLUS) {
    if (hasInput && hasCand)
      *pfEaten = TRUE;
    return S_OK;
  }
  // 标点键：有编码 / 中文标点 / 全角 —— 任意一项为真都要吞
  if (wParam == VK_OEM_1 || wParam == VK_OEM_COMMA || wParam == VK_OEM_PERIOD ||
      wParam == VK_OEM_2 || wParam == VK_OEM_3 || wParam == VK_OEM_4 ||
      wParam == VK_OEM_5 || wParam == VK_OEM_6 || wParam == VK_OEM_7) {
    if (hasInput || _status.chinesePunctuation || _status.fullShape)
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

  // ---------------- 英文模式 ----------------
  if (!_status.chinese) {
    if (_status.fullShape) {
      wchar_t ch = 0;
      if (wParam == VK_SPACE) {
        ch = L' ';
      } else {
        ch = VkToChar(wParam, lParam);  // 已处理 Shift 大小写
      }
      if (ch) {
        *pfEaten = TRUE;
        _CommitAndEnd(pic, std::wstring(1, ch));
      }
    }
    return S_OK;
  }

  // 1. 造词窗口可见 → 独占
  if (_phraseWnd && _phraseWnd->IsVisible()) {
    *pfEaten = TRUE;
    _phraseWnd->HandleKey(wParam);
    return S_OK;
  }

  // 2. Ctrl / Alt / Win 修饰键放行（Ctrl+= 除外）
  if (GetKeyState(VK_CONTROL) & 0x8000) {
    // Ctrl + 加号：弹出 / 关闭造词窗口
    if (!_tempEnglishMode && (wParam == VK_OEM_PLUS || wParam == VK_ADD)) {
      *pfEaten = TRUE;
      _ShowPhraseWindow(pic);
    }
    return S_OK;
  }
  if (GetKeyState(VK_MENU) & 0x8000)
    return S_OK;
  if (GetKeyState(VK_LWIN) & 0x8000)
    return S_OK;
  if (GetKeyState(VK_RWIN) & 0x8000)
    return S_OK;

  // 3. 临时英文模式处理
  if (_tempEnglishMode) {
    *pfEaten = TRUE;

    if (wParam == VK_RETURN) {
      if (!_tempEnglishBuffer.empty())
        _CommitAndEnd(pic, _tempEnglishBuffer);
      _tempEnglishBuffer.clear();
      _tempEnglishMode = false;
      _HideCandidates();
      return S_OK;
    }
    if (wParam == VK_ESCAPE) {
      _tempEnglishBuffer.clear();
      _tempEnglishMode = false;
      _HideCandidates();
      return S_OK;
    }
    if (wParam == VK_BACK) {
      if (!_tempEnglishBuffer.empty()) {
        _tempEnglishBuffer.pop_back();
        if (_tempEnglishBuffer.empty()) {
          _tempEnglishMode = false;
          _HideCandidates();
        } else {
          _ShowTempEnglish(pic);
        }
      }
      return S_OK;
    }
    if (wParam == VK_SPACE) {
      _tempEnglishBuffer += L' ';
      _ShowTempEnglish(pic);
      return S_OK;
    }
    if ((wParam >= 'A' && wParam <= 'Z') || (wParam >= '0' && wParam <= '9')) {
      wchar_t ch = VkToChar(wParam, lParam);
      if (ch) {
        _tempEnglishBuffer += ch;
        _ShowTempEnglish(pic);
      }
      return S_OK;
    }
    // ---------------- 标点处理 ----------------
    bool isPunctKey =
        (wParam == VK_OEM_1 || wParam == VK_OEM_COMMA ||
         wParam == VK_OEM_PERIOD || wParam == VK_OEM_2 || wParam == VK_OEM_3 ||
         wParam == VK_OEM_4 || wParam == VK_OEM_5 || wParam == VK_OEM_6 ||
         wParam == VK_OEM_7);
    if (isPunctKey) {
      wchar_t ch = VkToChar(wParam, lParam);
      if (ch) {
        _tempEnglishBuffer += ch;
        _ShowTempEnglish(pic);
      }
    }
    return S_OK;
  }

  // 4.Shift + 字母：开关关闭时进入临时英文
  bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
  if (shift && wParam >= 'A' && wParam <= 'Z') {
    if (g_config.shiftDirectCommit)
      return S_OK;  // 宿主输出大写
    // 进入临时英文（先清空已有编码）
    if (!_engine.Buffer().empty()) {
      _CancelWithContext(pic);
      _HideCandidates();
    }
    _tempEnglishMode = true;
    _tempEnglishBuffer.clear();
    wchar_t ch = VkToChar(wParam, lParam);
    if (ch)
      _tempEnglishBuffer += ch;
    *pfEaten = TRUE;
    _ShowTempEnglish(pic);
    return S_OK;
  }

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

  // ---------------- 标点处理 ----------------
  bool isPunctKey =
      (wParam == VK_OEM_1 || wParam == VK_OEM_COMMA ||
       wParam == VK_OEM_PERIOD || wParam == VK_OEM_2 || wParam == VK_OEM_3 ||
       wParam == VK_OEM_4 || wParam == VK_OEM_5 || wParam == VK_OEM_6 ||
       wParam == VK_OEM_7);

  // 情况 1：有编码 -> 顶字上屏后输出标点
  if (!_engine.Buffer().empty()) {
    wchar_t ch = VkToChar(wParam, lParam);
    std::wstring punct, firstCand;
    if (ch && _engine.OnPunctuation(ch, punct, firstCand)) {
      *pfEaten = TRUE;
      if (!firstCand.empty())
        _CommitAndEnd(pic, firstCand);
      else
        _CancelWithContext(pic);

      std::wstring outputPunct;
      if (_status.chinesePunctuation) {
        if (ch == L'\'' || ch == L'"')
          outputPunct = _ProcessQuote(ch);  // 引号配对
        else
          outputPunct = punct;  // 其他标点用映射表
      } else {
        outputPunct = std::wstring(1, ch);  // 半角
      }

      _CommitAndEnd(pic, outputPunct);
      _engine.Reset();
      _HideCandidates();
      return S_OK;
    }
    // 非映射标点（如 Shift+, 得到 '<'）：保留编码，按键交给宿主
    return S_OK;
  }

  // 情况 2：无编码但中文标点或全角 -> 直接输出转换后的标点
  if (isPunctKey && (_status.chinesePunctuation || _status.fullShape)) {
    wchar_t ch = VkToChar(wParam, lParam);
    if (ch) {
      *pfEaten = TRUE;
      std::wstring output;
      if (_status.chinesePunctuation) {
        if (ch == L'\'' || ch == L'"') {
          output = _ProcessQuote(ch);  // 引号配对
        } else {
          bool found = false;
          for (auto& p : g_punctMap) {
            if (p.first == ch) {
              output = p.second;
              found = true;
              break;
            }
          }
          if (!found)
            output = std::wstring(1, ch);
        }
      } else {
        output = std::wstring(1, ch);  // 英文标点交给 _CommitAndEnd 全角化
      }
      _CommitAndEnd(pic, output);
      return S_OK;
    }
  }

  // 情况 3：英文标点半角、或非标点键 -> 交给宿主
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
  if (_clientId == TF_CLIENTID_NULL) {
    OutputDebugStringW(L"[JiDa] Commit: _clientId NULL\n");
    return;
  }

  // 记录上屏历史（用原文，便于用户词复用；转换只在最终输出时做）
  std::wstring code = _engine.LastCommittedCode();
  if (!code.empty()) {
    _engine.OnCommitted(code, text);
  }

  // 简繁 + 全半角
  std::wstring output =
      ConvertText(text, _status.traditional, _status.fullShape);

  auto* s = new CCommitSession(pic, _composition, output);
  HRESULT phrSession = E_FAIL;
  HRESULT hr = pic->RequestEditSession(
      _clientId, s, TF_ES_SYNC | TF_ES_READWRITE, &phrSession);
  s->Release();

  wchar_t dbg[256];
  swprintf_s(dbg,
             L"[JiDa] Commit hr=0x%08X phr=0x%08X hasComp=%d src=%ls out=%ls\n",
             (unsigned)hr, (unsigned)phrSession, _composition ? 1 : 0,
             text.c_str(), output.c_str());
  OutputDebugStringW(dbg);

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

void CJiDaTextService::_GetHostCaret(ITfContext* pic, RECT& rcCaret) {
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
  _GetHostCaret(pic, rcCaret);

  UINT dpi = GetDpiForRect(rcCaret);
  _candWnd->Show(_engine.Candidates(), _engine.SelectedIndex(), rcCaret, dpi);
}

void CJiDaTextService::_ShowTempEnglish(ITfContext* pic) {
  if (!_candWnd)
    return;
  if (_tempEnglishBuffer.empty()) {
    _candWnd->Hide();
    return;
  }

  _candWnd->SetTitle(_tempEnglishBuffer);

  std::vector<CandidateItem> items;
  // items.push_back({_tempEnglishBuffer, L""});

  RECT rcCaret = {};
  _GetHostCaret(pic, rcCaret);

  UINT dpi = GetDpiForRect(rcCaret);
  _candWnd->Show(items, 0, rcCaret, dpi);
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
  _tempEnglishMode = false;
  _tempEnglishBuffer.clear();
}

void CJiDaTextService::_CancelWithContext(ITfContext* pic) {
  if (_composition) {
    auto* s = new CCommitSession(pic, _composition, L"");
    pic->RequestEditSession(_clientId, s, TF_ES_SYNC | TF_ES_READWRITE,
                            nullptr);
    s->Release();
    _composition->Release();
    _composition = nullptr;
  }
  _engine.Reset();
  _tempEnglishMode = false;
  _tempEnglishBuffer.clear();
}

STDMETHODIMP CJiDaTextService::OnSetFocus(ITfDocumentMgr*, ITfDocumentMgr*) {
  _Cancel();
  _HideCandidates();
  if (_activeCtx) {
    _activeCtx->Release();
    _activeCtx = nullptr;
    _singleQuoteOpen = false;
    _doubleQuoteOpen = false;
    _tempEnglishMode = false;
    _tempEnglishBuffer.clear();
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

void CJiDaTextService::_ShowStatusContextMenu(POINT screenPoint) {
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

  HWND owner = _candWnd ? _candWnd->GetHwnd()
                        : (_statusWnd ? _statusWnd->GetHwnd() : nullptr);

  UINT cmd =
      TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                     screenPoint.x, screenPoint.y, 0, owner, nullptr);
  DestroyMenu(menu);

  switch (cmd) {
    case 1001:
      _OnStatusCommand(StatusCommand::ToggleChinese);
      break;
    case 1002:
      _OnStatusCommand(StatusCommand::ToggleFullShape);
      break;
    case 1003:
      _OnStatusCommand(StatusCommand::TogglePunctuation);
      break;
    case 1004:
      _OnStatusCommand(StatusCommand::ToggleTraditional);
      break;
    case 1005:
      _OnStatusCommand(StatusCommand::OpenSettings);
      break;
    case 1006:
      _OnStatusCommand(StatusCommand::HideStatusWindow);
      break;
    default:
      break;
  }
}

void CJiDaTextService::_ShowPhraseWindow(ITfContext* pic) {
  if (!_phraseWnd)
    return;

  // 已可见 → 视为取消
  if (_phraseWnd->IsVisible()) {
    _phraseWnd->Hide();
    _engine.ClearRecentCommits();
    OutputDebugStringW(L"[JiDa] Phrase: closed, history cleared\n");
    return;
  }

  auto& commits = _engine.GetRecentCommits();
  wchar_t dbg[128];
  swprintf_s(dbg, L"[JiDa] Phrase: history=%zu\n", commits.size());
  OutputDebugStringW(dbg);

  if (commits.size() < 2) {
    OutputDebugStringW(L"[JiDa] Phrase: need >=2, ignore\n");
    return;
  }

  // 最多显示最近 16 个
  const size_t kMaxDisplay = 16;
  size_t start =
      (commits.size() > kMaxDisplay) ? commits.size() - kMaxDisplay : 0;

  std::vector<PhraseEntry> entries;
  entries.reserve(commits.size() - start);
  for (size_t i = start; i < commits.size(); ++i) {
    entries.push_back(PhraseEntry{commits[i].code, commits[i].fullCode,
                                  commits[i].text, true});
  }

  // 定位到光标
  POINT anchor = {0, 0};
  RECT rcCaret = {0, 0, 0, 0};
  ITfContextView* view = nullptr;
  if (SUCCEEDED(pic->GetActiveView(&view)) && view) {
    ITfRange* range = nullptr;
    TF_SELECTION sel;
    ULONG n = 0;
    if (SUCCEEDED(pic->GetSelection(TF_INVALID_EDIT_COOKIE,
                                    TF_DEFAULT_SELECTION, 1, &sel, &n)) &&
        n)
      range = sel.range;
    if (range) {
      RECT r;
      BOOL clipped = FALSE;
      view->GetTextExt(TF_INVALID_EDIT_COOKIE, range, &r, &clipped);
      if (r.right > r.left || r.bottom > r.top)
        rcCaret = r;
      range->Release();
    }
    view->Release();
  }
  anchor.x = rcCaret.left;
  anchor.y = rcCaret.bottom;

  // 取消回调：清空历史
  _phraseWnd->OnCancel = [this]() {
    _engine.ClearRecentCommits();
    OutputDebugStringW(L"[JiDa] Phrase: cancelled, history cleared\n");
  };

  // 确认回调：生成编码 + 写入词库 + 清空历史
  _phraseWnd->Show(
      entries, anchor, [this](const std::vector<PhraseEntry>& selected) {
        std::wstring code = BuildPhraseCode(selected);
        std::wstring text;
        for (auto& e : selected)
          text += e.text;

        wchar_t d[512];
        swprintf_s(d,
                   L"[JiDa] Phrase confirm: count=%zu code='%ls' text='%ls'\n",
                   selected.size(), code.c_str(), text.c_str());
        OutputDebugStringW(d);

        if (code.empty() || text.empty()) {
          _engine.ClearRecentCommits();
          return;
        }

        // 重复检查
        bool dup = false;
        for (auto& up : g_userPhrases) {
          if (up.code == code && up.text == text) {
            dup = true;
            break;
          }
        }
        if (dup) {
          OutputDebugStringW(L"[JiDa] Phrase: duplicate, skip\n");
          _engine.ClearRecentCommits();
          return;
        }

        if (AddUserPhrase(code, text)) {
          SaveUserPhrases();
          swprintf_s(d, L"[JiDa] Phrase saved: %ls -> %ls (total=%zu)\n",
                     code.c_str(), text.c_str(), g_userPhrases.size());
          OutputDebugStringW(d);
        } else {
          OutputDebugStringW(L"[JiDa] Phrase: AddUserPhrase returned false\n");
        }

        _engine.ClearRecentCommits();
      });
}

std::wstring CJiDaTextService::_ProcessQuote(wchar_t ch) {
  if (ch == L'\'') {
    if (_singleQuoteOpen) {
      _singleQuoteOpen = false;
      return L"’";  // 右单引号
    }
    _singleQuoteOpen = true;
    return L"‘";  // 左单引号
  }
  if (ch == L'"') {
    if (_doubleQuoteOpen) {
      _doubleQuoteOpen = false;
      return L"”";  // 右双引号
    }
    _doubleQuoteOpen = true;
    return L"“";  // 左双引号
  }
  return std::wstring(1, ch);
}
