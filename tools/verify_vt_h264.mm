#import <CoreGraphics/CoreGraphics.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>
#import <VideoToolbox/VideoToolbox.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

struct EncodedRecord {
    CMSampleBufferRef sample;
    int64_t pts;
    size_t bytes;
    bool keyframe;
};

struct DecodedMetrics {
    int64_t pts = -1;
    double psnrY = 0.0;
    double psnrU = 0.0;
    double psnrV = 0.0;
    uint64_t sseY = 0;
    uint64_t sseU = 0;
    uint64_t sseV = 0;
};

struct FrameReference {
    std::vector<uint8_t> y;
    std::vector<uint8_t> uv;
};

struct CaptureState {
    std::mutex mu;
    int width = 0;
    int height = 0;
    int count = 0;
    int repIndex = 0;
    bool isStill = false;
    std::vector<EncodedRecord> encoded;
    std::vector<bool> decodedSeen;
    std::vector<DecodedMetrics> decodedMetrics;
    std::vector<FrameReference> refs;
    std::vector<uint8_t> repDecY;
    std::vector<uint8_t> repDecUV;
    bool hasRepDec = false;
    std::atomic<int> encodedCount{0};
    std::atomic<int> decodedCount{0};
    std::atomic<int> errors{0};
};

static uint8_t inputByte(size_t plane, int x, int y, int frame) {
    if (plane == 1) {
        const bool overlay = x > 180 + (frame * 12) && x < 720 + (frame * 12) && y > 165 && y < 440;
        if (overlay) return (x & 1) ? 151 : 102;
        return (x & 1) ? 118 + (y / 65 % 3) * 6 : 142 - (x / 180 % 4) * 4;
    }
    const int left = 180 + frame * 12;
    const int top = 330 - frame * 3;
    if (x >= left && x < left + 540 && y >= top && y < top + 480) {
        const int sx = x - left, sy = y - top;
        if (sy < 30 || sx < 5 || sx >= 535 || sy >= 475) return 102;
        const int col = sx % 13, row = sy % 24;
        if (sx > 22 && sx < 480 - 19 * (sy / 24 % 4) &&
            (row == 6 || row == 12 || (row > 6 && row < 12 && (col == 2 || col == 7)))) {
            return 206;
        }
        return 37 + sy / 72 * 3;
    }
    if (x < 155 && y > 130 && y < 940) return ((x / 21 + y / 32) % 3 == 0) ? 126 : 59;
    if (y > 95 && y < 950 && x > 270 && x < 1517 &&
        (y % 31 < 2 || (y % 31 > 9 && y % 31 < 17 && x % 15 < 3))) {
        return 189;
    }
    return 49 + (x / 216 % 4) * 7 + (y / 135 % 5) * 5;
}

static double psnrFromSSE(double sse, size_t samples) {
    if (samples == 0) return 0.0;
    if (sse <= 0.0) return 99.99;
    return 10.0 * std::log10((255.0 * 255.0 * static_cast<double>(samples)) / sse);
}

static int queryBoolProperty(VTSessionRef session, CFStringRef key, OSStatus *outStatus) {
    CFTypeRef value = NULL;
    OSStatus s = VTSessionCopyProperty(session, key, NULL, &value);
    if (outStatus) *outStatus = s;
    int result = -1;
    if (s == noErr && value && CFGetTypeID(value) == CFBooleanGetTypeID()) {
        result = CFEqual(value, kCFBooleanTrue) ? 1 : 0;
    }
    if (value) CFRelease(value);
    return result;
}

static bool sampleIsKeyframe(CMSampleBufferRef sample) {
    CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray(sample, false);
    if (!attachments || CFArrayGetCount(attachments) == 0) return true;
    CFDictionaryRef dict = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(attachments, 0));
    if (!dict) return true;
    CFBooleanRef notSync = static_cast<CFBooleanRef>(
        CFDictionaryGetValue(dict, kCMSampleAttachmentKey_NotSync));
    if (!notSync) return true;
    return !CFBooleanGetValue(notSync);
}

