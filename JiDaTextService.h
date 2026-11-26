#pragma once
#include <msctf.h>
#include "CandidateWindow.h"
#include "Config.h"
#include "Converter.h"
#include "Globals.h"
#include "JiDaEngine.h"
#include "PhraseWindow.h"
#include "StatusWindow.h"

class CJiDaTextService : public ITfTextInputProcessor,
                         public ITfKeyEventSink,
                         public ITfThreadMgrEventSink {
 public:
  CJiDaTextService();
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
  ~CJiDaTextService();
  void _CommitAndEnd(ITfContext* pic, const std::wstring& text);
  void _EnsureComposition(ITfContext* pic);
  void _SyncComposition(ITfContext* pic);
  void _GetHostCaret(ITfContext* pic, RECT& rcCaret);
  void _ShowCandidates(ITfContext* pic);
  void _HideCandidates();
  void _Cancel();
  HRESULT _CancelWithContext(ITfContext* pic);
  void _HandleLetterResult(ITfContext* pic,
                           EngineAction act,
                           std::wstring& committed,
                           std::wstring& newBuffer);
  void _ShowStatusContextMenu(POINT screenPoint);

  LONG _ref = 1;
  ITfThreadMgr* _threadMgr = nullptr;
  TfClientId _clientId = TF_CLIENTID_NULL;
  DWORD _sinkCookie = TF_INVALID_COOKIE;
  ITfComposition* _composition = nullptr;

  JiDaEngine _engine;
  std::unique_ptr<CCandidateWindow> _candWnd;
  ITfContext* _activeCtx = nullptr;

 private:
  void _OnStatusCommand(StatusCommand cmd);
  void _OpenSettingsWindow();
  void _SyncStatusBarVisibility();

  std::unique_ptr<CStatusWindow> _statusWnd;
  InputStatus _status;
  std::unique_ptr<CPhraseWindow> _phraseWnd;
  void _ShowPhraseWindow(ITfContext* pic);

  bool _singleQuoteOpen = false;           // ' 是否已输出过左引号
  bool _doubleQuoteOpen = false;           // " 是否已输出过左引号
  std::wstring _ProcessQuote(wchar_t ch);  // 引号配对处理

  bool _tempEnglishMode = false;
  std::wstring _tempEnglishBuffer;
  bool _shiftDownForKey = false;
  void _ShowTempEnglish(ITfContext* pic);
};