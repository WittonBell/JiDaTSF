#pragma once
#include <msctf.h>
#include <utility>
#include "Globals.h"

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

class CCompositionSink : public ITfCompositionSink {
  LONG _ref = 1;

 public:
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (riid == IID_IUnknown || riid == IID_ITfCompositionSink) {
      *ppv = this;
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&_ref); }
  STDMETHODIMP_(ULONG) Release() override {
    ULONG u = InterlockedDecrement(&_ref);
    if (!u)
      delete this;
    return u;
  }
  // 组合被外部终止（如宿主失去焦点）时的通知，可以留空
  STDMETHODIMP OnCompositionTerminated(TfEditCookie ecWrite,
                                       ITfComposition* pComposition) override {
    return S_OK;
  }
};

class CCommitSession : public ITfEditSession {
 public:
  CCommitSession(ITfContext* ctx, ITfComposition* comp, std::wstring text)
      : _ctx(ctx), _comp(comp), _text(std::move(text)) {
    _ctx->AddRef();
    if (_comp)
      _comp->AddRef();
  }
  ~CCommitSession() {
    if (_comp)
      _comp->Release();
    _ctx->Release();
  }

  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (riid == IID_IUnknown || riid == IID_ITfEditSession) {
      *ppv = this;
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&_ref); }
  STDMETHODIMP_(ULONG) Release() override {
    ULONG u = InterlockedDecrement(&_ref);
    if (!u)
      delete this;
    return u;
  }

  STDMETHODIMP DoEditSession(TfEditCookie ec) override {
    bool done = false;

    // 优先走组合：有组合时把 range 文本替换成要提交的汉字，再结束组合
    if (_comp) {
      ITfRange* r = nullptr;
      if (SUCCEEDED(_comp->GetRange(&r)) && r) {
        HRESULT hrSet = r->SetText(ec, 0, _text.c_str(), (LONG)_text.size());
        r->Release();
        if (SUCCEEDED(hrSet)) {
          HRESULT hrEnd = _comp->EndComposition(ec);
          if (SUCCEEDED(hrEnd))
            done = true;
        }
      }
    }

    // 回退：无组合或组合提交失败，直接插入文本
    if (!done) {
      ITfInsertAtSelection* ins = nullptr;
      if (SUCCEEDED(
              _ctx->QueryInterface(IID_ITfInsertAtSelection, (void**)&ins))) {
        // 关键：TF_IAS_NOQUERY 是真正插入，不是查询
        HRESULT hrIns = ins->InsertTextAtSelection(
            ec, TF_IAS_NOQUERY, _text.c_str(), (LONG)_text.size(), nullptr);
        ins->Release();
        done = SUCCEEDED(hrIns);
      }
    }

    return done ? S_OK : E_FAIL;
  }

 private:
  LONG _ref = 1;
  ITfContext* _ctx;
  ITfComposition* _comp;
  std::wstring _text;
};

class CStartCompositionSession : public ITfEditSession,
                                 public ITfCompositionSink {
 public:
  // 三参数构造，与调用处一致
  CStartCompositionSession(ITfContext* ctx,
                           std::wstring text,
                           ITfComposition** out)
      : _ctx(ctx), _text(std::move(text)), _out(out) {
    _ctx->AddRef();
  }
  ~CStartCompositionSession() { _ctx->Release(); }

  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (!ppv)
      return E_POINTER;
    *ppv = nullptr;
    if (riid == IID_IUnknown || riid == IID_ITfEditSession)
      *ppv = static_cast<ITfEditSession*>(this);
    else if (riid == IID_ITfCompositionSink)
      *ppv = static_cast<ITfCompositionSink*>(this);
    else
      return E_NOINTERFACE;
    AddRef();
    return S_OK;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&_ref); }
  STDMETHODIMP_(ULONG) Release() override {
    ULONG u = InterlockedDecrement(&_ref);
    if (!u)
      delete this;
    return u;
  }

  STDMETHODIMP OnCompositionTerminated(TfEditCookie, ITfComposition*) override {
    return S_OK;
  }

  STDMETHODIMP DoEditSession(TfEditCookie ec) override {
    ITfContextComposition* ctxComp = nullptr;
    if (FAILED(_ctx->QueryInterface(IID_ITfContextComposition,
                                    (void**)&ctxComp))) {
      return S_OK;
    }

    ITfRange* range = nullptr;
    TF_SELECTION sel;
    ULONG n = 0;
    if (SUCCEEDED(_ctx->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &sel, &n)) &&
        n) {
      range = sel.range;
    }
    if (!range) {
      ITfInsertAtSelection* ins = nullptr;
      if (SUCCEEDED(
              _ctx->QueryInterface(IID_ITfInsertAtSelection, (void**)&ins))) {
        ins->InsertTextAtSelection(ec, TF_IAS_QUERYONLY, nullptr, 0, &range);
        ins->Release();
      }
    }
    if (!range) {
      ctxComp->Release();
      return S_OK;
    }

    ITfComposition* comp = nullptr;
    // 关键：传 this 作为 ITfCompositionSink，而不是 nullptr
    HRESULT hrStart = ctxComp->StartComposition(
        ec, range, static_cast<ITfCompositionSink*>(this), &comp);

    if (SUCCEEDED(hrStart) && comp) {
      const wchar_t* txt = g_config.inlinePreedit ? _text.c_str() : L"";
      range->SetText(ec, 0, txt, (LONG)wcslen(txt));
      *_out = comp;
    }

    range->Release();
    ctxComp->Release();
    return S_OK;
  }

 private:
  LONG _ref = 1;
  ITfContext* _ctx;
  std::wstring _text;
  ITfComposition** _out;
};

