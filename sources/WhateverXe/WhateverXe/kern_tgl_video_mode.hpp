// Explicit experiment policy, not general command or hardware admission.
// Fields follow Intel media-driver 4f4a07a39f1731ec89838c323df2bc3c0a026fab,
// Gen12 AVC/NV12 interface projection, preserving observed Apple DW1=0x82.
#ifndef KERN_TGL_VIDEO_MODE_HPP
#define KERN_TGL_VIDEO_MODE_HPP
#include <stdint.h>
#include <stddef.h>
namespace TGLVideoMode {
enum class Layout {ObservedShort,ReferenceAVCNV12SixDWORD};
inline bool matches(const uint32_t *w,size_t remaining,Layout layout) {
 if(!w || remaining<2)return false;
 if(layout==Layout::ObservedShort)return w[0]==0x70800000 && w[1]==0x82;
 return layout==Layout::ReferenceAVCNV12SixDWORD && remaining>=6 &&
  w[0]==0x70800004 && w[1]==0x82 && w[2]==0x002b030a &&
  w[3]==0x07000303 && !w[4] && !w[5];
}
}
#endif
