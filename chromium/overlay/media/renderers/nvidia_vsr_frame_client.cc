#include "media/renderers/nvidia_vsr_frame_client.h"
#include <optional>
#include <vector>
#include "base/no_destructor.h"
#include "base/synchronization/lock.h"
#include "base/strings/string_number_conversions.h"
#include "base/feature_list.h"
#include "base/functional/bind.h"
#include "base/task/bind_post_task.h"
#include "gpu/command_buffer/client/shared_image_interface.h"
#include "gpu/ipc/client/gpu_channel_host.h"
#include "gpu/ipc/common/command_buffer_id.h"
#include "media/renderers/nvidia_vsr_source_release.h"
#include "media/video/gpu_video_accelerator_factories.h"
#include "ui/gl/gl_features.h"
#include "gpu/command_buffer/client/client_shared_image.h"
#include "media/base/media_log.h"
#include "media/base/video_frame.h"
#include "media/base/video_types.h"
namespace media {
namespace {
struct QuarantinedSourceFrames {
  base::Lock lock;
  std::vector<scoped_refptr<VideoFrame>> frames;
};

void QuarantineSourceFrames(
    std::map<std::pair<uint64_t, uint64_t>, scoped_refptr<VideoFrame>> frames) {
  if (frames.empty())
    return;
  static base::NoDestructor<QuarantinedSourceFrames> quarantine;
  base::AutoLock lock(quarantine->lock);
  for (auto& [key, frame] : frames)
    quarantine->frames.push_back(std::move(frame));
}
}  // namespace
nvvfx_vsr::FrameBypass ClassifyNvidiaVsrFrame(const VideoFrame& frame,
                                           bool encrypted_track) {
  nvvfx_vsr::FrameEligibility info;
  info.encrypted_track=encrypted_track;
  info.protected_video=frame.metadata().protected_video;
  info.hw_protected=frame.metadata().hw_protected;
  // Do not examine the GPU image at all for a protected/encrypted frame.
  if(info.encrypted_track || info.protected_video || info.hw_protected)
    return nvvfx_vsr::FrameBypass::Protected;
  info.gpu_image=frame.HasSharedImage();
  const bool opaque_argb_shared_image =
      frame.format()==PIXEL_FORMAT_ARGB && frame.HasSharedImage() &&
      frame.shared_image()->format()==viz::SinglePlaneFormat::kBGRA_8888 &&
      frame.shared_image()->alpha_type()==kOpaque_SkAlphaType;
  info.opaque=IsOpaque(frame.format()) || opaque_argb_shared_image;
  info.bit_depth=frame.BitDepth();
  info.supported_format=frame.format()==PIXEL_FORMAT_NV12 ||
      frame.format()==PIXEL_FORMAT_XRGB || frame.format()==PIXEL_FORMAT_XBGR ||
      opaque_argb_shared_image;
  info.valid_sdr_color=frame.ColorSpace().IsValid();
  info.hdr=frame.ColorSpace().IsHDR();
  info.transformed=frame.metadata().transformation.has_value() &&
      !(frame.metadata().transformation.value()==kNoTransformation);
  const auto visible=frame.visible_rect().size();
  const auto natural=frame.natural_size();
  info.square_pixels=static_cast<int64_t>(visible.width())*natural.height()==
      static_cast<int64_t>(visible.height())*natural.width();
  info.input={visible.width(),visible.height()};
  return nvvfx_vsr::ClassifyFrame(info);
}
struct NvidiaVsrFrameClient::FrameSnapshot {
  gfx::Size natural_size;
  base::TimeDelta timestamp;
  std::optional<base::TimeDelta> frame_duration;
};
struct NvidiaVsrFrameClient::Slot {
  scoped_refptr<gpu::ClientSharedImage> image;
  gpu::SyncToken last_use;
  bool busy=false;
};
NvidiaVsrFrameClient::NvidiaVsrFrameClient(MediaLog* log,
    GpuVideoAcceleratorFactories* factories,ReadyCB ready,base::RepeatingClosure disconnected)
    :log_(log->Clone()),factories_(factories),ready_(std::move(ready)),
     disconnected_(std::move(disconnected)) {
  DETACH_FROM_SEQUENCE(sequence_checker_);
}
NvidiaVsrFrameClient::~NvidiaVsrFrameClient() {
  QuarantineSourceFrames(std::move(source_leases_));
}
void NvidiaVsrFrameClient::SetCallbacks(ReadyCB ready,base::RepeatingClosure disconnected) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ready_=std::move(ready);disconnected_=std::move(disconnected);
}
bool NvidiaVsrFrameClient::Observe(scoped_refptr<VideoFrame> frame,
                                   bool encrypted_track,uint64_t generation) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const auto reason=ClassifyNvidiaVsrFrame(*frame,encrypted_track);
  std::string descriptor="format="+VideoPixelFormatToString(frame->format())+
      " visible="+frame->visible_rect().ToString()+
      " natural="+frame->natural_size().ToString()+
      " opaque_metadata="+base::NumberToString(frame->metadata().alpha_is_opaque)+
      " bypass="+base::NumberToString(static_cast<int>(reason));
  if(reason!=nvvfx_vsr::FrameBypass::Protected) {
    descriptor+=" color="+frame->ColorSpace().ToString();
    if(frame->HasSharedImage())
      descriptor+=" shared_image="+frame->shared_image()->format().ToString()+
          " alpha_type="+base::NumberToString(static_cast<int>(
              frame->shared_image()->alpha_type()));
    else descriptor+=" storage=cpu";
  }
  // Bound configuration logging even for streams with frequent size changes.
  if(reported_.size()<32 && reported_.insert(descriptor).second)
    MEDIA_LOG(INFO,log_) << "NVIDIA VSR probe generation=" << generation
                         << " " << descriptor;
  if(reason!=nvvfx_vsr::FrameBypass::None || disabled_ || ready_.is_null() ||
     !base::FeatureList::IsEnabled(features::kNvidiaVideoSuperResolution)) return false;
  const auto key=std::make_pair(generation,frame->unique_id().GetUnsafeValue());
  if(seen_.contains(key)) return false;
  if(!remote_.is_bound()) {Connect();return false;}
  if(!available_ || in_flight_>=2 || !Submit(std::move(frame),generation)) return false;
  seen_.insert(key);
  seen_order_.push_back(key);
  if(seen_order_.size()>128) {seen_.erase(seen_order_.front());seen_order_.pop_front();}
  return true;
}
void NvidiaVsrFrameClient::SetCapacityCallback(base::RepeatingClosure callback) {
  capacity_=std::move(callback);
}
void NvidiaVsrFrameClient::NotifyCapacity() {
  if(!capacity_.is_null()) capacity_.Run();
}

