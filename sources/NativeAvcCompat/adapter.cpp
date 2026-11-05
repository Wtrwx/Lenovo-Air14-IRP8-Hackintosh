// SPDX-License-Identifier: MIT
#include "sps.hpp"
#include <array>
#include <atomic>
#include <cstdio>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#ifndef NATIVEAVC_TESTING
#include <CommonCrypto/CommonDigest.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <sys/sysctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <os/log.h>
#endif

using Complete = uint32_t (*)(void *, void *, void *);
using namespace nativeavc;
static std::atomic<unsigned> converted {0}, rejected {0};

static bool readSpan(const void *source, void *target, size_t size) {
    const auto address = reinterpret_cast<mach_vm_address_t>(source);
    if (!address || address > UINT64_MAX - size) return false;
    mach_vm_size_t actual = 0;
    return mach_vm_read_overwrite(mach_task_self(), address, size,
              reinterpret_cast<mach_vm_address_t>(target), &actual) == KERN_SUCCESS && actual == size;
}

static uint32_t reject(const char *reason) {
    if (rejected.fetch_add(1, std::memory_order_relaxed) < 8) {
#ifdef NATIVEAVC_TESTING
        std::fprintf(stderr, "NativeAvcCompat: rejected native AVC input (%s)\n", reason);
#else
        os_log_with_type(OS_LOG_DEFAULT, OS_LOG_TYPE_DEFAULT,
            "NativeAvcCompat ABI2: rejected native AVC input (%{public}s)", reason);
#endif
    }
    return 5; // Existing donor's invalid/unsupported-input status, before submit.
}

static uint32_t invoke(void *context, void *job, void *output, Complete original, uintptr_t vtable) {
    if (!original) return reject("missing original method");
    std::array<uint8_t, CommandSize> command {};
    std::array<uint8_t, FrameSize> frame {};
    std::array<uint8_t, NewSpsSize> source {};
    std::array<uint8_t, OldSpsSize> projected {};
    uintptr_t actualVtable = 0;
    uint32_t pixels[2] {};
    if (!readSpan(context, &actualVtable, sizeof(actualVtable)) || actualVtable != vtable ||
        !readSpan(static_cast<uint8_t *>(context) + 0x128, pixels, sizeof(pixels)) ||
        !readSpan(job, command.data(), command.size())) return reject("context/job span");
    if (get<uint32_t>(command.data(), 0) != 0x20 || get<uint64_t>(command.data(), 8) != 0x400)
        return reject("native VDEnc command contract");
    auto inputFrame = get<uint8_t *>(command.data(), 0x20);
    if (!readSpan(inputFrame, frame.data(), frame.size())) return reject("frame span");
    if (!pixels[0] || pixels[0] > 4096 || !pixels[1] || pixels[1] > 4096 ||
        (pixels[0] & 15) || (pixels[1] & 15)) return reject("aligned frame dimensions");
    const auto widthMb = pixels[0] / 16, heightMb = pixels[1] / 16;
    if (widthMb * heightMb > MaxMacroblocks) return reject("native MFX command capacity");
    auto sps = get<void *>(frame.data(), FrameSps);
    if (!readSpan(sps, source.data(), OldSpsSize)) return reject("SPS span");
    const bool oldLayout = progressiveSps(source.data(), OldSpsSize, widthMb, heightMb);
    if (!oldLayout) {
        if (!readSpan(sps, source.data(), NewSpsSize) ||
            !projectSps(source.data(), NewSpsSize, widthMb, heightMb, projected.data(), projected.size()))
            return reject("progressive SPS ABI");
    }
    const auto count = get<uint32_t>(frame.data(), 0x420);
    const auto array = get<uintptr_t>(frame.data(), 0x430);
    if (!count || count > MaxSlices || !array || array > UINTPTR_MAX - count * sizeof(uintptr_t))
        return reject("slice array");
    uint32_t previous = 0;
    for (uint32_t i = 0; i < count; ++i) {
        uintptr_t pointer = 0;
        std::array<uint8_t, 0x1c> header {};
        if (!readSpan(reinterpret_cast<void *>(array + i * sizeof(uintptr_t)), &pointer, sizeof(pointer)) ||
            !readSpan(reinterpret_cast<void *>(pointer), header.data(), header.size()))
            return reject("slice span");
        const auto first = get<uint32_t>(header.data(), 4);
        if (!sliceStart(first, get<uint32_t>(header.data(), 0x14), get<uint32_t>(header.data(), 0x18),
                        previous, i, widthMb * heightMb)) return reject("slice macroblock bounds");
        previous = first;
    }
    if (oldLayout) return original(context, job, output);
    // Keep caller memory intact. The native completion method synchronously
    // emits, submits and waits before returning; no SPS pointer escapes it.
    put(frame.data(), FrameSps, projected.data());
    put(command.data(), 0x20, frame.data());
    const auto status = original(context, command.data(), output);
    if (status) return status;
    // The native method's direct frame outputs are these three statistics.
    // Its encoded length is written through the unchanged pointer at +0x488.
    // Do not copy the frame or projected SPS pointer back over caller input.
    if (mach_vm_write(mach_task_self(), reinterpret_cast<mach_vm_address_t>(inputFrame) + 0x498,
              reinterpret_cast<vm_offset_t>(frame.data() + 0x498), 12) != KERN_SUCCESS)
        return reject("statistics output span");
    if (converted.fetch_add(1, std::memory_order_relaxed) == 0) {
#ifdef NATIVEAVC_TESTING
        std::fprintf(stderr, "NativeAvcCompat: projected native AVC SPS, %ux%u MB, %u slices\n",
                     widthMb, heightMb, count);
#else
        os_log_with_type(OS_LOG_DEFAULT, OS_LOG_TYPE_DEFAULT,
            "NativeAvcCompat ABI2: projected native AVC SPS, %ux%u MB, %u slices", widthMb, heightMb, count);
#endif
    }
    return status;
}

