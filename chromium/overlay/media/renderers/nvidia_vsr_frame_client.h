#ifndef MEDIA_RENDERERS_NVIDIA_VSR_FRAME_CLIENT_H_
#define MEDIA_RENDERERS_NVIDIA_VSR_FRAME_CLIENT_H_
#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <map>
#include <set>
#include <string>
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/task/sequenced_task_runner.h"
#include "gpu/ipc/common/nvidia_vsr.mojom.h"
#include "media/base/media_export.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "nvvfx_vsr/frame_eligibility.h"
namespace gpu { class SharedImageInterface; class GpuChannelHost; class ClientSharedImage; }
namespace media {
class MediaLog;
class VideoFrame;
class GpuVideoAcceleratorFactories;
MEDIA_EXPORT nvvfx_vsr::FrameBypass ClassifyNvidiaVsrFrame(
    const VideoFrame& frame,bool encrypted_track);
class MEDIA_EXPORT NvidiaVsrFrameClient {
 public:
  using ReadyCB = base::RepeatingCallback<void(uint64_t,uint64_t,scoped_refptr<VideoFrame>)>;
  explicit NvidiaVsrFrameClient(MediaLog* log,
      GpuVideoAcceleratorFactories* factories=nullptr,ReadyCB ready={},
      base::RepeatingClosure disconnected={});
  ~NvidiaVsrFrameClient();
  void SetCallbacks(ReadyCB ready,base::RepeatingClosure disconnected);
  bool Observe(scoped_refptr<VideoFrame> frame,bool encrypted_track,
               std::uint64_t generation);
  void SetCapacityCallback(base::RepeatingClosure callback);
 private:
  void NotifyCapacity();
  base::RepeatingClosure capacity_;
  struct Slot;
  struct FrameSnapshot;
  void Connect();
  void OnConnected(bool available);
  void OnDisconnected();
  bool Submit(scoped_refptr<VideoFrame> frame,uint64_t generation);
  void OnCompleted(std::shared_ptr<Slot> slot,FrameSnapshot original,
      uint64_t generation,uint64_t frame_id,bool success,
      const std::string& status,
      const gpu::SyncToken& token,float gpu_ms);
  static void ReturnLease(base::WeakPtr<NvidiaVsrFrameClient> client,
      std::shared_ptr<Slot> slot,
      scoped_refptr<base::SequencedTaskRunner> runner,const gpu::SyncToken& token);
  std::unique_ptr<MediaLog> log_;
  raw_ptr<GpuVideoAcceleratorFactories> factories_;
  ReadyCB ready_;
  base::RepeatingClosure disconnected_;
  std::set<std::string> reported_;
  scoped_refptr<base::SequencedTaskRunner> runner_;
  scoped_refptr<gpu::SharedImageInterface> sii_;
  scoped_refptr<gpu::GpuChannelHost> channel_;
  mojo::Remote<gpu::mojom::NvidiaVsr> remote_;
  // Output leases include the current compositor frame and future results.
  // Five surfaces add ~16 MiB over three at 1080p; GPU work remains two jobs.
  std::array<std::shared_ptr<Slot>,5> slots_;
  std::set<std::pair<uint64_t,uint64_t>> seen_;
  std::deque<std::pair<uint64_t,uint64_t>> seen_order_;
  int32_t source_route_=0,output_route_=0;
  uint64_t count_=0,submitted_=0,completed_=0;
  unsigned in_flight_=0;
  std::map<std::pair<uint64_t,uint64_t>, scoped_refptr<VideoFrame>> source_leases_;
  bool connecting_=false,available_=false,disabled_=false;
  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<NvidiaVsrFrameClient> weak_factory_{this};
};
}  // namespace media
#endif
