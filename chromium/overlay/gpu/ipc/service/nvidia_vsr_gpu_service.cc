#include "gpu/ipc/service/nvidia_vsr_gpu_service.h"
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
#include "gpu/ipc/common/command_buffer_id.h"
#include "gpu/ipc/service/gpu_channel.h"
#include "gpu/ipc/service/gpu_channel_manager.h"
#include "gpu/ipc/service/shared_image_stub.h"
#include "nvvfx_vsr/processor_config.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkColorSpace.h"
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
#include "ui/gl/gl_version_info.h"
#include "ui/gl/init/gl_factory.h"
namespace gpu {
namespace {
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
bool CreateTexture(GLuint* id,nvvfx_vsr::Dimensions size) {
  glGenTextures(1,id);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
  glBindTexture(GL_TEXTURE_2D,*id);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,size.width,size.height,0,
               GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
  return *id!=0 && glGetError()==GL_NO_ERROR;
}
}
struct NvidiaVsrGpuService::TextureSlot {
  GLuint input=0,output=0;
  nvvfx_vsr::ProcessorConfig config{};
};
struct NvidiaVsrGpuService::Job {
  mojom::NvidiaVsrFrameRequestPtr request;
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
  const auto gl_implementation = gl::GetGLImplementation();
  if (!base::FeatureList::IsEnabled(features::kNvidiaVideoSuperResolution)) {
    LOG(ERROR) << "NVIDIA VSR init: feature disabled";
    return false;
  }
  if (gl_implementation != gl::kGLImplementationEGLGLES2 &&
      gl_implementation != gl::kGLImplementationEGLANGLE) {
    LOG(ERROR) << "NVIDIA VSR init: unsupported GL implementation "
               << gl::GetGLImplementationGLName(gl::GLImplementationParts(gl_implementation));
    return false;
  }
  if (!context_ || !context_->gr_context() ||
      !context_->MakeCurrent(nullptr, true)) {
    LOG(ERROR) << "NVIDIA VSR init: SharedContextState unavailable";
    return false;
  }
  if (!context_->real_context()->GetVersionInfo()->IsAtLeastGLES(3, 0)) {
    LOG(ERROR) << "NVIDIA VSR init: GL version below ES 3.0";
    return false;
  }
  // Each video renderer owns an independent SDK session. CUDA's primary
  // context is retained per worker thread; the SDK processor is not global.
  claimed_=true;
  auto surface=gl::init::CreateOffscreenGLSurface(
      context_->real_context()->GetGLDisplayEGL(),gfx::Size());
  if (!surface) {
    LOG(ERROR) << "NVIDIA VSR init: could not create worker GL surface";
    claimed_=false;return false;
  }
  gl::GLContextAttribs worker_attribs;
  // ANGLE uses its global share groups for textures/semaphores. Native EGL
  // instead shares the command decoder's explicit share group.
  const bool use_angle =
      gl_implementation == gl::kGLImplementationEGLANGLE;
  worker_attribs.global_texture_share_group = use_angle;
  worker_attribs.global_semaphore_share_group = use_angle;
  worker_attribs.robust_resource_initialization = use_angle;
  worker_attribs.robust_buffer_access = use_angle;
  worker_attribs.allow_client_arrays = !use_angle;
  auto worker_context = gl::init::CreateGLContext(
      use_angle ? nullptr : context_->real_context()->share_group(),
      surface.get(), worker_attribs);
  if (!worker_context) {
    LOG(ERROR) << "NVIDIA VSR init: could not create shared GL worker context";
    claimed_=false;return false;
  }
  worker_context_=std::move(worker_context);worker_surface_=std::move(surface);
  worker_.reset(new NvidiaVsrGpuWorker(worker_context_.get(),worker_surface_.get()));
  source_sequence_=scheduler_->CreateSequence(SchedulingPriority::kNormal,owner_runner_,
                                              CommandBufferNamespace::GPU_IO,source_id_);
  output_sequence_=scheduler_->CreateSequence(SchedulingPriority::kNormal,owner_runner_,
                                              CommandBufferNamespace::GPU_IO,output_id_);
  watchdog_.Start(FROM_HERE,base::Milliseconds(10),this,&NvidiaVsrGpuService::Watchdog);
  return true;
}
SyncToken NvidiaVsrGpuService::SourceToken(uint64_t count) const {
  SyncToken token(CommandBufferNamespace::GPU_IO,source_id_,count);
  token.SetVerifyFlush();return token;
}
SyncToken NvidiaVsrGpuService::OutputToken(uint64_t count) const {
  SyncToken token(CommandBufferNamespace::GPU_IO,output_id_,count);
  token.SetVerifyFlush();return token;
}
void NvidiaVsrGpuService::Process(mojom::NvidiaVsrFrameRequestPtr request,
                                  ProcessCallback callback) {
  DCHECK(owner_runner_->BelongsToCurrentThread());
  if(closing_ || !claimed_ || request->release_count!=last_count_+1 ||
     request->dependencies.size()>8 || jobs_.size()>=4) {
    receiver_.ReportBadMessage("Invalid NVIDIA VSR session/order/queue");Close();return;
  }
  const uint64_t count=request->release_count;last_count_=count;
  auto job=std::make_unique<Job>();job->callback=std::move(callback);
  job->request=std::move(request);
  job->result.status="not processed";
  auto cfg=nvvfx_vsr::SelectBrowserProcessorConfig(
      {job->request->visible_rect.width(),job->request->visible_rect.height()});
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
  auto cfg=*nvvfx_vsr::SelectBrowserProcessorConfig(
      {job.request->visible_rect.width(),job.request->visible_rect.height()});
  // Native GL and Skia both mutate state outside Chromium's command decoder.
  context_->set_need_context_state_reset(true);
  // Prove publication backing capability before importing the source or SDK.
  auto output_rep=representations_->ProduceGLTexture(job.request->output, /*gpu_only=*/true);
  if(!output_rep || output_rep->GetTexture()->target()!=GL_TEXTURE_2D ||
     output->color_space!=input->color_space.GetAsFullRangeRGB()) {
    job.worker_done=true;job.result.status="output backing/color bypass";
    FinishSource(count);return;
  }
  output_rep.reset();
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
    const bool allocated=CreateTexture(&slots_[slot]->input,cfg.input) &&
        CreateTexture(&slots_[slot]->output,cfg.output);
    context_->gr_context()->resetContext();
    if(!allocated) {
      glDeleteTextures(1,&slots_[slot]->input);glDeleteTextures(1,&slots_[slot]->output);
      slots_[slot].reset();job.worker_done=true;
      job.result.status="private texture allocation failed";FinishSource(count);return;
    }
  }
  job.source_rep=representations_->ProduceSkia(
      job.request->input,context_,{},/*gpu_only=*/true);
  std::vector<GrBackendSemaphore> begin,end;
  job.source_access=job.source_rep ?
      job.source_rep->BeginScopedReadAccess(&begin,&end) : nullptr;
  job.source_image=job.source_access ?
      job.source_access->CreateSkImage(context_.get()) : nullptr;
  GrGLTextureInfo texture{GL_TEXTURE_2D,slots_[slot]->input,GL_RGBA8};
  auto backend=GrBackendTextures::MakeGL(cfg.input.width,cfg.input.height,
      skgpu::Mipmapped::kNo,texture);
  job.surface=SkSurfaces::WrapBackendTexture(context_->gr_context(),backend,
      kTopLeft_GrSurfaceOrigin,0,kRGBA_8888_SkColorType,
      input->color_space.GetAsFullRangeRGB().ToSkColorSpace(),nullptr);
  if(!job.source_image || !job.surface ||
     (!begin.empty() && !job.surface->wait(begin.size(),begin.data(),false))) {
    job.worker_done=true;job.result.status="Skia source conversion unavailable";
    if(!job.source_access) {FinishSource(count);return;}
    GrFlushInfo cleanup;
    cleanup.fNumSemaphores=end.size();cleanup.fSignalSemaphores=end.data();
    context_->gr_context()->flush(cleanup);
    job.source_access->ApplyBackendSurfaceEndState();
    context_->gr_context()->submit();
    job.owner_fence=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);glFlush();
    scheduler_->ContinueTask(source_sequence_,base::BindOnce(
        &NvidiaVsrGpuService::FinishSource,base::WrapRefCounted(this),count));
    scheduler_->DisableSequence(source_sequence_);
    owner_runner_->PostTask(FROM_HERE,base::BindOnce(
        &NvidiaVsrGpuService::PollSourceFence,base::WrapRefCounted(this),count));
    return;
  }
  job.surface->getCanvas()->drawImageRect(job.source_image,
      SkRect::MakeXYWH(job.request->visible_rect.x(),job.request->visible_rect.y(),
                       cfg.input.width,cfg.input.height),
      SkRect::MakeWH(cfg.input.width,cfg.input.height),SkSamplingOptions(),nullptr,
      SkCanvas::kStrict_SrcRectConstraint);
  GrFlushInfo flush;
  flush.fNumSemaphores=end.size();flush.fSignalSemaphores=end.data();
  const auto flushed=context_->gr_context()->flush(
      job.surface.get(),SkSurfaces::BackendSurfaceAccess::kNoAccess,flush);
  job.source_access->ApplyBackendSurfaceEndState();
  const bool submitted=context_->gr_context()->submit();
  const bool source_valid=flushed.fSuccess && submitted &&
      (end.empty() || flushed.fSubmitted==GrSemaphoresSubmitted::kYes);
  if(!source_valid) {
    job.worker_done=true;job.result.status="Skia source flush/submit failed";
  }
  job.owner_fence=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);
  glFlush();
  GLsync worker_fence=source_valid ? glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0) : nullptr;
  glFlush();
  if(source_valid && !worker_fence) {
    job.worker_done=true;job.result.status="worker fence unavailable";
  }
  scheduler_->ContinueTask(source_sequence_,base::BindOnce(
      &NvidiaVsrGpuService::FinishSource,base::WrapRefCounted(this),count));
  scheduler_->DisableSequence(source_sequence_);
  owner_runner_->PostTask(FROM_HERE,base::BindOnce(
      &NvidiaVsrGpuService::PollSourceFence,base::WrapRefCounted(this),count));
  if(!worker_fence) return;
  worker_runner_->PostTask(FROM_HERE,base::BindOnce(&NvidiaVsrGpuWorker::Run,
      base::Unretained(worker_.get()),slot,cfg,slots_[slot]->input,slots_[slot]->output,
      NvidiaVsrSourceFence{worker_fence},base::DoNothing(),base::BindOnce(&NvidiaVsrGpuService::TryStart,
      base::WrapRefCounted(this),slot),base::BindOnce(
      &NvidiaVsrGpuService::OnWorkerFinished,base::WrapRefCounted(this),count,slot)));
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
  // Polling and this scheduler continuation are separate tasks. Another GPU
  // client may have changed the current GL context between them. Skia access
  // must end on the same context on which it began.
  if(!context_ || !context_->MakeCurrent(nullptr,true)) {
    unsafe_=true;
    {base::AutoLock lock(admission_lock_);admission_.Disable();}
    // Retain the source access and its unreleased token on context failure.
    scheduler_->ContinueTask(source_sequence_,base::BindOnce(
        &NvidiaVsrGpuService::FinishSource,base::WrapRefCounted(this),count));
    scheduler_->DisableSequence(source_sequence_);
    return;
  }
  context_->set_need_context_state_reset(true);
  job.source_access.reset();job.source_rep.reset();
  job.source_done=true;
}
bool NvidiaVsrGpuService::TryStart(std::size_t slot) {
  base::AutoLock lock(admission_lock_);return admission_.Start(slot,NowMs());
}
void NvidiaVsrGpuService::Watchdog() {
  base::AutoLock lock(admission_lock_);
  const bool was_disabled = admission_.disabled();
  admission_.CheckWatchdog(NowMs());
  if (!was_disabled && admission_.disabled())
    LOG(WARNING) << "NVIDIA VSR watchdog disabled session: running job exceeded 100ms";
}
void NvidiaVsrGpuService::WaitForOutput(uint64_t count) {
  auto& job=*jobs_.at(count);
  if(job.worker_done) {FinishOutput(count);return;}
  job.output_waiting=true;
  scheduler_->ContinueTask(output_sequence_,base::BindOnce(
      &NvidiaVsrGpuService::FinishOutput,base::WrapRefCounted(this),count));
  scheduler_->DisableSequence(output_sequence_);
}
void NvidiaVsrGpuService::OnWorkerFinished(uint64_t count,std::size_t slot,
                                          NvidiaVsrWorkerResult result) {
  // Runs on the serial worker before it can begin the next queued inference.
  // Owner publication may lag behind; keep the slot occupied until its copy.
  {
    base::AutoLock lock(admission_lock_);
    if(result.quarantine) admission_.Quarantine(slot);
    else admission_.FinishInference(slot);
  }
  owner_runner_->PostTask(FROM_HERE,base::BindOnce(
      &NvidiaVsrGpuService::OnWorkerDone,base::WrapRefCounted(this),count,
      std::move(result)));
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
  context_->set_need_context_state_reset(true);
  auto rep=representations_->ProduceGLTexture(job.request->output, /*gpu_only=*/true);
  auto access=rep ? rep->BeginScopedAccess(GL_SHARED_IMAGE_ACCESS_MODE_READWRITE_CHROMIUM,
      SharedImageRepresentation::AllowUnclearedAccess::kYes) : nullptr;
  if(!access || rep->GetTexture()->target()!=GL_TEXTURE_2D) return false;
  GLuint fbo[2]{};glGenFramebuffersEXT(2,fbo);
  glBindFramebufferEXT(GL_READ_FRAMEBUFFER,fbo[0]);
  glFramebufferTexture2DEXT(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,
                        slots_[*job.slot]->output,0);
  bool valid=glCheckFramebufferStatusEXT(GL_READ_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
  glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER,fbo[1]);
  glFramebufferTexture2DEXT(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,
                        rep->GetTexture()->service_id(),0);
  valid&=glCheckFramebufferStatusEXT(GL_DRAW_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
  if(valid) {
    const auto size=job.request->output_size;
    // Blits obey scissor and window rectangles inherited from Skia.
    glDisable(GL_SCISSOR_TEST);
    if(context_->real_context()->HasExtension("GL_EXT_window_rectangles"))
      glWindowRectanglesEXT(GL_EXCLUSIVE_EXT,0,nullptr);
    glBlitFramebuffer(0,0,size.width(),size.height(),0,0,size.width(),size.height(),
                       GL_COLOR_BUFFER_BIT,GL_NEAREST);
    valid=glGetError()==GL_NO_ERROR;
    if(valid) rep->SetCleared();
  }
  glBindFramebufferEXT(GL_FRAMEBUFFER,0);glDeleteFramebuffersEXT(2,fbo);
  access.reset();glFlush();context_->gr_context()->resetContext();
  return valid;
}
void NvidiaVsrGpuService::FinishOutput(uint64_t count) {
  auto& job=*jobs_.at(count);
  DCHECK(job.source_done && job.worker_done);
  bool enabled;
  {base::AutoLock lock(admission_lock_);enabled=!admission_.disabled();}
  const bool success=job.result.success && enabled && Publish(job);
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
  context_->set_need_context_state_reset(true);
  for(auto& slot:slots_) if(slot) {
    glDeleteTextures(1,&slot->input);glDeleteTextures(1,&slot->output);slot.reset();
  }
  context_->gr_context()->resetContext();
  worker_.reset();worker_context_.reset();worker_surface_.reset();
  representations_.reset();context_.reset();channel_factory_.reset();
  claimed_=false;
}
}  // namespace gpu