#ifdef NATIVEAVC_TESTING
extern "C" uint32_t nativeavc_test_invoke(void *ctx, void *job, void *out, Complete fn, uintptr_t vt) {
    return invoke(ctx, job, out, fn, vt);
}
#else
static Complete originalComplete;
static uintptr_t imageBase;
static uint32_t complete(void *ctx, void *job, void *out) {
    return invoke(ctx, job, out, originalComplete, imageBase + 0x754a68);
}

static bool fileHash(const char *path, const char *expected, off_t offset = 0, size_t limit = SIZE_MAX) {
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return false;
    if (lseek(fd, offset, SEEK_SET) != offset) { close(fd); return false; }
    CC_SHA256_CTX context; CC_SHA256_Init(&context);
    std::array<uint8_t, 65536> chunk {};
    ssize_t count;
    size_t total = 0;
    count = 0;
    while (total < limit) {
        size_t wanted = limit - total;
        if (wanted > chunk.size()) wanted = chunk.size();
        count = read(fd, chunk.data(), wanted);
        if (count <= 0) break;
        CC_SHA256_Update(&context, chunk.data(), static_cast<CC_LONG>(count));
        total += static_cast<size_t>(count);
    }
    close(fd);
    if (count < 0 || (limit != SIZE_MAX && total != limit)) return false;
    unsigned char digest[32]; char hex[65] {};
    CC_SHA256_Final(digest, &context);
    for (size_t i = 0; i < sizeof(digest); ++i) std::snprintf(hex + 2*i, 3, "%02x", digest[i]);
    return !std::strcmp(hex, expected);
}

static bool physicalGpu() {
    io_iterator_t iterator = 0;
    if (IOServiceGetMatchingServices(kIOMainPortDefault, IOServiceMatching("IOPCIDevice"), &iterator)) return false;
    unsigned matched = 0;
    for (io_object_t device; (device = IOIteratorNext(iterator)); IOObjectRelease(device)) {
        auto property = [device](CFStringRef key) {
            uint32_t value = UINT32_MAX;
            auto object = IORegistryEntryCreateCFProperty(device, key, kCFAllocatorDefault, 0);
            if (object && CFGetTypeID(object) == CFDataGetTypeID() &&
                CFDataGetLength(static_cast<CFDataRef>(object)) == 4)
                std::memcpy(&value, CFDataGetBytePtr(static_cast<CFDataRef>(object)), 4);
            if (object) CFRelease(object);
            return value;
        };
        if (property(CFSTR("vendor-id")) == 0x8086 && property(CFSTR("device-id")) == 0xa7a0) ++matched;
    }
    IOObjectRelease(iterator);
    return matched == 1;
}

static bool uuid(const mach_header_64 *header, const uint8_t expected[16]) {
    if (!header || header->magic != MH_MAGIC_64 || header->cputype != CPU_TYPE_X86_64) return false;
    auto cursor = reinterpret_cast<const uint8_t *>(header) + sizeof(*header);
    auto end = cursor + header->sizeofcmds;
    for (uint32_t i = 0; i < header->ncmds; ++i) {
        if (cursor + sizeof(load_command) > end) return false;
        auto lc = reinterpret_cast<const load_command *>(cursor);
        if (lc->cmdsize < sizeof(load_command) || lc->cmdsize > static_cast<size_t>(end - cursor)) return false;
        if (lc->cmd == LC_UUID && lc->cmdsize == sizeof(uuid_command))
            return !std::memcmp(reinterpret_cast<const uuid_command *>(lc)->uuid, expected, 16);
        cursor += lc->cmdsize;
    }
    return false;
}

