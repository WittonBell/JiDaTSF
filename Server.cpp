#include "Globals.h"
#include "JidaTextService.h"

class CClassFactory : public IClassFactory {
  LONG _ref = 1;

 public:
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (riid == IID_IUnknown || riid == IID_IClassFactory) {
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
  STDMETHODIMP CreateInstance(IUnknown* pUnkOuter,
                              REFIID riid,
                              void** ppv) override {
    if (pUnkOuter)
      return CLASS_E_NOAGGREGATION;
    auto* svc = new CJidaTextService();
    HRESULT hr = svc->QueryInterface(riid, ppv);
    svc->Release();
    return hr;
  }
  STDMETHODIMP LockServer(BOOL) override { return S_OK; }
};
STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
  if (rclsid != CLSID_JidaTSF)
    return CLASS_E_CLASSNOTAVAILABLE;
  auto* cf = new CClassFactory();
  HRESULT hr = cf->QueryInterface(riid, ppv);
  cf->Release();
  return hr;
}
STDAPI DllCanUnloadNow() {
  return S_FALSE;
}