static void encodeCallback(void *ctx, void *source, OSStatus status,
                           VTEncodeInfoFlags flags, CMSampleBufferRef sample) {
    (void)source;
    (void)flags;
    CaptureState *st = static_cast<CaptureState *>(ctx);
    if (status != noErr || !sample || !CMSampleBufferDataIsReady(sample)) {
        std::fprintf(stderr, "encode_callback error status=%d ready=%d\n",
                     static_cast<int>(status), sample ? CMSampleBufferDataIsReady(sample) : 0);
        st->errors.fetch_add(1);
        return;
    }
    CMTime pts = CMSampleBufferGetPresentationTimeStamp(sample);
    int64_t frameIdx = CMTIME_IS_NUMERIC(pts)
        ? CMTimeConvertScale(pts, 30, kCMTimeRoundingMethod_RoundHalfAwayFromZero).value
        : -1;
    CMBlockBufferRef block = CMSampleBufferGetDataBuffer(sample);
    size_t size = block ? CMBlockBufferGetDataLength(block) : 0;
    bool kf = sampleIsKeyframe(sample);
    CFRetain(sample);
    {
        std::lock_guard<std::mutex> lk(st->mu);
        st->encoded.push_back(EncodedRecord{sample, frameIdx, size, kf});
    }
    st->encodedCount.fetch_add(1);
}

static void decodeCallback(void *ctx, void *source, OSStatus status,
                           VTDecodeInfoFlags flags, CVImageBufferRef image,
                           CMTime pts, CMTime duration) {
    (void)source;
    (void)flags;
    (void)duration;
    CaptureState *st = static_cast<CaptureState *>(ctx);
    if (status != noErr || !image) {
        std::fprintf(stderr, "decode_callback error status=%d image=%d\n",
                     static_cast<int>(status), image != NULL);
        st->errors.fetch_add(1);
        return;
    }
    int64_t frame = CMTIME_IS_NUMERIC(pts)
        ? CMTimeConvertScale(pts, 30, kCMTimeRoundingMethod_RoundHalfAwayFromZero).value
        : -1;
    if (frame < 0 || frame >= st->count ||
        static_cast<int>(CVPixelBufferGetWidth(image)) != st->width ||
        static_cast<int>(CVPixelBufferGetHeight(image)) != st->height ||
        !CVPixelBufferIsPlanar(image) ||
        CVPixelBufferGetPixelFormatType(image) != kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange) {
        std::fprintf(stderr, "decode_format_error frame=%lld size=%zux%zu fmt=0x%x\n",
                     static_cast<long long>(frame), CVPixelBufferGetWidth(image),
                     CVPixelBufferGetHeight(image), static_cast<unsigned>(CVPixelBufferGetPixelFormatType(image)));
        st->errors.fetch_add(1);
        return;
    }
    if (CVPixelBufferLockBaseAddress(image, kCVPixelBufferLock_ReadOnly) != kCVReturnSuccess) {
        st->errors.fetch_add(1);
        return;
    }

    const int w = st->width;
    const int h = st->height;
    const uint8_t *decY = static_cast<const uint8_t *>(CVPixelBufferGetBaseAddressOfPlane(image, 0));
    const uint8_t *decUV = static_cast<const uint8_t *>(CVPixelBufferGetBaseAddressOfPlane(image, 1));
    const size_t strideY = CVPixelBufferGetBytesPerRowOfPlane(image, 0);
    const size_t strideUV = CVPixelBufferGetBytesPerRowOfPlane(image, 1);

    if (!decY || !decUV || strideY < static_cast<size_t>(w) || strideUV < static_cast<size_t>(w)) {
        CVPixelBufferUnlockBaseAddress(image, kCVPixelBufferLock_ReadOnly);
        st->errors.fetch_add(1);
        return;
    }

    const FrameReference &ref = st->refs[st->isStill ? 0 : static_cast<size_t>(frame)];
    uint64_t sseY = 0;
    for (int y = 0; y < h; ++y) {
        const uint8_t *rp = ref.y.data() + static_cast<size_t>(y) * w;
        const uint8_t *dp = decY + static_cast<size_t>(y) * strideY;
        for (int x = 0; x < w; ++x) {
            int d = static_cast<int>(rp[x]) - static_cast<int>(dp[x]);
            sseY += static_cast<uint64_t>(d * d);
        }
    }
    uint64_t sseU = 0, sseV = 0;
    for (int y = 0; y < h / 2; ++y) {
        const uint8_t *rp = ref.uv.data() + static_cast<size_t>(y) * w;
        const uint8_t *dp = decUV + static_cast<size_t>(y) * strideUV;
        for (int x = 0; x < w; x += 2) {
            int du = static_cast<int>(rp[x]) - static_cast<int>(dp[x]);
            int dv = static_cast<int>(rp[x + 1]) - static_cast<int>(dp[x + 1]);
            sseU += static_cast<uint64_t>(du * du);
            sseV += static_cast<uint64_t>(dv * dv);
        }
    }

    DecodedMetrics m;
    m.pts = frame;
    m.sseY = sseY;
    m.sseU = sseU;
    m.sseV = sseV;
    m.psnrY = psnrFromSSE(static_cast<double>(sseY), static_cast<size_t>(w) * h);
    m.psnrU = psnrFromSSE(static_cast<double>(sseU), static_cast<size_t>(w / 2) * (h / 2));
    m.psnrV = psnrFromSSE(static_cast<double>(sseV), static_cast<size_t>(w / 2) * (h / 2));

    {
        std::lock_guard<std::mutex> lk(st->mu);
        if (st->decodedSeen[static_cast<size_t>(frame)]) {
            std::fprintf(stderr, "duplicate decoded frame=%lld\n", static_cast<long long>(frame));
            st->errors.fetch_add(1);
        } else {
            st->decodedSeen[static_cast<size_t>(frame)] = true;
            st->decodedMetrics[static_cast<size_t>(frame)] = m;
            if (frame == st->repIndex) {
                st->repDecY.resize(static_cast<size_t>(w) * h);
                for (int y = 0; y < h; ++y) {
                    std::memcpy(st->repDecY.data() + static_cast<size_t>(y) * w,
                                decY + static_cast<size_t>(y) * strideY, static_cast<size_t>(w));
                }
                st->repDecUV.resize(static_cast<size_t>(w) * (h / 2));
                for (int y = 0; y < h / 2; ++y) {
                    std::memcpy(st->repDecUV.data() + static_cast<size_t>(y) * w,
                                decUV + static_cast<size_t>(y) * strideUV, static_cast<size_t>(w));
                }
                st->hasRepDec = true;
            }
        }
    }

    CVPixelBufferUnlockBaseAddress(image, kCVPixelBufferLock_ReadOnly);
    st->decodedCount.fetch_add(1);
}