__attribute__((constructor)) static void start() {
    char release[32] {}; size_t length = sizeof(release);
    if (sysctlbyname("kern.osrelease", release, &length, nullptr, 0) ||
        std::strcmp(release, "25.6.0") || !physicalGpu()) {
        os_log_with_type(OS_LOG_DEFAULT, OS_LOG_TYPE_DEFAULT,
            "NativeAvcCompat ABI2: OS/physical GPU guard rejected"); return;
    }
    constexpr const char *path = "/Library/GPUBundles/AppleIntelTGLGraphicsVADriver.bundle/Contents/MacOS/AppleIntelTGLGraphicsVADriver";
    constexpr const char *bridge = "/Library/GPUBundles/AppleIntelTGLGraphicsVADriver.bundle/Contents/MacOS/TGLVAOriginal";
    constexpr uint8_t donorUuid[] = {0x00,0x20,0x40,0x00,0x02,0x63,0x40,0x92,0xb4,0x83,0xcc,0x25,0x44,0xd2,0x63,0x50};
    constexpr uint8_t bridgeUuid[] = {0x77,0xac,0x3f,0x94,0x8a,0x77,0x39,0xb0,0x90,0xa0,0x9e,0x86,0xd3,0xf3,0xf6,0xb3};
    const mach_header_64 *driver = nullptr; bool bridgeFound = false;
    for (uint32_t i = 0; i < _dyld_image_count(); ++i) {
        const auto name = _dyld_get_image_name(i);
        const auto header = reinterpret_cast<const mach_header_64 *>(_dyld_get_image_header(i));
        if (name && !std::strcmp(name, path) && uuid(header, donorUuid)) driver = header;
        if (name && !std::strcmp(name, bridge) && uuid(header, bridgeUuid)) bridgeFound = true;
    }
    if (!driver || !bridgeFound ||
        // Pin the complete executable/constant payload, excluding the Mach-O
        // header and signature that change when sealing the enclosing bundle.
        !fileHash(path, "3af22e872e2312035f4603b70b28c2fd5fb47a274ef21a40abf5446911cbf30f", 0x1160, 0x754000 - 0x1160) ||
        !fileHash(bridge, "cbf0a308a536d2d54565684025d32b200649681aa2fda45dd46bdfdb77b3da9e")) {
        os_log_with_type(OS_LOG_DEFAULT, OS_LOG_TYPE_DEFAULT,
            "NativeAvcCompat ABI2: mapped image/immutable payload guard rejected"); return;
    }
    imageBase = reinterpret_cast<uintptr_t>(driver);
    auto slot = reinterpret_cast<uintptr_t *>(imageBase + 0x754a90);
    const auto expected = imageBase + 0x27724;
    constexpr uint8_t prologue[] = {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec,0x18,0x49,0x89,0xd6};
    if (*slot != expected || std::memcmp(reinterpret_cast<void *>(expected), prologue, sizeof(prologue))) {
        os_log_with_type(OS_LOG_DEFAULT, OS_LOG_TYPE_DEFAULT,
            "NativeAvcCompat ABI2: completion vtable/prologue guard rejected"); return;
    }
    const auto page = reinterpret_cast<mach_vm_address_t>(slot) & ~(static_cast<uint64_t>(vm_page_size) - 1);
    if (mach_vm_protect(mach_task_self(), page, vm_page_size, false,
                        VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY)) {
        os_log_with_type(OS_LOG_DEFAULT, OS_LOG_TYPE_DEFAULT,
            "NativeAvcCompat ABI2: completion page protection rejected"); return;
    }
    originalComplete = reinterpret_cast<Complete>(expected);
    uintptr_t current = expected;
    const bool changed = __atomic_compare_exchange_n(slot, &current, reinterpret_cast<uintptr_t>(&complete),
                                                     false, __ATOMIC_RELEASE, __ATOMIC_RELAXED);
    if (mach_vm_protect(mach_task_self(), page, vm_page_size, false, VM_PROT_READ)) {
        if (changed) __atomic_store_n(slot, expected, __ATOMIC_RELEASE);
        mach_vm_protect(mach_task_self(), page, vm_page_size, false, VM_PROT_READ);
        os_log_with_type(OS_LOG_DEFAULT, OS_LOG_TYPE_DEFAULT,
            "NativeAvcCompat ABI2: completion protection restore failed; adapter rolled back");
        return;
    }
    if (changed) os_log_with_type(OS_LOG_DEFAULT, OS_LOG_TYPE_DEFAULT,
        "NativeAvcCompat ABI2: exact native AVC completion adapter installed");
    else os_log_with_type(OS_LOG_DEFAULT, OS_LOG_TYPE_DEFAULT,
        "NativeAvcCompat ABI2: concurrent completion change; adapter not installed");
}
#endif
