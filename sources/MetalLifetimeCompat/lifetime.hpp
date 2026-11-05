// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <objc/runtime.h>

namespace metal_lifetime {
using Dealloc = void (*)(id, SEL);
struct Layout {
    Class buffer {}, queue {};
    ptrdiff_t queueOffset {}, targetOffset {}, soleOffset {}, cacheOffset {};
};
template<class T> inline T load(const void *object, ptrdiff_t offset) {
    T value;
    std::memcpy(&value, static_cast<const uint8_t *>(object) + offset, sizeof(value));
    return value;
}
template<class T> inline void store(void *object, ptrdiff_t offset, T value) {
    std::memcpy(static_cast<uint8_t *>(object) + offset, &value, sizeof(value));
}
bool inspect(Class buffer, Class queue, Layout &out);
bool repairAtDealloc(id buffer, const Layout &layout);

#ifdef METAL_LIFETIME_TESTING
bool installForTest(const Layout &, Dealloc expected);
uint64_t repairsForTest();
#endif
}
