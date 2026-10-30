#include "media_source_factory.h"

#include <atomic>
#include <new>
#include <wrl/client.h>
#include <wrl/implements.h>

#include "media_source.h"
#include "media_source_activation.h"

namespace clone_vcam {
namespace {

std::atomic<ULONG> g_lockCount{0};

class MediaSourceClassFactory final : public IClassFactory {
public:
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return ++refCount_; }
    IFACEMETHODIMP_(ULONG) Release() override {
        const ULONG count = --refCount_;
        if (count == 0) delete this;
        return count;
    }
    IFACEMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (outer) return CLASS_E_NOAGGREGATION;
        if (riid == IID_IMFActivate) {
            return CreateMediaSourceActivation(riid, ppv);
        }
        auto source = Microsoft::WRL::Make<MediaSource>();
        if (!source) return E_OUTOFMEMORY;
        return source->QueryInterface(riid, ppv);
    }
    IFACEMETHODIMP LockServer(BOOL lock) override {
        if (lock) ++g_lockCount;
        else --g_lockCount;
        return S_OK;
    }
private:
    std::atomic<ULONG> refCount_{1};
};

}  // namespace

HRESULT CreateMediaSourceClassFactory(IClassFactory** factory) {
    if (!factory) return E_POINTER;
    *factory = new (std::nothrow) MediaSourceClassFactory();
    return *factory ? S_OK : E_OUTOFMEMORY;
}

bool CanUnloadMediaSourceServer() noexcept {
    return g_lockCount.load() == 0;
}

}  // namespace clone_vcam
