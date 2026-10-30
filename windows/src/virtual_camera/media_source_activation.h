#pragma once

#include <Windows.h>
#include <unknwn.h>

namespace clone_vcam {

HRESULT CreateMediaSourceActivation(REFIID iid, void** object);

}  // namespace clone_vcam
