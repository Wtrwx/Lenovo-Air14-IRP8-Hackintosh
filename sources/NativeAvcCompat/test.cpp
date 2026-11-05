// SPDX-License-Identifier: MIT
#include "sps.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>
#include <sys/mman.h>
#include <CommonCrypto/CommonDigest.h>

using namespace nativeavc;
using Sps = std::array<uint8_t, NewSpsSize>;
static Sps modern(uint32_t w, uint32_t h);
using Complete = uint32_t (*)(void *, void *, void *);
extern "C" uint32_t nativeavc_test_invoke(void *, void *, void *, Complete, uintptr_t);
static void *callerFrame, *callerSps;
static unsigned called;
static uint32_t mockStatus;
static uint32_t mockComplete(void *, void *job, void *) {
    ++called;
    auto frame = get<uint8_t *>(job, 0x20);
    auto sps = get<uint8_t *>(frame, FrameSps);
    assert(frame != callerFrame && sps != callerSps);
    assert(progressiveSps(sps, OldSpsSize, 108, 68));
    assert(get<void *>(callerFrame, FrameSps) == callerSps);
    put<uint32_t>(frame, 0x498, 11); put<uint32_t>(frame, 0x49c, 22); put<uint32_t>(frame, 0x4a0, 33);
    return mockStatus;
}
static void invocationTest() {
    auto sps = modern(108, 68);
    std::array<uint8_t, 0x7000> ctx {};
    std::array<uint8_t, FrameSize> frame {};
    std::array<uint8_t, CommandSize> command {};
    std::array<uint8_t, 0x1c> slice {};
    auto pointer = slice.data();
    uintptr_t vtable = 0x1234;
    put(ctx.data(), 0, vtable);
    put<uint32_t>(ctx.data(), 0x128, 1728); put<uint32_t>(ctx.data(), 0x12c, 1088);
    put<uint32_t>(command.data(), 0, 0x20); put<uint64_t>(command.data(), 8, 0x400);
    put(command.data(), 0x20, frame.data()); put(frame.data(), FrameSps, sps.data());
    put<uint32_t>(frame.data(), 0x420, 1); put(frame.data(), 0x430, &pointer);
    callerFrame = frame.data(); callerSps = sps.data();
    const auto beforeFrame = frame; const auto beforeSps = sps; const auto beforeCommand = command;
    assert(nativeavc_test_invoke(ctx.data(), command.data(), nullptr, mockComplete, vtable) == 0);
    assert(called == 1 && sps == beforeSps && command == beforeCommand);
    assert(get<uint32_t>(frame.data(), 0x498) == 11 && get<uint32_t>(frame.data(), 0x49c) == 22 &&
           get<uint32_t>(frame.data(), 0x4a0) == 33);
    frame = beforeFrame; mockStatus = 9;
    assert(nativeavc_test_invoke(ctx.data(), command.data(), nullptr, mockComplete, vtable) == 9);
    assert(called == 2 && frame == beforeFrame);
    put<uint64_t>(command.data(), 8, 0x4000);
    assert(nativeavc_test_invoke(ctx.data(), command.data(), nullptr, mockComplete, vtable) == 5);
    command = beforeCommand;
    put<uint32_t>(slice.data(), 4, 7344);
    assert(nativeavc_test_invoke(ctx.data(), command.data(), nullptr, mockComplete, vtable) == 5);
    assert(nativeavc_test_invoke(reinterpret_cast<void *>(1), command.data(), nullptr, mockComplete, vtable) == 5);
    assert(called == 2 && frame == beforeFrame && sps == beforeSps);
    std::puts("Completion tests passed: input isolation, output propagation, original errors, invalid spans and slice rejection.");
}
static Sps modern(uint32_t w, uint32_t h) {
    Sps p {};
    put(p.data(), 0x828, w - 1); put(p.data(), 0x82c, h - 1);
    put<uint32_t>(p.data(), 0x830, 1); put<uint32_t>(p.data(), 0x838, 1);
    return p;
}

