#include "media/renderers/nvidia_vsr_source_release.h"
#include "gpu/command_buffer/client/shared_image_interface.h"
namespace media {
void NvidiaVsrSourceRelease::GenerateSyncToken(gpu::SyncToken* token) {
  *token = valid_ ? copy_token_ : previous_;
}
void NvidiaVsrSourceRelease::WaitSyncToken(const gpu::SyncToken& token) {
  previous_=token;
  gpu::SyncToken dependency=token;
  if (dependency.HasData() && !dependency.verified_flush() && verifier_) {
    if(!verifier_->CanVerifySyncToken(dependency)) {valid_=false;return;}
    verifier_->VerifySyncToken(dependency);
    if(!dependency.verified_flush()) {valid_=false;return;}
  }
  if (dependency.HasData()) dependencies_->push_back(dependency);
}
bool NvidiaVsrSourceRelease::valid() const {return valid_;}
}  // namespace media