static inline uint8_t clampU8(int v) {
    return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
}

static void yuvToRgb(uint8_t y, uint8_t u, uint8_t v, uint8_t *r, uint8_t *g, uint8_t *b) {
    int c = static_cast<int>(y) - 16;
    int d = static_cast<int>(u) - 128;
    int e = static_cast<int>(v) - 128;
    *r = clampU8((298 * c + 409 * e + 128) >> 8);
    *g = clampU8((298 * c - 100 * d - 208 * e + 128) >> 8);
    *b = clampU8((298 * c + 516 * d + 128) >> 8);
}

static bool writeComparePng(NSString *path, int w, int h,
                            const FrameReference &ref,
                            const std::vector<uint8_t> &decY,
                            const std::vector<uint8_t> &decUV) {
    const int scale = (w > 1920) ? 2 : 1;
    const int pw = w / scale;
    const int ph = h / scale;
    const int outW = pw * 3;
    const int outH = ph;
    std::vector<uint8_t> rgba(static_cast<size_t>(outW) * outH * 4, 255);

    for (int py = 0; py < ph; ++py) {
        int y = py * scale;
        int cy = y / 2;
        for (int px = 0; px < pw; ++px) {
            int x = px * scale;
            int cx = (x / 2) * 2;

            uint8_t ry = ref.y[static_cast<size_t>(y) * w + x];
            uint8_t ru = ref.uv[static_cast<size_t>(cy) * w + cx];
            uint8_t rv = ref.uv[static_cast<size_t>(cy) * w + cx + 1];
            uint8_t *p0 = &rgba[(static_cast<size_t>(py) * outW + px) * 4];
            yuvToRgb(ry, ru, rv, &p0[0], &p0[1], &p0[2]);

            uint8_t dy = decY[static_cast<size_t>(y) * w + x];
            uint8_t du = decUV[static_cast<size_t>(cy) * w + cx];
            uint8_t dv = decUV[static_cast<size_t>(cy) * w + cx + 1];
            uint8_t *p1 = &rgba[(static_cast<size_t>(py) * outW + pw + px) * 4];
            yuvToRgb(dy, du, dv, &p1[0], &p1[1], &p1[2]);

            uint8_t diff = clampU8(std::abs(static_cast<int>(ry) - static_cast<int>(dy)) * 4);
            uint8_t *p2 = &rgba[(static_cast<size_t>(py) * outW + 2 * pw + px) * 4];
            p2[0] = diff;
            p2[1] = diff;
            p2[2] = diff;
        }
    }

    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(rgba.data(), outW, outH, 8, outW * 4, cs,
                                             kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    CGImageRef cg = ctx ? CGBitmapContextCreateImage(ctx) : NULL;
    bool ok = false;
    if (cg) {
        NSURL *url = [NSURL fileURLWithPath:path];
        CGImageDestinationRef dest = CGImageDestinationCreateWithURL((__bridge CFURLRef)url, CFSTR("public.png"), 1, NULL);
        if (dest) {
            CGImageDestinationAddImage(dest, cg, NULL);
            ok = CGImageDestinationFinalize(dest);
            CFRelease(dest);
        }
        CGImageRelease(cg);
    }
    if (ctx) CGContextRelease(ctx);
    if (cs) CGColorSpaceRelease(cs);
    return ok;
}

static NSDictionary *uuInputAttributes() {
    return @{
        (__bridge NSString *)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange),
        (__bridge NSString *)kCVPixelBufferIOSurfacePropertiesKey: @{}
    };
}