// Optional private donor replay: these two leaf functions only generate MFX
// state in CPU memory. No driver is loaded, IOConnect opened, or GPU submitted.
static void replay(const char *path) {
    std::ifstream f(path, std::ios::binary);
    std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)), {});
    assert(b.size() == 8454464);
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256(b.data(), static_cast<CC_LONG>(b.size()), digest);
    char hex[65] {};
    for (size_t i = 0; i < sizeof(digest); ++i) std::snprintf(hex + 2*i, 3, "%02x", digest[i]);
    assert(std::strcmp(hex, "a972cdb7ddd56bcf38c3ab71e874b73efc34bc683d30e49f0a1a1919625a726d") == 0);
    auto code = static_cast<uint8_t *>(mmap(nullptr, b.size(), PROT_READ | PROT_WRITE,
                                          MAP_PRIVATE | MAP_ANON, -1, 0));
    assert(code != MAP_FAILED);
    std::memcpy(code, b.data(), b.size());
    assert(mprotect(code, b.size(), PROT_READ | PROT_EXEC) == 0);
    using Image = void (*)(void *, void *);
    using Slice = void (*)(void *, void *, uint32_t);
    const auto image = reinterpret_cast<Image>(code + 0x23287);
    const auto slice = reinterpret_cast<Slice>(code + 0x258ab);
    std::array<uint8_t, 0x7000> context {};
    std::array<uint8_t, 0x1000> commands {}, pps {}, sliceData[4];
    std::array<uint8_t, FrameSize> frame {};
    std::array<uint8_t, 0x100> pack {};
    uint8_t *slices[4];
    for (size_t i = 0; i < 4; ++i) {
        sliceData[i].fill(0); slices[i] = sliceData[i].data();
        put<uint32_t>(slices[i], 4, static_cast<uint32_t>(i * 1836));
        put<uint32_t>(slices[i], 8, 2); // I slice; keeps reference paths inactive.
    }
    put<uint32_t>(context.data(), 8, 108); put<uint32_t>(context.data(), 0x10, 68);
    put(context.data(), 0xa00, commands.data());
    put(frame.data(), 0x418, pps.data()); put<uint32_t>(frame.data(), 0x420, 4);
    put(frame.data(), 0x430, slices); put(frame.data(), 0x480, pack.data());
    auto input = modern(108, 68);
    std::array<uint8_t, OldSpsSize> projected {};
    assert(projectSps(input.data(), input.size(), 108, 68, projected.data(), projected.size()));
    auto check = [&](void *sps, bool converted) {
        commands.fill(0);
        // Restore the command templates used by the admitted donor.
        std::memcpy(commands.data() + 0x254, code + 0x24b46c, 0x54);
        std::memcpy(commands.data() + 0x378, code + 0x24b574, 0x2c);
        put(frame.data(), FrameSps, sps);
        image(context.data(), frame.data());
        const auto flags = get<uint32_t>(commands.data(), 0x264);
        assert((flags & 6) == (converted ? 4u : 6u));
        for (uint32_t i = 0; i < 4; ++i) {
            slice(context.data(), frame.data(), i);
            const auto first = get<uint32_t>(commands.data(), 0x388);
            const auto next = get<uint32_t>(commands.data(), 0x38c);
            const uint32_t factor = converted ? 1 : 2;
            assert((first & 0x7fff) == i * 1836 * factor);
            assert((first >> 24) == i * 17 * factor);
            assert(((next >> 16) & 0x1ff) == (i == 3 ? 68 : (i + 1) * 17 * factor));
        }
    };
    check(input.data(), false);
    check(projected.data(), true);
    assert(munmap(code, b.size()) == 0);
    std::puts("Exact donor replay passed: unprojected rows 0/34/68/102; projected rows 0/17/34/51.");
}

int main(int argc, char **argv) {
    auto p = modern(108, 68);
    std::array<uint8_t, OldSpsSize> out {}, untouched {};
    out.fill(0xa5); untouched = out;
    assert(!projectSps(nullptr, 0, 108, 68, out.data(), out.size()));
    assert(!projectSps(p.data(), p.size()-1, 108, 68, out.data(), out.size()));
    assert(!projectSps(p.data(), p.size(), 0, 68, out.data(), out.size()));
    assert(!projectSps(p.data(), p.size(), 108, 67, out.data(), out.size()));
    assert(out == untouched);
    for (size_t offset : {size_t(0x18), size_t(0x1c), size_t(0x834)}) {
        auto bad = p; put<uint32_t>(bad.data(), offset, 1);
        assert(!projectSps(bad.data(), bad.size(), 108, 68, out.data(), out.size()));
    }
    auto bad = p; put<uint32_t>(bad.data(), 0x830, 0);
    assert(!projectSps(bad.data(), bad.size(), 108, 68, out.data(), out.size()));
    bad = p; put<uint32_t>(bad.data(), 0x838, 2);
    assert(!projectSps(bad.data(), bad.size(), 108, 68, out.data(), out.size()));
    assert(projectSps(p.data(), p.size(), 108, 68, out.data(), out.size()));
    assert(progressiveSps(out.data(), out.size(), 108, 68));
    assert(!progressiveSps(p.data(), OldSpsSize, 108, 68));
    unsigned accepted=0, oversized=0;
    for (uint32_t w : {1u, 2u, 80u, 108u, 180u, 240u, 256u, 480u})
        for (uint32_t h : {1u, 2u, 45u, 68u, 113u, 128u, 135u, 256u, 270u}) {
            auto input = modern(w,h);
            input[0xc13] = 0x5a; input[0x17] = 0x7b; input[0x20] = 0x89;
            if(w>256 || h>256 || w*h>MaxMacroblocks) {
                assert(!projectSps(input.data(), input.size(), w, h, out.data(), out.size()));
                ++oversized; continue;
            }
            assert(projectSps(input.data(), input.size(), w, h, out.data(), out.size()));
            assert(out[0xc0b] == 0x5a && out[0x17] == 0x7b && out[0x18] == 0x89);
            assert(progressiveSps(out.data(), out.size(), w, h));
            ++accepted;
        }
    assert(sliceStart(0,0,0,0,0,7344));
    assert(!sliceStart(1,0,0,0,0,7344));
    assert(sliceStart(1836,0,0,0,1,7344));
    assert(!sliceStart(1836,0,0,1836,1,7344));
    assert(!sliceStart(7344,0,0,5508,4,7344));
    assert(!sliceStart(0,1,0,0,0,7344));
    assert(!sliceStart(0,0,1,0,0,7344));
    assert(sliceStart(0x7fff,0,0,0,1,0x8000));
    assert(!sliceStart(0x8000,0,0,0,1,0x10000));
    std::printf("SPS projection tests passed (%u admitted dimensions, %u oversized dimensions rejected, plus boundary cases).\n",accepted,oversized);
    invocationTest();
    if (argc == 2) replay(argv[1]);
}
