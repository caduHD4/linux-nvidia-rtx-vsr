#include "gpu/ipc/service/nvidia_vsr_gpu_service.h"
#include <atomic>
#include <optional>
#include <vector>
#include "base/feature_list.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/task/bind_post_task.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "gpu/command_buffer/common/shared_image_info.h"
#include "gpu/command_buffer/service/gles2_cmd_decoder.h"
#include "gpu/command_buffer/service/memory_tracking.h"
#include "gpu/command_buffer/service/scheduler.h"
#include "gpu/command_buffer/service/shared_context_state.h"
#include "gpu/command_buffer/service/shared_image/shared_image_factory.h"
#include "gpu/command_buffer/service/shared_image/shared_image_representation.h"
#include "gpu/command_buffer/service/texture_manager.h"
#include "gpu/command_buffer/service/texture_passthrough.h"
#include "gpu/ipc/common/command_buffer_id.h"
#include "gpu/ipc/service/gpu_channel.h"
#include "gpu/ipc/service/gpu_channel_manager.h"
#include "gpu/ipc/service/shared_image_stub.h"
#include "mojo/public/cpp/bindings/lib/report_bad_message.h"
#include "nvvfx_vsr/processor_config.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkImage.h"
#include "third_party/skia/include/core/SkSamplingOptions.h"
#include "third_party/skia/include/gpu/ganesh/GrBackendSemaphore.h"
#include "third_party/skia/include/gpu/ganesh/GrDirectContext.h"
#include "third_party/skia/include/gpu/ganesh/SkSurfaceGanesh.h"
#include "third_party/skia/include/gpu/ganesh/gl/GrGLBackendSurface.h"
#include "ui/gl/gl_context.h"
#include "ui/gl/gl_features.h"
#include "ui/gl/gl_implementation.h"
#include "ui/gl/gl_surface.h"
#include "ui/gl/init/gl_factory.h"
namespace gpu {
namespace {
std::atomic<bool> g_session_claimed{false};
uint64_t NowMs() { return base::TimeTicks::Now().since_origin().InMilliseconds(); }
bool SameSize(nvvfx_vsr::Dimensions a,nvvfx_vsr::Dimensions b) {
  return a.width==b.width && a.height==b.height;
}
bool Supported(const SharedImageMetadata& info) {
  if(info.usage.Has(SHARED_IMAGE_USAGE_PROTECTED_VIDEO) ||
     !info.color_space.IsValid() || info.color_space.IsHDR()) return false;
  return info.format==viz::MultiPlaneFormat::kNV12 ||
      info.format==viz::SinglePlaneFormat::kRGBX_8888 ||
      info.format==viz::SinglePlaneFormat::kBGRX_8888 ||
      ((info.format==viz::SinglePlaneFormat::kRGBA_8888 ||
        info.format==viz::SinglePlaneFormat::kBGRA_8888) &&
       info.alpha_type==kOpaque_SkAlphaType);
}
void CreateTexture(GLuint* id,nvvfx_vsr::Dimensions size) {
  glGenTextures(1,id);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
  glBindTexture(GL_TEXTURE_2D,*id);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,size.width,size.height,0,
               GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
}
}
struct NvidiaVsrGpuService::TextureSlot {
  GLuint input=0,output=0;
  nvvfx_vsr::ProcessorConfig config{};
};
struct NvidiaVsrGpuService::Job {
  mojom::NvidiaVsrRequestPtr request;
  ProcessCallback callback;
  std::optional<std::size_t> slot;
  NvidiaVsrWorkerResult result;
  bool worker_done=false,source_done=false,output_waiting=false,reset_done=false;
  GLsync owner_fence=nullptr;
  std::unique_ptr<SkiaImageRepresentation> source_rep;
  std::unique_ptr<SkiaImageRepresentation::ScopedReadAccess> source_access;
  sk_sp<SkImage> source_image;
  sk_sp<SkSurface> surface;
};
NvidiaVsrGpuService::NvidiaVsrGpuService(GpuChannel* channel,int32_t source_route,
    int32_t output_route,mojo::PendingReceiver<mojom::NvidiaVsr> receiver)
    :base::RefCountedDeleteOnSequence<NvidiaVsrGpuService>(
          base::SingleThreadTaskRunner::GetCurrentDefault()),
     owner_runner_(base::SingleThreadTaskRunner::GetCurrentDefault()),
     worker_runner_(base::ThreadPool::CreateSingleThreadTaskRunner(
         {base::MayBlock(),base::TaskPriority::USER_VISIBLE},
         base::SingleThreadTaskRunnerThreadMode::DEDICATED)),
     scheduler_(channel->gpu_channel_manager()->scheduler()),
     context_(channel->shared_image_stub()->shared_context_state()),
     channel_factory_(channel->shared_image_stub()->factory()->GetWeakPtr()),
     representations_(std::make_unique<SharedImageRepresentationFactory>(
         channel->gpu_channel_manager()->shared_image_manager(),
         scoped_refptr<MemoryTracker>(channel->shared_image_stub()->memory_tracker()))),
     worker_(nullptr,base::OnTaskRunnerDeleter(worker_runner_)),
     receiver_(this,std::move(receiver)),
     source_id_(CommandBufferIdFromChannelAndRoute(channel->client_id(),source_route)),
     output_id_(CommandBufferIdFromChannelAndRoute(channel->client_id(),output_route)) {
  receiver_.set_disconnect_handler(base::BindOnce(&NvidiaVsrGpuService::Close,
                                                 base::Unretained(this)));
}
NvidiaVsrGpuService::~NvidiaVsrGpuService() {
  DCHECK(owner_runner_->BelongsToCurrentThread());
  DCHECK(jobs_.empty());
}
bool NvidiaVsrGpuService::Initialize() {
  if(!base::FeatureList::IsEnabled(features::kNvidiaVideoSuperResolution) ||
     gl::GetGLImplementation()!=gl::kGLImplementationEGLGLES2 ||
     !context_ || !context_->gr_context() || !context_->MakeCurrent(nullptr,true))
    return false;
  bool expected=false;
  if(!g_session_claimed.compare_exchange_strong(expected,true)) return false;
  claimed_=true;
  auto surface=gl::init::CreateOffscreenGLSurface(
      context_->real_context()->GetGLDisplayEGL(),gfx::Size(1,1));
  auto worker_context=surface ? gl::init::CreateGLContext(
      context_->real_context()->share_group(),surface.get(),gl::GLContextAttribs()) : nullptr;
  if(!worker_context) {g_session_claimed=false;claimed_=false;return false;}
  worker_.reset(new NvidiaVsrGpuWorker(std::move(worker_context),std::move(surface)));
  source_sequence_=scheduler_->CreateSequence(SchedulingPriority::kNormal,owner_runner_,
                                              CommandBufferNamespace::GPU_IO,source_id_);
  output_sequence_=scheduler_->CreateSequence(SchedulingPriority::kNormal,owner_runner_,
                                              CommandBufferNamespace::GPU_IO,output_id_);
  watchdog_.Start(FROM_HERE,base::Milliseconds(10),this,&NvidiaVsrGpuService::Watchdog);
  return true;
}
SyncToken NvidiaVsrGpuService::SourceToken(uint64_t count) const {
  return SyncToken(CommandBufferNamespace::GPU_IO,source_id_,count);
}
SyncToken NvidiaVsrGpuService::OutputToken(uint64_t count) const {
  return SyncToken(CommandBufferNamespace::GPU_IO,output_id_,count);
}
void NvidiaVsrGpuService::Process(mojom::NvidiaVsrRequestPtr request,
                                  ProcessCallback callback) {
  DCHECK(owner_runner_->BelongsToCurrentThread());
  if(closing_ || !claimed_ || request->release_count!=last_count_+1 ||
     request->dependencies.size()>8 || jobs_.size()>=4) {
    mojo::ReportBadMessage("Invalid NVIDIA VSR session/order/queue");Close();return;
  }
  const uint64_t count=request->release_count;last_count_=count;
  auto job=std::make_unique<Job>();job->callback=std::move(callback);
  job->request=std::move(request);
  auto cfg=nvvfx_vsr::SelectProcessorConfig(
      {job->request->visible_rect.width(),job->request->visible_rect.height()},{1920,1080});
  bool valid=!job->request->protected_video && !job->request->encrypted_track &&
      job->request->generation && job->request->frame_id && cfg &&
      cfg->quality==static_cast<int>(job->request->quality) &&
      cfg->strength==job->request->strength &&
      cfg->output.width==job->request->output_size.width() &&
      cfg->output.height==job->request->output_size.height();
  if(valid) {base::AutoLock lock(admission_lock_);
    job->slot=admission_.Reserve(job->request->generation,job->request->frame_id,NowMs());}
  if(!job->slot) {job->worker_done=true;job->result.status="ineligible or busy";}
  auto dependencies=job->request->dependencies;jobs_.emplace(count,std::move(job));
  scheduler_->ScheduleTask(Scheduler::Task(source_sequence_,
      base::BindOnce(&NvidiaVsrGpuService::PrepareSource,base::WrapRefCounted(this),count),
      std::move(dependencies),SourceToken(count)));
  scheduler_->ScheduleTask(Scheduler::Task(output_sequence_,
      base::BindOnce(&NvidiaVsrGpuService::WaitForOutput,base::WrapRefCounted(this),count),
      {SourceToken(count)},OutputToken(count)));
}
void NvidiaVsrGpuService::PrepareSource(uint64_t count) {
  auto& job=*jobs_.at(count);
  if(job.worker_done || closing_) {job.worker_done=true;FinishSource(count);return;}
  auto input=channel_factory_ ? channel_factory_->GetSharedImageMetadata(job.request->input)
                               : std::nullopt;
  auto output=channel_factory_ ? channel_factory_->GetSharedImageMetadata(job.request->output)
                                : std::nullopt;
  if(!input || !output || !Supported(*input) ||
     !gfx::Rect(input->size).Contains(job.request->visible_rect) ||
     output->size!=job.request->output_size ||
     (output->format!=viz::SinglePlaneFormat::kRGBX_8888 &&
      output->format!=viz::SinglePlaneFormat::kRGBA_8888) ||
     output->usage.Has(SHARED_IMAGE_USAGE_PROTECTED_VIDEO) ||
     !context_->MakeCurrent(nullptr,true)) {
    job.worker_done=true;job.result.status="ownership/format/protection/backend bypass";
    FinishSource(count);return;
  }
  auto cfg=*nvvfx_vsr::SelectProcessorConfig(
      {job.request->visible_rect.width(),job.request->visible_rect.height()},{1920,1080});
  const auto slot=*job.slot;
  if(slots_[slot] && (!SameSize(slots_[slot]->config.input,cfg.input) ||
                     !SameSize(slots_[slot]->config.output,cfg.output))) {
    if(!job.reset_done) {
      scheduler_->ContinueTask(source_sequence_,base::BindOnce(
          &NvidiaVsrGpuService::PrepareSource,base::WrapRefCounted(this),count));
      scheduler_->DisableSequence(source_sequence_);
      worker_runner_->PostTask(FROM_HERE,base::BindOnce(&NvidiaVsrGpuWorker::ResetSlot,
          base::Unretained(worker_.get()),slot,base::BindPostTask(owner_runner_,
          base::BindOnce(&NvidiaVsrGpuService::ResumeAfterReset,base::WrapRefCounted(this),count))));
      return;
    }
    glDeleteTextures(1,&slots_[slot]->input);glDeleteTextures(1,&slots_[slot]->output);
    slots_[slot].reset();
  }
  if(!slots_[slot]) {
    slots_[slot]=std::make_unique<TextureSlot>();slots_[slot]->config=cfg;
    CreateTexture(&slots_[slot]->input,cfg.input);CreateTexture(&slots_[slot]->output,cfg.output);
    context_->gr_context()->resetContext();
  }
  job.source_rep=representations_->ProduceSkia(job.request->input,context_);
  std::vector<GrBackendSemaphore> begin,end;
  job.source_access=job.source_rep ? job.source_rep->BeginScopedReadAccess(&begin,&end) : nullptr;
  job.source_image=job.source_access ? job.source_access->CreateSkImage(context_.get()) : nullptr;
  GrGLTextureInfo texture{GL_TEXTURE_2D,slots_[slot]->input,GL_RGBA8};
  auto backend=GrBackendTextures::MakeGL(cfg.input.width,cfg.input.height,
                                       skgpu::Mipmapped::kNo,texture);
  job.surface=SkSurfaces::WrapBackendTexture(context_->gr_context(),backend,
      kTopLeft_GrSurfaceOrigin,0,kRGBA_8888_SkColorType,
      input->color_space.GetAsFullRangeRGB().ToSkColorSpace(),nullptr);
  if(!job.source_image || !job.surface ||
     (!begin.empty() && !job.surface->wait(begin.size(),begin.data(),false))) {
    job.worker_done=true;job.result.status="Skia source conversion unavailable";
    FinishSource(count);return;
  }
  job.surface->getCanvas()->drawImageRect(job.source_image,
      SkRect::MakeXYWH(job.request->visible_rect.x(),job.request->visible_rect.y(),
                      cfg.input.width,cfg.input.height),
      SkRect::MakeWH(cfg.input.width,cfg.input.height),SkSamplingOptions(),nullptr,
      SkCanvas::kStrict_SrcRectConstraint);
  GrFlushInfo flush;
  flush.fNumSemaphores=end.size();flush.fSignalSemaphores=end.data();
  context_->gr_context()->flush(job.surface.get(),SkSurfaces::BackendSurfaceAccess::kNoAccess,flush);
  job.source_access->ApplyBackendSurfaceEndState();context_->gr_context()->submit();
  job.owner_fence=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);
  GLsync worker_fence=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);glFlush();
  scheduler_->ContinueTask(source_sequence_,base::BindOnce(
      &NvidiaVsrGpuService::FinishSource,base::WrapRefCounted(this),count));
  scheduler_->DisableSequence(source_sequence_);
  owner_runner_->PostTask(FROM_HERE,base::BindOnce(
      &NvidiaVsrGpuService::PollSourceFence,base::WrapRefCounted(this),count));
  worker_runner_->PostTask(FROM_HERE,base::BindOnce(&NvidiaVsrGpuWorker::Run,
      base::Unretained(worker_.get()),slot,cfg,slots_[slot]->input,slots_[slot]->output,
      worker_fence,base::DoNothing(),base::BindOnce(&NvidiaVsrGpuService::TryStart,
      base::WrapRefCounted(this),slot),base::BindPostTask(owner_runner_,
      base::BindOnce(&NvidiaVsrGpuService::OnWorkerDone,base::WrapRefCounted(this),count))));
}
void NvidiaVsrGpuService::ResumeAfterReset(uint64_t count,bool safe) {
  auto& job=*jobs_.at(count);job.reset_done=true;
  if(!safe) {unsafe_=true;job.worker_done=true;job.result.status="staging reset unsafe";}
  scheduler_->EnableSequence(source_sequence_);
}
void NvidiaVsrGpuService::PollSourceFence(uint64_t count) {
  auto& job=*jobs_.at(count);
  if(!context_->MakeCurrent(nullptr,true)) {
    unsafe_=true;
    base::AutoLock lock(admission_lock_);
    admission_.Disable();
    return;
  }
  const GLenum state=glClientWaitSync(job.owner_fence,0,0);
  if(state==GL_TIMEOUT_EXPIRED) {
    owner_runner_->PostDelayedTask(FROM_HERE,base::BindOnce(
        &NvidiaVsrGpuService::PollSourceFence,base::WrapRefCounted(this),count),base::Milliseconds(1));
    return;
  }
  if(state!=GL_ALREADY_SIGNALED && state!=GL_CONDITION_SATISFIED) {
    unsafe_=true;{base::AutoLock lock(admission_lock_);admission_.Disable();}return;
  }
  glDeleteSync(job.owner_fence);job.owner_fence=nullptr;
  scheduler_->EnableSequence(source_sequence_);
}
void NvidiaVsrGpuService::FinishSource(uint64_t count) {
  auto& job=*jobs_.at(count);
  job.surface.reset();job.source_image.reset();job.source_access.reset();job.source_rep.reset();
  job.source_done=true;
}
bool NvidiaVsrGpuService::TryStart(std::size_t slot) {
  base::AutoLock lock(admission_lock_);return admission_.Start(slot,NowMs());
}
void NvidiaVsrGpuService::Watchdog() {
  base::AutoLock lock(admission_lock_);admission_.CheckWatchdog(NowMs());
}
void NvidiaVsrGpuService::WaitForOutput(uint64_t count) {
  auto& job=*jobs_.at(count);
  if(job.worker_done) {FinishOutput(count);return;}
  job.output_waiting=true;
  scheduler_->ContinueTask(output_sequence_,base::BindOnce(
      &NvidiaVsrGpuService::FinishOutput,base::WrapRefCounted(this),count));
  scheduler_->DisableSequence(output_sequence_);
}
void NvidiaVsrGpuService::OnWorkerDone(uint64_t count,NvidiaVsrWorkerResult result) {
  auto& job=*jobs_.at(count);job.worker_done=true;job.result=std::move(result);
  unsafe_|=job.result.quarantine;
  if(job.output_waiting) scheduler_->EnableSequence(output_sequence_);
}
bool NvidiaVsrGpuService::Publish(Job& job) {
  if(closing_ || unsafe_ || !channel_factory_ ||
     !channel_factory_->HasSharedImage(job.request->output) ||
     !context_->MakeCurrent(nullptr,true)) return false;
  auto rep=representations_->ProduceGLTexture(job.request->output);
  auto access=rep ? rep->BeginScopedAccess(GL_SHARED_IMAGE_ACCESS_MODE_READWRITE_CHROMIUM,
      SharedImageRepresentation::AllowUnclearedAccess::kYes) : nullptr;
  if(!access || rep->GetTexture()->target()!=GL_TEXTURE_2D) return false;
  GLuint fbo[2]{};glGenFramebuffers(2,fbo);
  glBindFramebuffer(GL_READ_FRAMEBUFFER,fbo[0]);
  glFramebufferTexture2D(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,
                        slots_[*job.slot]->output,0);
  bool valid=glCheckFramebufferStatus(GL_READ_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER,fbo[1]);
  glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,
                        rep->GetTexture()->service_id(),0);
  valid&=glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
  if(valid) {
    const auto size=job.request->output_size;
    glBlitFramebuffer(0,0,size.width(),size.height(),0,0,size.width(),size.height(),
                       GL_COLOR_BUFFER_BIT,GL_NEAREST);
    valid=glGetError()==GL_NO_ERROR;
    if(valid) rep->SetCleared();
  }
  glBindFramebuffer(GL_FRAMEBUFFER,0);glDeleteFramebuffers(2,fbo);
  access.reset();glFlush();context_->gr_context()->resetContext();
  return valid;
}
void NvidiaVsrGpuService::FinishOutput(uint64_t count) {
  auto& job=*jobs_.at(count);
  DCHECK(job.source_done && job.worker_done);
  const bool success=job.result.success && Publish(job);
  if(job.slot) {base::AutoLock lock(admission_lock_);
    if(job.result.quarantine) admission_.Quarantine(*job.slot);
    else admission_.CompleteSafely(*job.slot);}
  std::move(job.callback).Run(success,success ? "completed" : job.result.status,
                             OutputToken(count),job.result.gpu_ms);
  jobs_.erase(count);MaybeShutdown();
}
void NvidiaVsrGpuService::Close() {
  closing_=true;receiver_.reset();
  {base::AutoLock lock(admission_lock_);admission_.Disable();}
  MaybeShutdown();
}
void NvidiaVsrGpuService::MaybeShutdown() {
  if(closing_ && jobs_.empty() && !shutdown_started_) {
    shutdown_started_=true;
    owner_runner_->PostTask(FROM_HERE,base::BindOnce(
        &NvidiaVsrGpuService::Shutdown,base::WrapRefCounted(this)));
  }
}
void NvidiaVsrGpuService::Shutdown() {
  watchdog_.Stop();
  if(!worker_) {OnShutdown(true);return;}
  worker_runner_->PostTask(FROM_HERE,base::BindOnce(&NvidiaVsrGpuWorker::Shutdown,
      base::Unretained(worker_.get()),base::BindPostTask(owner_runner_,
      base::BindOnce(&NvidiaVsrGpuService::OnShutdown,base::WrapRefCounted(this)))));
}
void NvidiaVsrGpuService::OnShutdown(bool safe) {
  unsafe_|=!safe;
  if(source_sequence_) scheduler_->DestroySequence(source_sequence_);
  if(output_sequence_) scheduler_->DestroySequence(output_sequence_);
  source_sequence_=SequenceId();output_sequence_=SequenceId();
  if(unsafe_ || (context_ && !context_->MakeCurrent(nullptr,true))) {
    // Keep the service, native share group and private textures alive until
    // GPU process teardown. No in-flight CUDA registration can be invalidated.
    AddRef();return;
  }
  for(auto& slot:slots_) if(slot) {
    glDeleteTextures(1,&slot->input);glDeleteTextures(1,&slot->output);slot.reset();
  }
  worker_.reset();representations_.reset();context_.reset();channel_factory_.reset();
  if(claimed_) {g_session_claimed=false;claimed_=false;}
}
}  // namespace gpu