static NSArray *uuPropertyPairs() {
    return @[
        @[(__bridge NSString *)kVTCompressionPropertyKey_AllowOpenGOP, @YES],
        @[(__bridge NSString *)kVTCompressionPropertyKey_AllowTemporalCompression, @YES],
        @[(__bridge NSString *)kVTCompressionPropertyKey_RealTime, @YES],
        @[(__bridge NSString *)kVTCompressionPropertyKey_ProfileLevel, (__bridge NSString *)kVTProfileLevel_H264_Main_AutoLevel],
        @[(__bridge NSString *)kVTCompressionPropertyKey_AllowFrameReordering, @NO],
        @[(__bridge NSString *)kVTCompressionPropertyKey_MaxFrameDelayCount, @0],
        @[(__bridge NSString *)kVTCompressionPropertyKey_PrioritizeEncodingSpeedOverQuality, @YES]
    ];
}

int main(int argc, const char **argv) { @autoreleasepool {
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <ll-must-reject|uu-normal|uu-soft|hw-decode> <width> <height> <frames> <bitrate_bps> <motion|still> [out_dir]\n", argv[0]);
        return 2;
    }
    NSString *mode = @(argv[1]);
    int w = argc > 2 ? std::atoi(argv[2]) : 1280;
    int h = argc > 3 ? std::atoi(argv[3]) : 720;
    int count = argc > 4 ? std::atoi(argv[4]) : 24;
    long long bitrate = argc > 5 ? std::atoll(argv[5]) : 1400000;
    bool isStill = argc > 6 && std::strcmp(argv[6], "still") == 0;
    if (argc > 6 && std::strcmp(argv[6], "motion") != 0 && !isStill) {
        std::fprintf(stderr, "expected motion or still, got %s\n", argv[6]);
        return 2;
    }
    NSString *outDir = argc > 7 ? @(argv[7]) : nil;

    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 30LL * NSEC_PER_SEC),
        dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        std::fprintf(stderr, "TIMEOUT\n");
        _exit(124);
    });

    if ([mode isEqualToString:@"ll-must-reject"]) {
        NSDictionary *llSpec = @{
            (__bridge NSString *)kVTVideoEncoderSpecification_EnableHardwareAcceleratedVideoEncoder: @YES,
            (__bridge NSString *)kVTVideoEncoderSpecification_RequireHardwareAcceleratedVideoEncoder: @YES,
            (__bridge NSString *)kVTVideoEncoderSpecification_EnableLowLatencyRateControl: @YES
        };
        VTCompressionSessionRef enc = NULL;
        OSStatus s = VTCompressionSessionCreate(NULL, w, h, kCMVideoCodecType_H264,
            (__bridge CFDictionaryRef)llSpec, (__bridge CFDictionaryRef)uuInputAttributes(), NULL, NULL, NULL, &enc);
        std::printf("encoder_create=%d\n", static_cast<int>(s));
        if (s == -12903 && enc == NULL) {
            std::puts("PASS ll-must-reject status=-12903");
            return 0;
        }
        if (enc) { VTCompressionSessionInvalidate(enc); CFRelease(enc); }
        std::fprintf(stderr, "FAIL ll-must-reject expected -12903 got %d\n", static_cast<int>(s));
        return 3;
    }

    bool uuNormal = [mode isEqualToString:@"uu-normal"];
    bool uuSoft = [mode isEqualToString:@"uu-soft"];
    bool hwDecode = [mode isEqualToString:@"hw-decode"];
    if (!uuNormal && !uuSoft && !hwDecode) return 2;
    if (w < 16 || h < 16 || w > 4096 || h > 2160 || (w % 2) != 0 || (h % 2) != 0 || count < 1 || count > 240) {
        std::fprintf(stderr, "invalid geometry/frame count\n");
        return 2;
    }

    if (outDir) {
        [[NSFileManager defaultManager] createDirectoryAtPath:outDir withIntermediateDirectories:YES attributes:nil error:nil];
    }

    CaptureState st;
    st.width = w;
    st.height = h;
    st.count = count;
    st.repIndex = (count > 18) ? 18 : (count - 1);
    st.isStill = isStill;
    st.decodedSeen.assign(static_cast<size_t>(count), false);
    st.decodedMetrics.resize(static_cast<size_t>(count));

    size_t numRefs = isStill ? 1 : static_cast<size_t>(count);
    st.refs.resize(numRefs);
    for (size_t f = 0; f < numRefs; ++f) {
        st.refs[f].y.resize(static_cast<size_t>(w) * h);
        st.refs[f].uv.resize(static_cast<size_t>(w) * (h / 2));
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                st.refs[f].y[static_cast<size_t>(y) * w + x] = inputByte(0, x, y, static_cast<int>(f));
            }
        }
        for (int y = 0; y < h / 2; ++y) {
            for (int x = 0; x < w; ++x) {
                st.refs[f].uv[static_cast<size_t>(y) * w + x] = inputByte(1, x, y, static_cast<int>(f));
            }
        }
    }

    NSDictionary *encSpec = uuNormal ? @{
        (__bridge NSString *)kVTVideoEncoderSpecification_EnableHardwareAcceleratedVideoEncoder: @YES,
        (__bridge NSString *)kVTVideoEncoderSpecification_RequireHardwareAcceleratedVideoEncoder: @YES
    } : @{
        (__bridge NSString *)kVTVideoEncoderSpecification_EnableHardwareAcceleratedVideoEncoder: @NO,
        (__bridge NSString *)kVTVideoEncoderSpecification_RequireHardwareAcceleratedVideoEncoder: @NO
    };

    VTCompressionSessionRef enc = NULL;
    OSStatus s = VTCompressionSessionCreate(NULL, w, h, kCMVideoCodecType_H264,
        (__bridge CFDictionaryRef)encSpec, (__bridge CFDictionaryRef)uuInputAttributes(), NULL, encodeCallback, &st, &enc);
    std::printf("encoder_create=%d\n", static_cast<int>(s));
    if (s != noErr || !enc) return 3;

    for (NSArray *pair in uuPropertyPairs()) {
        VTSessionSetProperty(enc, (__bridge CFStringRef)pair[0], (__bridge CFTypeRef)pair[1]);
    }
    VTSessionSetProperty(enc, kVTCompressionPropertyKey_ExpectedFrameRate, (__bridge CFTypeRef)@30);
    if (bitrate > 0) {
        VTSessionSetProperty(enc, kVTCompressionPropertyKey_AverageBitRate, (__bridge CFTypeRef)@(bitrate));
    }
    s = VTCompressionSessionPrepareToEncodeFrames(enc);
    std::printf("encoder_prepare=%d\n", static_cast<int>(s));
    if (s != noErr) return 3;

    OSStatus hwEncStatus = noErr;
    int actualEncHW = queryBoolProperty(enc, kVTCompressionPropertyKey_UsingHardwareAcceleratedVideoEncoder, &hwEncStatus);
    std::printf("hw_encoder=%d query_status=%d\n", actualEncHW, static_cast<int>(hwEncStatus));
    if (uuNormal && actualEncHW != 1) {
        std::fprintf(stderr, "expected hardware encoder\n");
        return 4;
    }
    if (!uuNormal && actualEncHW == 1) {
        std::fprintf(stderr, "expected software encoder\n");
        return 4;
    }

    CVPixelBufferPoolRef pool = VTCompressionSessionGetPixelBufferPool(enc);
    if (!pool) return 3;

    for (int n = 0; n < count; ++n) {
        CVPixelBufferRef pixel = NULL;
        if (CVPixelBufferPoolCreatePixelBuffer(NULL, pool, &pixel) != kCVReturnSuccess || !pixel) return 3;
        if (CVPixelBufferLockBaseAddress(pixel, 0) != kCVReturnSuccess) return 7;
        uint8_t *dstY = static_cast<uint8_t *>(CVPixelBufferGetBaseAddressOfPlane(pixel, 0));
        uint8_t *dstUV = static_cast<uint8_t *>(CVPixelBufferGetBaseAddressOfPlane(pixel, 1));
        size_t strideY = CVPixelBufferGetBytesPerRowOfPlane(pixel, 0);
        size_t strideUV = CVPixelBufferGetBytesPerRowOfPlane(pixel, 1);
        const FrameReference &ref = st.refs[isStill ? 0 : static_cast<size_t>(n)];
        for (int y = 0; y < h; ++y) {
            std::memcpy(dstY + static_cast<size_t>(y) * strideY, ref.y.data() + static_cast<size_t>(y) * w, static_cast<size_t>(w));
        }
        for (int y = 0; y < h / 2; ++y) {
            std::memcpy(dstUV + static_cast<size_t>(y) * strideUV, ref.uv.data() + static_cast<size_t>(y) * w, static_cast<size_t>(w));
        }
        CVPixelBufferUnlockBaseAddress(pixel, 0);
        s = VTCompressionSessionEncodeFrame(enc, pixel, CMTimeMake(n, 30), CMTimeMake(1, 30), NULL, NULL, NULL);
        CVPixelBufferRelease(pixel);
        if (s != noErr) {
            std::fprintf(stderr, "encode_submit=%d\n", static_cast<int>(s));
            return 3;
        }
    }
    s = VTCompressionSessionCompleteFrames(enc, kCMTimeInvalid);
    std::printf("encode_complete=%d frames=%d errors=%d\n", static_cast<int>(s), st.encodedCount.load(), st.errors.load());
    VTCompressionSessionInvalidate(enc);
    CFRelease(enc);
    if (s != noErr || st.encodedCount.load() != count || st.errors.load() != 0) return 5;

    std::sort(st.encoded.begin(), st.encoded.end(), [](const EncodedRecord &a, const EncodedRecord &b) {
        return a.pts < b.pts;
    });

    CMSampleBufferRef first = st.encoded[0].sample;
    if (outDir) {
        NSMutableData *annex = [NSMutableData data];
        const uint8_t startCode[] = {0, 0, 0, 1};
        for (size_t i = 0; i < 2; ++i) {
            const uint8_t *bytes = NULL;
            size_t sz = 0, total = 0;
            int nalLen = 0;
            if (CMVideoFormatDescriptionGetH264ParameterSetAtIndex(
                    CMSampleBufferGetFormatDescription(first), i, &bytes, &sz, &total, &nalLen) != noErr) {
                return 8;
            }
            [annex appendBytes:startCode length:4];
            [annex appendBytes:bytes length:sz];
        }
        for (const auto &rec : st.encoded) {
            CMBlockBufferRef block = CMSampleBufferGetDataBuffer(rec.sample);
            size_t sz = CMBlockBufferGetDataLength(block);
            std::vector<uint8_t> buf(sz);
            if (CMBlockBufferCopyDataBytes(block, 0, sz, buf.data()) != noErr) return 8;
            for (size_t at = 0; at < sz;) {
                if (sz - at < 4) return 8;
                uint32_t nalu = (uint32_t(buf[at]) << 24) | (uint32_t(buf[at + 1]) << 16) |
                                (uint32_t(buf[at + 2]) << 8) | uint32_t(buf[at + 3]);
                at += 4;
                if (nalu > sz - at) return 8;
                [annex appendBytes:startCode length:4];
                [annex appendBytes:buf.data() + at length:nalu];
                at += nalu;
            }
        }
        if (![annex writeToFile:[outDir stringByAppendingPathComponent:@"output.h264"] atomically:YES]) return 8;
    }

    NSDictionary *decSpec = @{
        (__bridge NSString *)kVTVideoDecoderSpecification_EnableHardwareAcceleratedVideoDecoder: @(hwDecode),
        (__bridge NSString *)kVTVideoDecoderSpecification_RequireHardwareAcceleratedVideoDecoder: @(hwDecode)
    };
    NSDictionary *outAttrs = @{
        (__bridge NSString *)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange)
    };
    VTDecompressionOutputCallbackRecord cb = {decodeCallback, &st};
    VTDecompressionSessionRef dec = NULL;
    s = VTDecompressionSessionCreate(NULL, CMSampleBufferGetFormatDescription(first),
        (__bridge CFDictionaryRef)decSpec, (__bridge CFDictionaryRef)outAttrs, &cb, &dec);
    std::printf("decoder_create=%d\n", static_cast<int>(s));
    if (s != noErr || !dec) return 3;

    OSStatus hwDecStatus = noErr;
    int actualDecHW = queryBoolProperty(dec, kVTDecompressionPropertyKey_UsingHardwareAcceleratedVideoDecoder, &hwDecStatus);
    std::printf("hw_decoder=%d query_status=%d\n", actualDecHW, static_cast<int>(hwDecStatus));
    if (hwDecode && actualDecHW != 1) {
        std::fprintf(stderr, "expected hardware decoder\n");
        return 4;
    }
    if (!hwDecode && actualDecHW == 1) {
        std::fprintf(stderr, "expected software decoder\n");
        return 4;
    }

    for (const auto &rec : st.encoded) {
        s = VTDecompressionSessionDecodeFrame(dec, rec.sample, kVTDecodeFrame_EnableAsynchronousDecompression, NULL, NULL);
        if (s != noErr) {
            std::fprintf(stderr, "decode_submit=%d\n", static_cast<int>(s));
            return 3;
        }
    }
    s = VTDecompressionSessionWaitForAsynchronousFrames(dec);
    std::printf("decode_complete=%d frames=%d errors=%d\n", static_cast<int>(s), st.decodedCount.load(), st.errors.load());
    VTDecompressionSessionInvalidate(dec);
    CFRelease(dec);
    for (auto &rec : st.encoded) CFRelease(rec.sample);
    if (s != noErr || st.decodedCount.load() != count || st.errors.load() != 0) return 5;

    uint64_t totalBytes = 0, idrBytes = 0, pBytesSum = 0, pCount = 0;
    uint64_t totalSseY = 0, totalSseU = 0, totalSseV = 0;
    double minPsnrY = 999.0;
    NSMutableArray *perFrame = [NSMutableArray arrayWithCapacity:static_cast<NSUInteger>(count)];
    for (int i = 0; i < count; ++i) {
        const auto &rec = st.encoded[static_cast<size_t>(i)];
        const auto &dm = st.decodedMetrics[static_cast<size_t>(i)];
        totalBytes += rec.bytes;
        if (i == 0) idrBytes = rec.bytes;
        else { pBytesSum += rec.bytes; ++pCount; }
        totalSseY += dm.sseY;
        totalSseU += dm.sseU;
        totalSseV += dm.sseV;
        if (dm.psnrY < minPsnrY) minPsnrY = dm.psnrY;
        [perFrame addObject:@{
            @"pts": @(rec.pts),
            @"bytes": @(rec.bytes),
            @"keyframe": @(rec.keyframe),
            @"psnr_y_db": @(dm.psnrY),
            @"psnr_u_db": @(dm.psnrU),
            @"psnr_v_db": @(dm.psnrV)
        }];
    }

    size_t lumaSamples = static_cast<size_t>(w) * h * static_cast<size_t>(count);
    size_t chromaSamples = static_cast<size_t>(w / 2) * (h / 2) * static_cast<size_t>(count);
    double psnrY = psnrFromSSE(static_cast<double>(totalSseY), lumaSamples);
    double psnrU = psnrFromSSE(static_cast<double>(totalSseU), chromaSamples);
    double psnrV = psnrFromSSE(static_cast<double>(totalSseV), chromaSamples);
    double bitrateMbps = static_cast<double>(totalBytes) * 8.0 * 30.0 / static_cast<double>(count) / 1e6;
    double avgPBytes = pCount ? static_cast<double>(pBytesSum) / static_cast<double>(pCount) : 0.0;

    std::printf("SUMMARY mode=%s %dx%d frames=%d motion=%s hw_enc=%d hw_dec=%d bitrate_mbps=%.3f idr_bytes=%llu avg_p_bytes=%.1f psnr_y=%.2f psnr_u=%.2f psnr_v=%.2f f0_y=%.2f min_y=%.2f\n",
                argv[1], w, h, count, isStill ? "still" : "motion", actualEncHW, actualDecHW,
                bitrateMbps, static_cast<unsigned long long>(idrBytes), avgPBytes,
                psnrY, psnrU, psnrV, st.decodedMetrics[0].psnrY, minPsnrY);

    if (outDir) {
        if (st.hasRepDec) {
            const FrameReference &refRep = st.refs[isStill ? 0 : static_cast<size_t>(st.repIndex)];
            NSString *pngPath = [outDir stringByAppendingPathComponent:@"compare.png"];
            if (!writeComparePng(pngPath, w, h, refRep, st.repDecY, st.repDecUV)) {
                std::fprintf(stderr, "failed to write compare.png\n");
                return 8;
            }
        }
        NSDictionary *metrics = @{
            @"mode": mode,
            @"width": @(w),
            @"height": @(h),
            @"frames": @(count),
            @"bitrate_target_bps": @(bitrate),
            @"motion": isStill ? @"still" : @"motion",
            @"hw_encoder": @(actualEncHW == 1),
            @"hw_decoder": @(actualDecHW == 1),
            @"total_bytes": @(totalBytes),
            @"bitrate_mbps": @(bitrateMbps),
            @"idr_bytes": @(idrBytes),
            @"avg_p_bytes": @(avgPBytes),
            @"psnr_y_db": @(psnrY),
            @"psnr_u_db": @(psnrU),
            @"psnr_v_db": @(psnrV),
            @"frame0_psnr_y_db": @(st.decodedMetrics[0].psnrY),
            @"min_psnr_y_db": @(minPsnrY),
            @"rep_frame_index": @(st.repIndex),
            @"per_frame": perFrame
        };
        NSData *json = [NSJSONSerialization dataWithJSONObject:metrics options:NSJSONWritingPrettyPrinted error:nil];
        if (![json writeToFile:[outDir stringByAppendingPathComponent:@"metrics.json"] atomically:YES]) return 8;
    }
    return 0;
}}
