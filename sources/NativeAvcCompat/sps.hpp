// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace nativeavc {
constexpr size_t OldSpsSize = 0xc0c;
constexpr size_t NewSpsSize = 0xc14;
constexpr size_t FrameSize = 0x4a8;
constexpr size_t FrameSps = 0x410; // Native VDEnc feature 0x400; not feature 0x4000.
constexpr size_t CommandSize = 0x38;
constexpr size_t MaxSlices = 512;
constexpr uint32_t MaxMacroblocks = 0x8000; // Native slice first_mb field is 15 bits.

template <typename T> inline T get(const void *p, size_t offset) {
    T result;
    std::memcpy(&result, static_cast<const uint8_t *>(p) + offset, sizeof(result));
    return result;
}
template <typename T> inline void put(void *p, size_t offset, T value) {
    std::memcpy(static_cast<uint8_t *>(p) + offset, &value, sizeof(value));
}

// The native VDEnc implementation emits progressive macroblock rows. Reject
// unsupported field/MBAFF inputs instead of changing their slice addresses.
inline bool progressiveSps(const void *p, size_t size, uint32_t widthMb, uint32_t heightMb) {
    if (!p || (size != OldSpsSize && size != NewSpsSize) ||
        !widthMb || widthMb > 256 || !heightMb || heightMb > 256 ||
        widthMb * heightMb > MaxMacroblocks) return false;
    const size_t shift = size == NewSpsSize ? 8 : 0;
    return get<uint32_t>(p, 0x820 + shift) == widthMb - 1 &&
           get<uint32_t>(p, 0x824 + shift) == heightMb - 1 &&
           get<uint32_t>(p, 0x828 + shift) == 1 &&
           get<uint32_t>(p, 0x82c + shift) == 0 &&
           get<uint32_t>(p, 0x830 + shift) <= 1;
}

inline bool projectSps(const void *input, size_t inputSize, uint32_t widthMb,
                       uint32_t heightMb, void *output, size_t outputSize) {
    if (!output || outputSize != OldSpsSize || inputSize != NewSpsSize ||
        !progressiveSps(input, inputSize, widthMb, heightMb) ||
        get<uint32_t>(input, 0x18) || get<uint32_t>(input, 0x1c)) return false;
    // This is an ABI projection, not a repair to a generated command buffer.
    // The 8-byte extension precedes the legacy SPS fields; retain the tail.
    std::memcpy(output, input, 0x18);
    std::memcpy(static_cast<uint8_t *>(output) + 0x18,
                static_cast<const uint8_t *>(input) + 0x20, NewSpsSize - 0x20);
    return true;
}

inline bool sliceStart(uint32_t firstMb, uint32_t fieldPic, uint32_t bottomField,
                       uint32_t previous, size_t index, uint32_t totalMb) {
    return !fieldPic && !bottomField && totalMb <= MaxMacroblocks && firstMb < totalMb &&
           (index ? firstMb > previous : firstMb == 0);
}
} // namespace nativeavc
