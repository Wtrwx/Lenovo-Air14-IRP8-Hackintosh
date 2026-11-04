#include <mach/kmod.h>
extern "C" {
extern kern_return_t _start(kmod_info_t *, void *);
extern kern_return_t _stop(kmod_info_t *, void *);
extern kern_return_t SMCProcessor_kern_start(kmod_info_t *, void *);
extern kern_return_t SMCProcessor_kern_stop(kmod_info_t *, void *);
__attribute__((visibility("default"))) KMOD_EXPLICIT_DECL(as.vit9696.SMCProcessor, "1.3.8", _start, _stop)
__attribute__((visibility("hidden"))) kmod_start_func_t *_realmain = SMCProcessor_kern_start;
__attribute__((visibility("hidden"))) kmod_stop_func_t *_antimain = SMCProcessor_kern_stop;
__attribute__((visibility("hidden"))) int _kext_apple_cc = __APPLE_CC__;
}
