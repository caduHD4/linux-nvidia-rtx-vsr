#include "media/renderers/nvidia_vsr_frame_client.h"
#include <algorithm>
#include "media/renderers/nvidia_vsr_source_release.h"
#include "base/functional/callback_helpers.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ref.h"
#include "base/test/task_environment.h"
#include "base/scoped_environment_variable_override.h"
#include "base/test/scoped_feature_list.h"
#include "gpu/command_buffer/client/test_shared_image_interface.h"
#include "gpu/ipc/client/gpu_channel_host.h"
#include "gpu/ipc/common/command_buffer_id.h"
#include "gpu/ipc/common/mock_gpu_channel.h"
#include "media/base/mock_media_log.h"
#include "media/video/mock_gpu_video_accelerator_factories.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "ui/gl/gl_features.h"
#include "gpu/command_buffer/client/client_shared_image.h"
#include "gpu/command_buffer/common/shared_image_info.h"
#include "media/base/video_frame.h"
#include "media/base/format_utils.h"
#include "media/base/video_types.h"
#include "testing/gtest/include/gtest/gtest.h"
namespace media {
namespace {
scoped_refptr<VideoFrame> MakeGpuFrame(gfx::Size size=gfx::Size(1280,720),
    gfx::ColorSpace color=gfx::ColorSpace::CreateREC709()) {
  gpu::SharedImageMetadata metadata;
  metadata.format=viz::SinglePlaneFormat::kRGBX_8888;
  metadata.size=size;
  metadata.color_space=color;
  metadata.alpha_type=kOpaque_SkAlphaType;
  metadata.surface_origin=kTopLeft_GrSurfaceOrigin;
  metadata.usage=gpu::SHARED_IMAGE_USAGE_RASTER_READ;
  auto frame=VideoFrame::WrapSharedImage(PIXEL_FORMAT_XBGR,
      gpu::ClientSharedImage::CreateForTesting(metadata),{},base::DoNothing(),
      gfx::Rect(metadata.size),metadata.size,base::TimeDelta());
  frame->set_color_space(metadata.color_space);
  return frame;
}
class VsrTestChannelHost : public gpu::GpuChannelHost {
 public:
  explicit VsrTestChannelHost(gpu::mojom::GpuChannel& channel)
      :GpuChannelHost(41,mojo::ScopedMessagePipeHandle()),channel_(channel) {}
  gpu::mojom::GpuChannel& GetGpuChannel() override {return *channel_;}
 private:
  ~VsrTestChannelHost() override=default;
  const raw_ref<gpu::mojom::GpuChannel> channel_;
};
class VsrTestImageInterface : public gpu::TestSharedImageInterface {
 public:
  explicit VsrTestImageInterface(scoped_refptr<gpu::GpuChannelHost> channel)
      :channel_(std::move(channel)) {}
  gpu::GpuChannelHost* GetGpuChannelForNvidiaVsr() override {return channel_.get();}
  bool CanVerifySyncToken(const gpu::SyncToken& token) override {
    return can_verify && TestSharedImageInterface::CanVerifySyncToken(token);
  }
  bool can_verify=true;
 private:
  ~VsrTestImageInterface() override=default;
  scoped_refptr<gpu::GpuChannelHost> channel_;
};
class VsrClientIpcTest : public testing::Test,public gpu::mojom::NvidiaVsr {
 public:
  struct Pending {
    gpu::mojom::NvidiaVsrFrameRequestPtr request;
    ProcessCallback callback;
  };
  void SetUp() override {
    features_.InitAndEnableFeature(features::kNvidiaVideoSuperResolution);
    host_=base::MakeRefCounted<VsrTestChannelHost>(channel_);
    sii_=base::MakeRefCounted<VsrTestImageInterface>(host_);
    factories_=std::make_unique<MockGpuVideoAcceleratorFactories>(sii_.get());
    EXPECT_CALL(channel_,CreateNvidiaVsr(testing::_,testing::_,testing::_,testing::_))
      .WillOnce([this](int32_t source,int32_t output,
          mojo::PendingReceiver<gpu::mojom::NvidiaVsr> receiver,
          gpu::mojom::GpuChannel::CreateNvidiaVsrCallback callback) {
        source_route_=source;output_route_=output;
        receiver_.Bind(std::move(receiver));std::move(callback).Run(true);
      });
    client_=std::make_unique<NvidiaVsrFrameClient>(&log_,factories_.get(),
      base::BindRepeating([](VsrClientIpcTest* self,uint64_t gen,uint64_t id,scoped_refptr<VideoFrame> frame) {
        self->completed_ids_.push_back(id);self->outputs_.push_back(std::move(frame));
      },base::Unretained(this)),base::BindRepeating([](VsrClientIpcTest* self){++self->invalidations_;},base::Unretained(this)));
    client_->Observe(MakeGpuFrame(),false,1);
    environment_.RunUntilIdle();
  }
  void TearDown() override {
    receiver_.reset();client_.reset();environment_.RunUntilIdle();
    pending_.clear();outputs_.clear();environment_.RunUntilIdle();
    factories_.reset();sii_.reset();host_.reset();environment_.RunUntilIdle();
  }
  void Process(gpu::mojom::NvidiaVsrFrameRequestPtr request,ProcessCallback callback) override {
    pending_.push_back({std::move(request),std::move(callback)});
  }
  void Complete(size_t index,bool success=true) {
    auto& pending=pending_.at(index);
    gpu::SyncToken token(gpu::CommandBufferNamespace::GPU_IO,
      gpu::CommandBufferIdFromChannelAndRoute(41,output_route_),pending.request->release_count);
    token.SetVerifyFlush();
    std::move(pending.callback).Run(success,"test",token,3.0f);
    environment_.RunUntilIdle();
  }
 protected:
  base::test::TaskEnvironment environment_;
  base::test::ScopedFeatureList features_;
  testing::NiceMock<gpu::MockGpuChannel> channel_;
  testing::NiceMock<MockMediaLog> log_;
  scoped_refptr<VsrTestChannelHost> host_;
  scoped_refptr<VsrTestImageInterface> sii_;
  std::unique_ptr<MockGpuVideoAcceleratorFactories> factories_;
  std::unique_ptr<NvidiaVsrFrameClient> client_;
  mojo::Receiver<gpu::mojom::NvidiaVsr> receiver_{this};
  std::vector<Pending> pending_;
  std::vector<scoped_refptr<VideoFrame>> outputs_;
  std::vector<uint64_t> completed_ids_;
  int32_t source_route_=0,output_route_=0;
  int invalidations_=0;
};
TEST_F(VsrClientIpcTest, SupersampledOutputPreservesNaturalSizeAndTimestamp) {
  base::ScopedEnvironmentVariableOverride preset("NVVFX_VSR_TARGET_HEIGHT","1440");
  auto original=MakeGpuFrame(gfx::Size(1920,1080));
  original->set_timestamp(base::Milliseconds(42));
  client_->Observe(original,false,1);environment_.RunUntilIdle();
  ASSERT_EQ(pending_.size(),1u);
  EXPECT_EQ(pending_[0].request->output_size,gfx::Size(2560,1440));
  EXPECT_EQ(pending_[0].request->quality,4u);
  Complete(0);ASSERT_EQ(outputs_.size(),1u);
  EXPECT_EQ(outputs_[0]->coded_size(),gfx::Size(2560,1440));
  EXPECT_EQ(outputs_[0]->visible_rect(),gfx::Rect(2560,1440));
  EXPECT_EQ(outputs_[0]->natural_size(),original->natural_size());
  EXPECT_EQ(outputs_[0]->timestamp(),original->timestamp());
}
TEST_F(VsrClientIpcTest, OneSubmissionPerDecodedFrameAndTwoPendingMaximum) {
  auto first=MakeGpuFrame();
  for(int i=0;i<180;++i) client_->Observe(first,false,1);
  client_->Observe(MakeGpuFrame(),false,1);
  client_->Observe(MakeGpuFrame(),false,1);
  environment_.RunUntilIdle();
  ASSERT_EQ(pending_.size(),2u);
  EXPECT_EQ(pending_[0].request->frame_id,first->unique_id().GetUnsafeValue());
  EXPECT_EQ(pending_[0].request->quality,4u);
  EXPECT_EQ(pending_[0].request->strength,1.0f);
  EXPECT_EQ(pending_[0].request->output_size,gfx::Size(1920,1080));
  Complete(0);
  ASSERT_EQ(outputs_.size(),1u);
  EXPECT_EQ(outputs_[0]->natural_size(),first->natural_size());
}
TEST_F(VsrClientIpcTest, BusyFrameCanBeRetriedAfterCapacityReturns) {
  client_->Observe(MakeGpuFrame(),false,1);
  client_->Observe(MakeGpuFrame(),false,1);
  auto waiting=MakeGpuFrame();
  client_->Observe(waiting,false,1);
  environment_.RunUntilIdle();
  ASSERT_EQ(pending_.size(),2u);
  Complete(0);
  client_->Observe(waiting,false,1);
  environment_.RunUntilIdle();
  ASSERT_EQ(pending_.size(),3u);
  EXPECT_EQ(pending_[2].request->frame_id,waiting->unique_id().GetUnsafeValue());
}
TEST_F(VsrClientIpcTest, SuccessfulOutputIsOpaqueAndPreservesMetadataAndLease) {
  auto original = MakeGpuFrame();
  original->set_timestamp(base::Milliseconds(37));
  original->metadata().frame_duration = base::Milliseconds(40);
  client_->Observe(original, false, 17);
  environment_.RunUntilIdle();
  ASSERT_EQ(pending_.size(), 1u);
  EXPECT_EQ(pending_[0].request->generation, 17u);
  Complete(0);
  ASSERT_EQ(outputs_.size(), 1u);
  ASSERT_EQ(completed_ids_.size(), 1u);
  EXPECT_EQ(completed_ids_[0], original->unique_id().GetUnsafeValue());

  const auto output_mailbox = outputs_[0]->shared_image()->mailbox();
  EXPECT_EQ(outputs_[0]->format(), PIXEL_FORMAT_XBGR);
  EXPECT_TRUE(IsOpaque(outputs_[0]->format()));
  EXPECT_EQ(outputs_[0]->shared_image()->format(),
            viz::SinglePlaneFormat::kRGBX_8888);
  EXPECT_EQ(VideoPixelFormatToSharedImageFormat(outputs_[0]->format()),
            outputs_[0]->shared_image()->format());
  EXPECT_EQ(outputs_[0]->shared_image()->alpha_type(), kOpaque_SkAlphaType);
  EXPECT_EQ(outputs_[0]->coded_size(), gfx::Size(1920, 1080));
  EXPECT_EQ(outputs_[0]->visible_rect(), gfx::Rect(1920, 1080));
  EXPECT_EQ(outputs_[0]->natural_size(), original->natural_size());
  EXPECT_EQ(outputs_[0]->timestamp(), original->timestamp());
  EXPECT_EQ(outputs_[0]->metadata().frame_duration,
            original->metadata().frame_duration);
  EXPECT_EQ(outputs_[0]->ColorSpace(), original->ColorSpace().GetAsFullRangeRGB());
  EXPECT_TRUE(outputs_[0]->acquire_sync_token().HasData());
  EXPECT_TRUE(outputs_[0]->acquire_sync_token().verified_flush());

  client_->Observe(MakeGpuFrame(), false, 17);
  environment_.RunUntilIdle();
  ASSERT_EQ(pending_.size(), 2u);
  EXPECT_NE(pending_[1].request->output, output_mailbox);

  gpu::SyncToken consumed(gpu::CommandBufferNamespace::GPU_IO,
                         gpu::CommandBufferId::FromUnsafeValue(4343), 9);
  consumed.SetVerifyFlush();
  std::vector<gpu::SyncToken> dependencies;
  NvidiaVsrSourceRelease consumer(consumed, &dependencies);
  outputs_[0]->UpdateReleaseSyncToken(&consumer);
  outputs_.clear();
  environment_.RunUntilIdle();
  client_->Observe(MakeGpuFrame(), false, 17);
  environment_.RunUntilIdle();
  ASSERT_EQ(pending_.size(), 3u);
  EXPECT_EQ(pending_[2].request->output, output_mailbox);
  EXPECT_NE(std::find(pending_[2].request->dependencies.begin(),
                      pending_[2].request->dependencies.end(), consumed),
            pending_[2].request->dependencies.end());
}
TEST_F(VsrClientIpcTest, SourceReleaseMergesPriorUseWithoutWaitingForInference) {
  auto frame=MakeGpuFrame();
  gpu::SyncToken prior(gpu::CommandBufferNamespace::GPU_IO,
      gpu::CommandBufferId::FromUnsafeValue(4040),3);
  prior.SetVerifyFlush();
  std::vector<gpu::SyncToken> dependencies;
  NvidiaVsrSourceRelease previous(prior,&dependencies);
  frame->UpdateReleaseSyncToken(&previous);
  client_->Observe(frame,false,1);environment_.RunUntilIdle();
  ASSERT_EQ(pending_.size(),1u);
  const auto& request=*pending_[0].request;
  EXPECT_NE(std::find(request.dependencies.begin(),request.dependencies.end(),prior),
            request.dependencies.end());
  gpu::SyncToken later(gpu::CommandBufferNamespace::GPU_IO,
      gpu::CommandBufferId::FromUnsafeValue(5050),4);
  NvidiaVsrSourceRelease inspect(later,&dependencies);
  frame->UpdateReleaseSyncToken(&inspect);
  ASSERT_EQ(dependencies.size(),1u);
  EXPECT_EQ(dependencies[0].command_buffer_id(),
      gpu::CommandBufferIdFromChannelAndRoute(41,source_route_));
  EXPECT_EQ(dependencies[0].release_count(),request.release_count);
  EXPECT_TRUE(outputs_.empty());
}
TEST_F(VsrClientIpcTest, PendingInferenceRetainsDecoderFrameUntilSourceCopyAck) {
  auto frame=MakeGpuFrame();
  bool released=false;
  frame->AddDestructionObserver(base::BindOnce(
      [](bool* value){*value=true;},base::Unretained(&released)));
  client_->Observe(frame,false,1);environment_.RunUntilIdle();
  ASSERT_EQ(pending_.size(),1u);
  frame.reset();
  EXPECT_FALSE(released);
  EXPECT_TRUE(outputs_.empty());
  Complete(0);
  EXPECT_TRUE(released);
  ASSERT_EQ(outputs_.size(),1u);
  EXPECT_EQ(outputs_[0]->natural_size(),gfx::Size(1280,720));
}
TEST_F(VsrClientIpcTest, UnverifiableAcquireBypassesWithoutSkippingReleaseCount) {
  auto frame=MakeGpuFrame();
  gpu::SyncToken acquire(gpu::CommandBufferNamespace::GPU_IO,
      gpu::CommandBufferId::FromUnsafeValue(9090),3);
  frame->UpdateAcquireSyncToken(acquire);
  sii_->can_verify=false;
  client_->Observe(frame,false,1);environment_.RunUntilIdle();
  EXPECT_TRUE(pending_.empty());
  sii_->can_verify=true;
  client_->Observe(MakeGpuFrame(),false,1);environment_.RunUntilIdle();
  ASSERT_EQ(pending_.size(),1u);
  EXPECT_EQ(pending_[0].request->release_count,1u);
}
TEST_F(VsrClientIpcTest, UnverifiablePriorUsePreservesOriginalReleaseToken) {
  auto frame=MakeGpuFrame();
  gpu::SyncToken prior(gpu::CommandBufferNamespace::GPU_IO,
      gpu::CommandBufferId::FromUnsafeValue(9090),3);
  std::vector<gpu::SyncToken> dependencies;
  NvidiaVsrSourceRelease previous(prior,&dependencies);
  frame->UpdateReleaseSyncToken(&previous);
  sii_->can_verify=false;
  client_->Observe(frame,false,1);environment_.RunUntilIdle();
  EXPECT_TRUE(pending_.empty());
  gpu::SyncToken later(gpu::CommandBufferNamespace::GPU_IO,
      gpu::CommandBufferId::FromUnsafeValue(5050),4);
  NvidiaVsrSourceRelease inspect(later,&dependencies);
  frame->UpdateReleaseSyncToken(&inspect);
  ASSERT_EQ(dependencies.size(),1u);
  EXPECT_EQ(dependencies[0],prior);
  sii_->can_verify=true;
  client_->Observe(MakeGpuFrame(),false,1);environment_.RunUntilIdle();
  ASSERT_EQ(pending_.size(),1u);
  EXPECT_EQ(pending_[0].request->release_count,1u);
}
TEST_F(VsrClientIpcTest, FailedInferencePublishesNothingAndNativeUsesMode11) {
  client_->Observe(MakeGpuFrame(gfx::Size(1920,1080)),false,1);
  environment_.RunUntilIdle();ASSERT_EQ(pending_.size(),1u);
  EXPECT_EQ(pending_[0].request->quality,11u);
  EXPECT_EQ(pending_[0].request->visible_rect.size(),pending_[0].request->output_size);
  Complete(0,false);EXPECT_TRUE(outputs_.empty());
  client_->Observe(MakeGpuFrame(),false,2);environment_.RunUntilIdle();
  EXPECT_EQ(pending_.size(),2u);
}
TEST_F(VsrClientIpcTest, OutputPoolWaitsForConsumerLeaseAndCarriesItsFence) {
  for(size_t i=0;i<5;++i) {
    client_->Observe(MakeGpuFrame(),false,1);
    environment_.RunUntilIdle();
    ASSERT_EQ(pending_.size(),i+1);
    Complete(i);
  }
  client_->Observe(MakeGpuFrame(),false,1);environment_.RunUntilIdle();
  ASSERT_EQ(pending_.size(),5u);
  gpu::SyncToken consumed(gpu::CommandBufferNamespace::GPU_IO,
      gpu::CommandBufferId::FromUnsafeValue(4343),9);
  consumed.SetVerifyFlush();
  std::vector<gpu::SyncToken> dependencies;
  NvidiaVsrSourceRelease consumer(consumed,&dependencies);
  outputs_[0]->UpdateReleaseSyncToken(&consumer);
  outputs_.clear();environment_.RunUntilIdle();
  client_->Observe(MakeGpuFrame(),false,1);environment_.RunUntilIdle();
  ASSERT_EQ(pending_.size(),6u);
  EXPECT_NE(std::find(pending_[5].request->dependencies.begin(),
      pending_[5].request->dependencies.end(),consumed),pending_[5].request->dependencies.end());
}
TEST_F(VsrClientIpcTest, ProtectionCreatesNoSubmissionAndDisconnectInvalidates) {
  client_->Observe(MakeGpuFrame(),true,1);
  auto protected_frame=MakeGpuFrame();protected_frame->metadata().protected_video=true;
  client_->Observe(protected_frame,false,1);environment_.RunUntilIdle();
  EXPECT_TRUE(pending_.empty());
  receiver_.reset();environment_.RunUntilIdle();
  EXPECT_EQ(invalidations_,1);
  client_->Observe(MakeGpuFrame(),false,1);environment_.RunUntilIdle();
  EXPECT_TRUE(pending_.empty());
}
TEST(NvidiaVsrFrameClientTest, CopyReleaseOrdersPreviousUsesWithoutGpuWait) {
  auto frame = MakeGpuFrame();
  gpu::SyncToken previous(gpu::CommandBufferNamespace::GPU_IO,
                         gpu::CommandBufferId::FromUnsafeValue(91), 3);
  gpu::SyncToken copy(gpu::CommandBufferNamespace::GPU_IO,
                     gpu::CommandBufferId::FromUnsafeValue(92), 7);
  std::vector<gpu::SyncToken> prior_dependencies;
  NvidiaVsrSourceRelease previous_client(previous, &prior_dependencies);
  EXPECT_EQ(frame->UpdateReleaseSyncToken(&previous_client), previous);
  EXPECT_TRUE(prior_dependencies.empty());
  std::vector<gpu::SyncToken> copy_dependencies;
  NvidiaVsrSourceRelease copy_client(copy, &copy_dependencies);
  EXPECT_EQ(frame->UpdateReleaseSyncToken(&copy_client), copy);
  ASSERT_EQ(copy_dependencies.size(), 1u);
  EXPECT_EQ(copy_dependencies.front(), previous);
  // Subsequent users must observe the copy token, never an inference token.
  gpu::SyncToken later(gpu::CommandBufferNamespace::GPU_IO,
                      gpu::CommandBufferId::FromUnsafeValue(93), 8);
  std::vector<gpu::SyncToken> later_dependencies;
  NvidiaVsrSourceRelease later_client(later, &later_dependencies);
  frame->UpdateReleaseSyncToken(&later_client);
  ASSERT_EQ(later_dependencies.size(), 1u);
  EXPECT_EQ(later_dependencies.front(), copy);
}
TEST(NvidiaVsrFrameClientTest, AdmitsOpaqueSdrSharedImage) {
  auto frame=MakeGpuFrame();
  EXPECT_EQ(ClassifyNvidiaVsrFrame(*frame,false),nvvfx_vsr::FrameBypass::None);
}
TEST(NvidiaVsrFrameClientTest, AdmitsOpaqueVaapiArgbSharedImage) {
  gpu::SharedImageMetadata metadata;
  metadata.format=viz::SinglePlaneFormat::kBGRA_8888;
  metadata.size=gfx::Size(1280,720);
  metadata.color_space=gfx::ColorSpace::CreateREC709();
  metadata.alpha_type=kOpaque_SkAlphaType;
  metadata.surface_origin=kTopLeft_GrSurfaceOrigin;
  metadata.usage=gpu::SHARED_IMAGE_USAGE_RASTER_READ;
  auto frame=VideoFrame::WrapSharedImage(PIXEL_FORMAT_ARGB,
      gpu::ClientSharedImage::CreateForTesting(metadata),{},base::DoNothing(),
      gfx::Rect(metadata.size),metadata.size,base::TimeDelta());
  frame->set_color_space(metadata.color_space);
  EXPECT_EQ(ClassifyNvidiaVsrFrame(*frame,false),nvvfx_vsr::FrameBypass::None);

  metadata.alpha_type=kPremul_SkAlphaType;
  auto translucent_frame=VideoFrame::WrapSharedImage(PIXEL_FORMAT_ARGB,
      gpu::ClientSharedImage::CreateForTesting(metadata),{},base::DoNothing(),
      gfx::Rect(metadata.size),metadata.size,base::TimeDelta());
  translucent_frame->set_color_space(metadata.color_space);
  EXPECT_EQ(ClassifyNvidiaVsrFrame(*translucent_frame,false),
            nvvfx_vsr::FrameBypass::Format);
}
TEST(NvidiaVsrFrameClientTest, ProtectionPrecedesAllImageAccess) {
  auto frame=VideoFrame::CreateBlackFrame(gfx::Size(1280,720));
  EXPECT_EQ(ClassifyNvidiaVsrFrame(*frame,true),nvvfx_vsr::FrameBypass::Protected);
  frame->metadata().protected_video=true;
  EXPECT_EQ(ClassifyNvidiaVsrFrame(*frame,false),nvvfx_vsr::FrameBypass::Protected);
  frame->metadata().protected_video=false;frame->metadata().hw_protected=true;
  EXPECT_EQ(ClassifyNvidiaVsrFrame(*frame,false),nvvfx_vsr::FrameBypass::Protected);
}
TEST(NvidiaVsrFrameClientTest, RejectsCpuAndHdr) {
  auto cpu=VideoFrame::CreateBlackFrame(gfx::Size(1280,720));
  EXPECT_EQ(ClassifyNvidiaVsrFrame(*cpu,false),nvvfx_vsr::FrameBypass::CpuFrame);
  auto frame=MakeGpuFrame(gfx::Size(1280,720),gfx::ColorSpace::CreateHDR10());
  EXPECT_EQ(ClassifyNvidiaVsrFrame(*frame,false),nvvfx_vsr::FrameBypass::Color);
}
TEST(NvidiaVsrFrameClientTest, RejectsTransformedFrame) {
  auto frame=MakeGpuFrame();
  frame->metadata().transformation=VideoTransformation(VIDEO_ROTATION_90);
  EXPECT_EQ(ClassifyNvidiaVsrFrame(*frame,false),nvvfx_vsr::FrameBypass::Geometry);
}
}  // namespace
}  // namespace media
