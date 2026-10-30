#include "media_source_activation.h"

#include <mfapi.h>
#include <mfidl.h>
#include <wrl/client.h>
#include <wrl/implements.h>

#include <mutex>

#include "media_source.h"

namespace clone_vcam {
namespace {

class MediaSourceActivation final : public Microsoft::WRL::RuntimeClass<
    Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IMFActivate, IMFAttributes> {
public:
    HRESULT Initialize() {
        return MFCreateAttributes(&attributes_, 8);
    }

    IFACEMETHODIMP ActivateObject(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        *object = nullptr;
        Microsoft::WRL::ComPtr<IUnknown> active;
        {
            std::lock_guard lock(mutex_);
            if (!activated_) {
                auto source = Microsoft::WRL::Make<MediaSource>();
                if (!source) return E_OUTOFMEMORY;
                const HRESULT copyHr = source->CopySourceAttributes(attributes_.Get());
                if (FAILED(copyHr)) return copyHr;
                const HRESULT hr = source.As(&activated_);
                if (FAILED(hr)) return hr;
            }
            active = activated_;
        }
        return active->QueryInterface(iid, object);
    }

    IFACEMETHODIMP ShutdownObject() override {
        return E_NOTIMPL;
    }

    IFACEMETHODIMP DetachObject() override {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE GetItem(REFGUID key, PROPVARIANT* value) override { return attributes_->GetItem(key, value); }
    HRESULT STDMETHODCALLTYPE GetItemType(REFGUID key, MF_ATTRIBUTE_TYPE* type) override { return attributes_->GetItemType(key, type); }
    HRESULT STDMETHODCALLTYPE CompareItem(REFGUID key, REFPROPVARIANT value, BOOL* result) override { return attributes_->CompareItem(key, value, result); }
    HRESULT STDMETHODCALLTYPE Compare(IMFAttributes* theirs, MF_ATTRIBUTES_MATCH_TYPE type, BOOL* result) override { return attributes_->Compare(theirs, type, result); }
    HRESULT STDMETHODCALLTYPE GetUINT32(REFGUID key, UINT32* value) override { return attributes_->GetUINT32(key, value); }
    HRESULT STDMETHODCALLTYPE GetUINT64(REFGUID key, UINT64* value) override { return attributes_->GetUINT64(key, value); }
    HRESULT STDMETHODCALLTYPE GetDouble(REFGUID key, double* value) override { return attributes_->GetDouble(key, value); }
    HRESULT STDMETHODCALLTYPE GetGUID(REFGUID key, GUID* value) override { return attributes_->GetGUID(key, value); }
    HRESULT STDMETHODCALLTYPE GetStringLength(REFGUID key, UINT32* length) override { return attributes_->GetStringLength(key, length); }
    HRESULT STDMETHODCALLTYPE GetString(REFGUID key, LPWSTR value, UINT32 size, UINT32* length) override { return attributes_->GetString(key, value, size, length); }
    HRESULT STDMETHODCALLTYPE GetAllocatedString(REFGUID key, LPWSTR* value, UINT32* length) override { return attributes_->GetAllocatedString(key, value, length); }
    HRESULT STDMETHODCALLTYPE GetBlobSize(REFGUID key, UINT32* size) override { return attributes_->GetBlobSize(key, size); }
    HRESULT STDMETHODCALLTYPE GetBlob(REFGUID key, UINT8* value, UINT32 size, UINT32* actual) override { return attributes_->GetBlob(key, value, size, actual); }
    HRESULT STDMETHODCALLTYPE GetAllocatedBlob(REFGUID key, UINT8** value, UINT32* size) override { return attributes_->GetAllocatedBlob(key, value, size); }
    HRESULT STDMETHODCALLTYPE GetUnknown(REFGUID key, REFIID iid, LPVOID* value) override { return attributes_->GetUnknown(key, iid, value); }
    HRESULT STDMETHODCALLTYPE SetItem(REFGUID key, REFPROPVARIANT value) override { return attributes_->SetItem(key, value); }
    HRESULT STDMETHODCALLTYPE DeleteItem(REFGUID key) override { return attributes_->DeleteItem(key); }
    HRESULT STDMETHODCALLTYPE DeleteAllItems() override { return attributes_->DeleteAllItems(); }
    HRESULT STDMETHODCALLTYPE SetUINT32(REFGUID key, UINT32 value) override { return attributes_->SetUINT32(key, value); }
    HRESULT STDMETHODCALLTYPE SetUINT64(REFGUID key, UINT64 value) override { return attributes_->SetUINT64(key, value); }
    HRESULT STDMETHODCALLTYPE SetDouble(REFGUID key, double value) override { return attributes_->SetDouble(key, value); }
    HRESULT STDMETHODCALLTYPE SetGUID(REFGUID key, REFGUID value) override { return attributes_->SetGUID(key, value); }
    HRESULT STDMETHODCALLTYPE SetString(REFGUID key, LPCWSTR value) override { return attributes_->SetString(key, value); }
    HRESULT STDMETHODCALLTYPE SetBlob(REFGUID key, const UINT8* value, UINT32 size) override { return attributes_->SetBlob(key, value, size); }
    HRESULT STDMETHODCALLTYPE SetUnknown(REFGUID key, IUnknown* value) override { return attributes_->SetUnknown(key, value); }
    HRESULT STDMETHODCALLTYPE LockStore() override { return attributes_->LockStore(); }
    HRESULT STDMETHODCALLTYPE UnlockStore() override { return attributes_->UnlockStore(); }
    HRESULT STDMETHODCALLTYPE GetCount(UINT32* count) override { return attributes_->GetCount(count); }
    HRESULT STDMETHODCALLTYPE GetItemByIndex(UINT32 index, GUID* key, PROPVARIANT* value) override { return attributes_->GetItemByIndex(index, key, value); }
    HRESULT STDMETHODCALLTYPE CopyAllItems(IMFAttributes* destination) override { return attributes_->CopyAllItems(destination); }

private:
    Microsoft::WRL::ComPtr<IMFAttributes> attributes_;
    Microsoft::WRL::ComPtr<IUnknown> activated_;
    std::mutex mutex_;
};

}  // namespace

HRESULT CreateMediaSourceActivation(REFIID iid, void** object) {
    if (!object) return E_POINTER;
    *object = nullptr;
    auto activation = Microsoft::WRL::Make<MediaSourceActivation>();
    if (!activation) return E_OUTOFMEMORY;
    HRESULT hr = activation->Initialize();
    if (FAILED(hr)) return hr;
    return activation->QueryInterface(iid, object);
}

}  // namespace clone_vcam