class CUpdateCompositionSession : public ITfEditSession {
 public:
  CUpdateCompositionSession(ITfContext* ctx,
                            ITfComposition* comp,
                            std::wstring text)
      : _ctx(ctx), _comp(comp), _text(std::move(text)) {
    _ctx->AddRef();
    _comp->AddRef();
  }
  ~CUpdateCompositionSession() {
    _comp->Release();
    _ctx->Release();
  }

  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (riid == IID_IUnknown || riid == IID_ITfEditSession) {
      *ppv = this;
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&_ref); }
  STDMETHODIMP_(ULONG) Release() override {
    ULONG u = InterlockedDecrement(&_ref);
    if (!u)
      delete this;
    return u;
  }

  STDMETHODIMP DoEditSession(TfEditCookie ec) override {
    ITfRange* r = nullptr;
    if (SUCCEEDED(_comp->GetRange(&r)) && r) {
      const wchar_t* txt = g_config.inlinePreedit ? _text.c_str() : L"";
      r->SetText(ec, 0, txt, (LONG)wcslen(txt));
      r->Release();
    }
    return S_OK;
  }

 private:
  LONG _ref = 1;
  ITfContext* _ctx;
  ITfComposition* _comp;
  std::wstring _text;
};

class CCaretPositionEditSession : public ITfEditSession {
 public:
  explicit CCaretPositionEditSession(ITfContext* context)
      : _refCount(1), _context(context) {
    if (_context)
      _context->AddRef();
  }

  ~CCaretPositionEditSession() {
    if (_context)
      _context->Release();
  }

  // IUnknown
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (!ppv)
      return E_POINTER;

    *ppv = nullptr;

    if (riid == IID_IUnknown || riid == IID_ITfEditSession) {
      *ppv = static_cast<ITfEditSession*>(this);
      AddRef();
      return S_OK;
    }

    return E_NOINTERFACE;
  }

  STDMETHODIMP_(ULONG) AddRef() override {
    return InterlockedIncrement(&_refCount);
  }

  STDMETHODIMP_(ULONG) Release() override {
    ULONG count = InterlockedDecrement(&_refCount);
    if (count == 0)
      delete this;
    return count;
  }

  // ITfEditSession
  STDMETHODIMP DoEditSession(TfEditCookie ec) override {
    _positioned = false;

    if (!_context)
      return E_FAIL;

    ITfContextView* view = nullptr;
    HRESULT hr = _context->GetActiveView(&view);
    if (FAILED(hr) || !view)
      return FAILED(hr) ? hr : E_FAIL;

    TF_SELECTION selection = {};
    ULONG fetched = 0;

    hr = _context->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &selection,
                                &fetched);

    if (SUCCEEDED(hr) && fetched > 0 && selection.range) {
      ITfRange* range = selection.range;

      // 确保查询的是插入点，而不是整个选区。
      // 对于非空选区，使用活动端（End）作为光标位置。
      hr = range->Collapse(ec, TF_ANCHOR_END);

      if (SUCCEEDED(hr)) {
        RECT rc = {};
        BOOL clipped = FALSE;

        hr = view->GetTextExt(ec, range, &rc, &clipped);

        if (SUCCEEDED(hr) && IsUsableCaretExtent(rc)) {
          _rcCaret = rc;
          _positioned = true;
        }
      }

      selection.range->Release();
    }

    view->Release();

    return _positioned ? S_OK : E_FAIL;
  }

  bool GetPosition(RECT* rc) const {
    if (!_positioned || !rc)
      return false;

    *rc = _rcCaret;
    return true;
  }

 private:
  LONG _refCount;
  ITfContext* _context;

  RECT _rcCaret = {};
  bool _positioned = false;
};