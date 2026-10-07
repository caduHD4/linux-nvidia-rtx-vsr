#ifndef MEDIA_RENDERERS_NVIDIA_VSR_PRESENTATION_CACHE_H_
#define MEDIA_RENDERERS_NVIDIA_VSR_PRESENTATION_CACHE_H_
#include "base/memory/scoped_refptr.h"
#include "media/base/video_frame.h"
#include "nvvfx_vsr/presentation_cache.h"
namespace media {
// Serialized by VideoRendererImpl::lock_; frame IDs, never PTS, are keys.
class NvidiaVsrPresentationCache final
    : public nvvfx_vsr::PresentationCache<scoped_refptr<VideoFrame>> {
 public:
  using nvvfx_vsr::PresentationCache<scoped_refptr<VideoFrame>>::PresentationCache;
};
}  // namespace media
#endif
