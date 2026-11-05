// SPDX-License-Identifier: MIT
#include "lifetime.hpp"
#include <atomic>
#include <cstdlib>
#include <cstdio>
#ifndef METAL_LIFETIME_TESTING
#include <array>
#include <CommonCrypto/CommonDigest.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <sys/sysctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <os/log.h>
#endif

namespace metal_lifetime {
static constexpr const char *TargetType =
    "^{IGPoshUsedAsTarget={IGPoshResourceUsedAsTarget=[1024I][1024[4Q]]B}"
    "{IGPoshResourceUsedAsTarget=[1024I][1024[4Q]]B}}";
static Layout active;
static Dealloc nativeDealloc;
static std::atomic<uint64_t> repaired {0};

// Only accept ivars declared on this class. Non-fragile ObjC adjusts all donor
// offsets when the OS superclass grows; never pin the old absolute offsets.
static bool ownIvar(Class cls, const char *name, const char *type,
                    size_t width, ptrdiff_t &offset) {
    unsigned count = 0;
    Ivar *list = class_copyIvarList(cls, &count);
    bool found = false;
    for (unsigned i = 0; i < count; ++i) {
        if (std::strcmp(ivar_getName(list[i]), name)) continue;
        const char *actual = ivar_getTypeEncoding(list[i]);
        const auto value = ivar_getOffset(list[i]);
        const auto parentSize = class_getInstanceSize(class_getSuperclass(cls));
        if (found || !actual || std::strcmp(actual, type) || value < 0 ||
            size_t(value) < parentSize || size_t(value) > class_getInstanceSize(cls) ||
            width > class_getInstanceSize(cls) - size_t(value)) {
            free(list); return false;
        }
        found = true; offset = value;
    }
    free(list); return found;
}

bool inspect(Class buffer, Class queue, Layout &out) {
    if (!buffer || !queue || !class_getSuperclass(buffer) || !class_getSuperclass(queue) ||
        std::strcmp(class_getName(class_getSuperclass(buffer)), "MTLIOAccelCommandBuffer") ||
        std::strcmp(class_getName(class_getSuperclass(queue)), "MTLIOAccelCommandQueue")) return false;
    Layout result {buffer, queue};
    if (!ownIvar(buffer, "_pIGCommandQueue", "@\"MTLIGAccelCommandQueue\"", 8, result.queueOffset) ||
        !ownIvar(buffer, "_pPoshUsedAsTarget", TargetType, 8, result.targetOffset) ||
        !ownIvar(buffer, "m_isOnlyActiveCommandBuffer", "B", 1, result.soleOffset) ||
        !ownIvar(queue, "m_pPoshUsedAsTarget", TargetType, 8, result.cacheOffset) ||
        result.targetOffset - result.queueOffset != 48 ||
        result.soleOffset - result.queueOffset != 112) return false;
    out = result; return true;
}

bool repairAtDealloc(id buffer, const Layout &layout) {
    if (!buffer || object_getClass(buffer) != layout.buffer) return false;
    void *object = (void *)buffer;
    if (load<uint8_t>(object, layout.soleOffset) != 1 ||
        !load<void *>(object, layout.targetOffset)) return false;
    id queue = load<id>(object, layout.queueOffset);
    if (!queue || object_getClass(queue) != layout.queue ||
        load<void *>((void *)queue, layout.cacheOffset)) return false;
    // The exact donor allocated a private target table, but set the same flag
    // used for queue-owned tables. Clear it only at dealloc, after all encoding
    // and completion. Let the native destructor perform its normal delete.
    // A non-null queue cache or an unknown class is deliberately left alone.
    store<uint8_t>(object, layout.soleOffset, 0);
    return true;
}

static void dealloc(id object, SEL selector) {
    bool firstRepair = false;
    if (repairAtDealloc(object, active)) {
        const auto count = repaired.fetch_add(1, std::memory_order_relaxed) + 1;
        firstRepair = count == 1;
    }
    nativeDealloc(object, selector);
#ifndef METAL_LIFETIME_TESTING
    if (firstRepair)
        os_log_with_type(OS_LOG_DEFAULT, OS_LOG_TYPE_DEFAULT,
            "MetalLifetimeCompat v1: native destructor completed for an uncached sole-buffer target table");
#else
    (void)firstRepair;
#endif
}

static bool install(const Layout &layout, Dealloc expected) {
    if (nativeDealloc || !expected) return false;
    Method method = class_getInstanceMethod(layout.buffer, sel_registerName("dealloc"));
    const char *types = method ? method_getTypeEncoding(method) : nullptr;
    if (!method || !types || std::strcmp(types, "v16@0:8") ||
        method_getImplementation(method) != reinterpret_cast<IMP>(expected)) return false;
    active = layout; nativeDealloc = expected;
    // Installation happens in this dependency's initializer, before the GPU
    // bundle returns to its caller and before any command buffer can exist.
    IMP previous = method_setImplementation(method, reinterpret_cast<IMP>(dealloc));
    if (previous == reinterpret_cast<IMP>(expected)) return true;
    method_setImplementation(method, previous);
    nativeDealloc = nullptr; active = {};
    return false;
}

#ifdef METAL_LIFETIME_TESTING
bool installForTest(const Layout &layout, Dealloc expected) { return install(layout, expected); }
uint64_t repairsForTest() { return repaired.load(std::memory_order_relaxed); }
#else
static constexpr const char *Driver =
    "/Library/GPUBundles/AppleIntelTGLGraphicsMTLDriver.bundle/Contents/MacOS/AppleIntelTGLGraphicsMTLDriver";
static constexpr const char *Companion =
    "/Library/GPUBundles/AppleIntelTGLGraphicsMTLDriver.bundle/Contents/MacOS/impostor-original.dylib";

static bool fileHash(const char *path, const char *expected, off_t offset, size_t length) {
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return false;
    if (lseek(fd, offset, SEEK_SET) != offset) { close(fd); return false; }
    CC_SHA256_CTX context; CC_SHA256_Init(&context);
    std::array<uint8_t, 32768> bytes {};
    size_t total = 0;
    while (total < length) {
        size_t wanted = length - total;
        if (wanted > bytes.size()) wanted = bytes.size();
        ssize_t count = read(fd, bytes.data(), wanted);
        if (count <= 0) break;
        CC_SHA256_Update(&context, bytes.data(), static_cast<CC_LONG>(count));
        total += size_t(count);
    }
    close(fd);
    if (total != length) return false;
    unsigned char digest[32]; char text[65] {};
    CC_SHA256_Final(digest, &context);
    for (size_t i = 0; i < sizeof(digest); ++i) std::snprintf(text + 2*i, 3, "%02x", digest[i]);
    return !std::strcmp(text, expected);
}

static bool physicalGpu() {
    io_iterator_t iterator = 0;
    if (IOServiceGetMatchingServices(kIOMainPortDefault, IOServiceMatching("IOPCIDevice"), &iterator)) return false;
    unsigned found = 0;
    for (io_object_t device; (device = IOIteratorNext(iterator)); IOObjectRelease(device)) {
        auto property = [device](CFStringRef key) {
            uint32_t value = UINT32_MAX;
            CFTypeRef data = IORegistryEntryCreateCFProperty(device, key, kCFAllocatorDefault, 0);
            if (data && CFGetTypeID(data) == CFDataGetTypeID() &&
                CFDataGetLength(static_cast<CFDataRef>(data)) == 4)
                std::memcpy(&value, CFDataGetBytePtr(static_cast<CFDataRef>(data)), 4);
            if (data) CFRelease(data);
            return value;
        };
        if (property(CFSTR("vendor-id")) == 0x8086 && property(CFSTR("device-id")) == 0xa7a0) ++found;
    }
    IOObjectRelease(iterator); return found == 1;
}

static bool uuid(const mach_header_64 *header) {
    constexpr uint8_t wanted[] = {0x2b,0x84,0x9e,0x57,0xac,0x6a,0x32,0x16,0xa6,0x7b,0x38,0xe0,0x80,0x60,0x35,0x85};
    if (!header || header->magic != MH_MAGIC_64 || header->cputype != CPU_TYPE_X86_64 ||
        header->sizeofcmds > 65536) return false;
    auto cursor = reinterpret_cast<const uint8_t *>(header) + sizeof(*header);
    const auto end = cursor + header->sizeofcmds;
    for (uint32_t i = 0; i < header->ncmds; ++i) {
        if (cursor + sizeof(load_command) > end) return false;
        const auto command = reinterpret_cast<const load_command *>(cursor);
        if (command->cmdsize < sizeof(load_command) || command->cmdsize > size_t(end-cursor)) return false;
        if (command->cmd == LC_UUID && command->cmdsize == sizeof(uuid_command))
            return !std::memcmp(reinterpret_cast<const uuid_command *>(command)->uuid, wanted, 16);
        cursor += command->cmdsize;
    }
    return false;
}

static void rejected(const char *gate) {
    os_log_with_type(OS_LOG_DEFAULT, OS_LOG_TYPE_DEFAULT,
        "MetalLifetimeCompat v1: guard rejected %{public}s; native destructor retained", gate);
}

__attribute__((constructor)) static void start() {
    char release[32] {}; size_t length = sizeof(release);
    if (sysctlbyname("kern.osrelease", release, &length, nullptr, 0) ||
        std::strcmp(release, "25.6.0") || !physicalGpu()) { rejected("OS/physical GPU"); return; }
    const mach_header_64 *header = nullptr; bool companion = false;
    for (uint32_t i = 0; i < _dyld_image_count(); ++i) {
        const auto name = _dyld_get_image_name(i);
        if (name && !std::strcmp(name, Driver)) header = reinterpret_cast<const mach_header_64 *>(_dyld_get_image_header(i));
        if (name && !std::strcmp(name, Companion)) companion = true;
    }
    if (!uuid(header) || !companion) { rejected("mapped donor/companion"); return; }
    if (!fileHash(Driver, "49b0f40eb067acb4ffc0345170f6b68e482b4736a8d0eddd3c15204885be2835", 0x20d0, 0x480000 - 0x20d0) ||
        !fileHash(Companion, "cd415a30e111d9ad48e6dd1936d848c1e809821aa3770c93ae7c759a4454567c", 0, 29696)) {
        rejected("immutable payload"); return;
    }
    Class buffer = objc_getClass("MTLIGAccelCommandBuffer");
    Class queue = objc_getClass("MTLIGAccelCommandQueue");
    if (!buffer || !queue || !class_getImageName(buffer) || !class_getImageName(queue) ||
        std::strcmp(class_getImageName(buffer), Driver) || std::strcmp(class_getImageName(queue), Driver)) {
        rejected("ObjC class image"); return;
    }
    Layout layout;
    if (!inspect(buffer, queue, layout)) { rejected("ObjC ivar types/layout"); return; }
    const auto base = reinterpret_cast<uintptr_t>(header);
    if (load<ptrdiff_t>(header, 0x499e20) != layout.queueOffset ||
        load<ptrdiff_t>(header, 0x499eb0) != layout.targetOffset ||
        load<ptrdiff_t>(header, 0x499ea8) != layout.soleOffset ||
        load<ptrdiff_t>(header, 0x499c20) != layout.cacheOffset) {
        rejected("relocated donor ivars"); return;
    }
    const auto original = reinterpret_cast<Dealloc>(base + 0x7ecba);
    constexpr uint8_t first[] = {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec,0x18};
    if (std::memcmp(reinterpret_cast<const void *>(original), first, sizeof(first))) {
        rejected("native destructor bytes"); return;
    }
    if (install(layout, original))
        os_log_with_type(OS_LOG_DEFAULT, OS_LOG_TYPE_DEFAULT,
            "MetalLifetimeCompat v1: exact donor and runtime ivar ABI accepted; dealloc repair installed");
    else rejected("native destructor IMP/type");
}
#endif
}
