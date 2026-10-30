#pragma once

#include <Windows.h>
#include <unknwn.h>

namespace clone_vcam {

HRESULT CreateMediaSourceClassFactory(IClassFactory** factory);
bool CanUnloadMediaSourceServer() noexcept;

}  // namespace clone_vcam
