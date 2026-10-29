#pragma once

#include <cstdlib>

inline void Check(bool condition) {
    if (!condition) std::abort();
}
