#ifndef GPU_IPC_SERVICE_NVIDIA_VSR_GPU_SERVICE_H_
#define GPU_IPC_SERVICE_NVIDIA_VSR_GPU_SERVICE_H_
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include "base/memory/ref_counted_delete_on_sequence.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/memory/on_task_runner_deleter.h"
#include "base/synchronization/lock.h"
#include "base/task/single_thread_task_runner.h"
#include "base/timer/timer.h"
#include "gpu/command_buffer/common/command_buffer_id.h"
#include "gpu/command_buffer/common/sync_token.h"
#include "gpu/command_buffer/service/sequence_id.h"
#include "gpu/ipc/common/nvidia_vsr.mojom.h"
#include "gpu/ipc/service/nvidia_vsr_gpu_worker.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "nvvfx_vsr/job_admission.h"
namespace gpu {
class GpuChannel;
class Scheduler;
class SharedContextState;
class SharedImageFactory;
class SharedImageRepresentationFactory;
class NvidiaVsrGpuService
    : public base::RefCountedDeleteOnSequence<NvidiaVsrGpuService>,
      public mojom::NvidiaVsr {
 public:
  NvidiaVsrGpuService(GpuChannel* channel,int32_t source_route,int32_t output_route,
                      mojo::PendingReceiver<mojom::NvidiaVsr> receiver);
  bool Initialize();
  void Close();
  void Process(mojom::NvidiaVsrRequestPtr request,
                ProcessCallback callback) override;
 private:
  friend class base::RefCountedDeleteOnSequence<NvidiaVsrGpuService>;
  friend class base::DeleteHelper<NvidiaVsrGpuService>;
  ~NvidiaVsrGpuService() override;
  struct Job;
  struct TextureSlot;
  void PrepareSource(uint64_t count);
  void ResumeAfterReset(uint64_t count,bool safe);
  void PollSourceFence(uint64_t count);
  void FinishSource(uint64_t count);
  void WaitForOutput(uint64_t count);
  void OnWorkerDone(uint64_t count,NvidiaVsrWorkerResult result);
  void FinishOutput(uint64_t count);
  bool Publish(Job& job);
  bool TryStart(std::size_t slot);
  void Watchdog();
  void MaybeShutdown();
  void Shutdown();
  void OnShutdown(bool safe);
  gpu::SyncToken SourceToken(uint64_t count) const;
  gpu::SyncToken OutputToken(uint64_t count) const;
  scoped_refptr<base::SingleThreadTaskRunner> owner_runner_;
  scoped_refptr<base::SingleThreadTaskRunner> worker_runner_;
  raw_ptr<Scheduler> scheduler_;
  scoped_refptr<SharedContextState> context_;
  base::WeakPtr<SharedImageFactory> channel_factory_;
  std::unique_ptr<SharedImageRepresentationFactory> representations_;
  std::unique_ptr<NvidiaVsrGpuWorker,base::OnTaskRunnerDeleter> worker_;
  mojo::Receiver<mojom::NvidiaVsr> receiver_;
  CommandBufferId source_id_,output_id_;
  SequenceId source_sequence_,output_sequence_;
  std::array<std::unique_ptr<TextureSlot>,3> slots_;
  std::map<uint64_t,std::unique_ptr<Job>> jobs_;
  base::Lock admission_lock_;
  nvvfx_vsr::JobAdmission admission_ GUARDED_BY(admission_lock_);
  base::RepeatingTimer watchdog_;
  uint64_t last_count_=0;
  bool claimed_=false,closing_=false,shutdown_started_=false,unsafe_=false;
};
}  // namespace gpu
#endif