void NvidiaVsrFrameClient::Connect() {
  runner_=base::SequencedTaskRunner::GetCurrentDefault();
  sii_=factories_ ? factories_->SharedImageInterface() : nullptr;
  channel_=sii_ ? sii_->GetGpuChannelForNvidiaVsr() : nullptr;
  if(!channel_) {disabled_=true;return;}
  source_route_=channel_->GenerateRouteID();
  output_route_=channel_->GenerateRouteID();
  connecting_=true;
  auto receiver=remote_.BindNewPipeAndPassReceiver();
  remote_.set_disconnect_handler(base::BindOnce(
      &NvidiaVsrFrameClient::OnDisconnected,weak_factory_.GetWeakPtr()));
  channel_->GetGpuChannel().CreateNvidiaVsr(source_route_,output_route_,
      std::move(receiver),base::BindPostTask(runner_,base::BindOnce(
      &NvidiaVsrFrameClient::OnConnected,weak_factory_.GetWeakPtr())));
}
void NvidiaVsrFrameClient::OnConnected(bool available) {
  connecting_=false;available_=available;disabled_=!available;
  MEDIA_LOG(INFO,log_) << "NVIDIA VSR GPU service available=" << available;
  if(available) NotifyCapacity();
}
void NvidiaVsrFrameClient::OnDisconnected() {
  available_=false;disabled_=true;remote_.reset();
  QuarantineSourceFrames(std::move(source_leases_));
  in_flight_=0;
  if(!disconnected_.is_null()) disconnected_.Run();
  MEDIA_LOG(INFO,log_) << "NVIDIA VSR GPU service disconnected; original fallback";
}
bool NvidiaVsrFrameClient::Submit(scoped_refptr<VideoFrame> frame,uint64_t generation) {
  auto cfg=nvvfx_vsr::SelectBrowserProcessorConfig(
      {frame->visible_rect().width(),frame->visible_rect().height()});
  if(!cfg) return false;
  const gfx::Size output_size(cfg->output.width,cfg->output.height);
  const gfx::ColorSpace rgb=frame->ColorSpace().GetAsFullRangeRGB();
  std::shared_ptr<Slot> slot;
  for(auto& candidate:slots_) {
    if(!candidate) candidate=std::make_shared<Slot>();
    if(!candidate->busy) {slot=candidate;break;}
  }
  if(!slot) return false;
  if(slot->image && (slot->image->size()!=output_size || slot->image->color_space()!=rgb)) {
    slot->image->UpdateDestructionSyncToken(slot->last_use);slot->image.reset();
  }
  if(!slot->image) {
    gpu::SharedImageInfo info(viz::SinglePlaneFormat::kRGBX_8888,output_size,rgb,
        kTopLeft_GrSurfaceOrigin,kOpaque_SkAlphaType,
        gpu::SHARED_IMAGE_USAGE_GLES2_WRITE | gpu::SHARED_IMAGE_USAGE_RASTER_READ |
        gpu::SHARED_IMAGE_USAGE_DISPLAY_READ,"NvidiaVsrOutput");
    slot->image=sii_->CreateSharedImage(info,gpu::kNullSurfaceHandle);
    if(!slot->image) return false;
  }
  // The decoder-owned SharedImage is already registered with the GPU process.
  // Re-importing it adds a second secondary reference and fails for the
  // decoder's CompoundImageBacking. Keep the VideoFrame lease until the GPU
  // service acknowledges completion instead.
  const uint64_t count=count_+1;
  const uint64_t id=frame->unique_id().GetUnsafeValue();
  auto request=gpu::mojom::NvidiaVsrFrameRequest::New();
  request->generation=generation;request->frame_id=id;request->release_count=count;
  request->input=frame->shared_image()->mailbox();request->output=slot->image->mailbox();
  request->visible_rect=frame->visible_rect();request->output_size=output_size;
  request->rgb_color_space=rgb;request->quality=cfg->quality;request->strength=cfg->strength;
  request->dependencies.push_back(frame->acquire_sync_token());
  request->dependencies.push_back(sii_->GenVerifiedSyncToken());
  if(slot->last_use.HasData()) request->dependencies.push_back(slot->last_use);
  gpu::SyncToken source(gpu::CommandBufferNamespace::GPU_IO,
      gpu::CommandBufferIdFromChannelAndRoute(channel_->channel_id(),source_route_),count);
  source.SetVerifyFlush();
  for(auto& dependency:request->dependencies) {
    if(!dependency.HasData() || dependency.verified_flush()) continue;
    if(!sii_->CanVerifySyncToken(dependency)) return false;
    sii_->VerifySyncToken(dependency);
    if(!dependency.verified_flush()) return false;
  }
  {
    NvidiaVsrSourceRelease release(source,&request->dependencies,sii_.get());
    frame->UpdateReleaseSyncToken(&release);
    if(!release.valid()) return false;  // Original release token remains unchanged.
  }  // End the dependency-vector borrow before Mojo consumes the request.
  ++count_;
  slot->busy=true;++in_flight_;++submitted_;
  source_leases_.emplace(std::make_pair(generation, id), frame);
  remote_->Process(std::move(request),base::BindOnce(&NvidiaVsrFrameClient::OnCompleted,
      weak_factory_.GetWeakPtr(),slot,
      FrameSnapshot{frame->natural_size(),frame->timestamp(),frame->metadata().frame_duration},
      generation,id));
  return true;
}

