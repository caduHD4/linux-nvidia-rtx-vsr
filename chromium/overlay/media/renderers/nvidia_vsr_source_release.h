#ifndef MEDIA_RENDERERS_NVIDIA_VSR_SOURCE_RELEASE_H_
#define MEDIA_RENDERERS_NVIDIA_VSR_SOURCE_RELEASE_H_
#include <vector>
#include "base/check.h"
#include "base/memory/raw_ptr.h"
#include "media/base/video_frame.h"
namespace media {
// The GPU source-copy task must wait on every collected dependency before
// releasing |copy_token|. It covers prior users and the new read without a
// renderer-side GPU wait or any dependency on inference completion.
class NvidiaVsrSourceRelease final : public VideoFrame::SyncTokenClient {
 public:
  NvidiaVsrSourceRelease(gpu::SyncToken copy_token,
                        std::vector<gpu::SyncToken>* dependencies)
      : copy_token_(copy_token), dependencies_(dependencies) {
    CHECK(copy_token_.HasData());
    CHECK(dependencies_);
  }
  void GenerateSyncToken(gpu::SyncToken* token) override { *token = copy_token_; }
  void WaitSyncToken(const gpu::SyncToken& token) override {
    if (token.HasData()) {
      dependencies_->push_back(token);
    }
  }
 private:
  const gpu::SyncToken copy_token_;
  const raw_ptr<std::vector<gpu::SyncToken>> dependencies_;
};
}  // namespace media
#endif
