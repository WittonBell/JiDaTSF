#pragma once
#include <msctf.h>
#include "CandidateWindow.h"
#include "Globals.h"
#include "JidaEngine.h"

class CJidaTextService : public ITfTextInputProcessor,
                           public ITfKeyEventSink,
                           public ITfThreadMgrEventSink {
 public:
  CJidaTextService();
  STDMETHODIMP QueryInterface(REFIID, void**) override;
  STDMETHODIMP_(ULONG) AddRef() override;
  STDMETHODIMP_(ULONG) Release() override;
  STDMETHODIMP Activate(ITfThreadMgr*, TfClientId) override;
  STDMETHODIMP Deactivate() override;
  STDMETHODIMP OnSetFocus(BOOL) override;
  STDMETHODIMP OnTestKeyDown(ITfContext*, WPARAM, LPARAM, BOOL*) override;
  STDMETHODIMP OnKeyDown(ITfContext*, WPARAM, LPARAM, BOOL*) override;
  STDMETHODIMP OnTestKeyUp(ITfContext*, WPARAM, LPARAM, BOOL*) override {
    return S_OK;
  }
  STDMETHODIMP OnKeyUp(ITfContext*, WPARAM, LPARAM, BOOL*) override {
    return S_OK;
  }
  STDMETHODIMP OnPreservedKey(ITfContext*, REFGUID, BOOL*) override {
    return S_OK;
  }
  STDMETHODIMP OnInitDocumentMgr(ITfDocumentMgr*) override { return S_OK; }
  STDMETHODIMP OnUninitDocumentMgr(ITfDocumentMgr*) override { return S_OK; }
  STDMETHODIMP OnSetFocus(ITfDocumentMgr*, ITfDocumentMgr*) override;
  STDMETHODIMP OnPushContext(ITfContext*) override { return S_OK; }
  STDMETHODIMP OnPopContext(ITfContext*) override { return S_OK; }

 private:
  ~CJidaTextService();
  void _CommitAndEnd(ITfContext* pic, const std::wstring& text);
  void _EnsureComposition(ITfContext* pic);
  void _SyncComposition(ITfContext* pic);
  void _ShowCandidates(ITfContext* pic);
  void _HideCandidates();
  void _Cancel();
  void _HandleLetterResult(ITfContext* pic,
                           EngineAction act,
                           std::wstring& committed,
                           std::wstring& newBuffer);

  LONG _ref = 1;
  ITfThreadMgr* _threadMgr = nullptr;
  TfClientId _clientId = TF_CLIENTID_NULL;
  DWORD _sinkCookie = TF_INVALID_COOKIE;
  ITfComposition* _composition = nullptr;

  JidaEngine _engine;
  std::unique_ptr<CCandidateWindow> _candWnd;
  ITfContext* _activeCtx = nullptr;
};