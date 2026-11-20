#pragma once
#include <msctf.h>
#include <utility>
#include "Globals.h"

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