void NvidiaVsrFrameClient::OnCompleted(std::shared_ptr<Slot> slot,
    FrameSnapshot original,uint64_t generation,uint64_t frame_id,bool success,
    const std::string& status,
    const gpu::SyncToken& token,float gpu_ms) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  source_leases_.erase(std::make_pair(generation, frame_id));
  --in_flight_;
  gpu::SyncToken ready_token=token;ready_token.SetVerifyFlush();
  slot->last_use=ready_token;
  slot->image->UpdateDestructionSyncToken(ready_token);
  if(!success) {
    slot->busy=false;
    NotifyCapacity();
    if(reported_.size()<32 && reported_.insert("service="+status).second)
      MEDIA_LOG(INFO,log_) << "NVIDIA VSR bypass: " << status;
    return;
  }
  ++completed_;
  const auto size=slot->image->size();
  auto enhanced=VideoFrame::WrapSharedImage(PIXEL_FORMAT_XBGR,slot->image,
      ready_token,base::BindOnce(
          &NvidiaVsrFrameClient::ReturnLease,weak_factory_.GetWeakPtr(),slot,runner_),
      gfx::Rect(size),original.natural_size,original.timestamp);
  if(!enhanced) {slot->busy=false;NotifyCapacity();return;}
  enhanced->set_color_space(slot->image->color_space());
  enhanced->metadata().frame_duration=original.frame_duration;
  enhanced->metadata().power_efficient=false;
  ready_.Run(generation,frame_id,std::move(enhanced));
  NotifyCapacity();
  if(completed_==1 || completed_%120==0)
    MEDIA_LOG(INFO,log_) << "NVIDIA VSR completed=" << completed_
        << " submitted=" << submitted_ << " last_gpu_ms=" << gpu_ms;
}
void NvidiaVsrFrameClient::ReturnLease(base::WeakPtr<NvidiaVsrFrameClient> client,
    std::shared_ptr<Slot> slot,
    scoped_refptr<base::SequencedTaskRunner> runner,const gpu::SyncToken& token) {
  // This slot is exclusively leased: the media sequence only examines busy
  // until the posted return runs. Preserve the consumer fence synchronously,
  // even when shutdown makes posting the return impossible.

  if(token.HasData()) slot->last_use=token;
  slot->image->UpdateDestructionSyncToken(slot->last_use);
  runner->PostTask(FROM_HERE,base::BindOnce(
      [](base::WeakPtr<NvidiaVsrFrameClient> client,std::shared_ptr<Slot> returned){
        returned->busy=false;
        if(client) client->NotifyCapacity();
      },std::move(client),std::move(slot)));
}
}  // namespace media
