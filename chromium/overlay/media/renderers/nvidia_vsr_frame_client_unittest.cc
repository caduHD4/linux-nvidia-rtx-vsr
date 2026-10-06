#include "media/renderers/nvidia_vsr_frame_client.h"
#include "media/renderers/nvidia_vsr_source_release.h"
#include "base/functional/callback_helpers.h"
#include "gpu/command_buffer/client/client_shared_image.h"
#include "gpu/command_buffer/common/shared_image_info.h"
#include "media/base/video_frame.h"
#include "testing/gtest/include/gtest/gtest.h"
namespace media {
namespace {
scoped_refptr<VideoFrame> MakeGpuFrame() {
  gpu::SharedImageMetadata metadata;
  metadata.format=viz::SinglePlaneFormat::kRGBX_8888;
  metadata.size=gfx::Size(1280,720);
  metadata.color_space=gfx::ColorSpace::CreateREC709();
  metadata.alpha_type=kOpaque_SkAlphaType;
  auto frame=VideoFrame::WrapSharedImage(PIXEL_FORMAT_XBGR,
      gpu::ClientSharedImage::CreateForTesting(metadata),{},base::DoNothing(),
      gfx::Rect(metadata.size),metadata.size,base::TimeDelta());
  frame->set_color_space(metadata.color_space);
  return frame;
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
  auto frame=MakeGpuFrame();
  frame->set_color_space(gfx::ColorSpace::CreateHDR10());
  EXPECT_EQ(ClassifyNvidiaVsrFrame(*frame,false),nvvfx_vsr::FrameBypass::Color);
}
TEST(NvidiaVsrFrameClientTest, RejectsTransformedFrame) {
  auto frame=MakeGpuFrame();
  frame->metadata().transformation=VideoTransformation(VIDEO_ROTATION_90);
  EXPECT_EQ(ClassifyNvidiaVsrFrame(*frame,false),nvvfx_vsr::FrameBypass::Geometry);
}
}  // namespace
}  // namespace media
