#ifndef MEDIA_RENDERERS_NVIDIA_VSR_SOURCE_RELEASE_H_
#define MEDIA_RENDERERS_NVIDIA_VSR_SOURCE_RELEASE_H_
#include <vector>
#include "base/check.h"
#include "base/memory/raw_ptr.h"
#include "media/base/video_frame.h"
#include "media/base/media_export.h"
namespace gpu { class SharedImageInterface; }
namespace media {
// The GPU source-copy task must wait on every collected dependency before
// releasing |copy_token|. It covers prior users and the new read without a
// renderer-side GPU wait or any dependency on inference completion.
class MEDIA_EXPORT NvidiaVsrSourceRelease final : public VideoFrame::SyncTokenClient {
 public:
  NvidiaVsrSourceRelease(gpu::SyncToken copy_token,
                        std::vector<gpu::SyncToken>* dependencies,
                        gpu::SharedImageInterface* verifier=nullptr)
      : copy_token_(copy_token), dependencies_(dependencies), verifier_(verifier) {
    CHECK(copy_token_.HasData());
    CHECK(dependencies_);
  }
  bool valid() const;
  void GenerateSyncToken(gpu::SyncToken* token) override;
  void WaitSyncToken(const gpu::SyncToken& token) override;
 private:
  const gpu::SyncToken copy_token_;
  const raw_ptr<std::vector<gpu::SyncToken>> dependencies_;
  const raw_ptr<gpu::SharedImageInterface> verifier_;
  gpu::SyncToken previous_;
  bool valid_=true;
};
}  // namespace media
#endif
