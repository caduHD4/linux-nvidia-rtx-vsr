// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpu/ipc/service/gpu_channel_test_common.h"

#include <array>
#include <cmath>
#include <string>

#include "base/logging.h"
#include "base/run_loop.h"
#include "base/task/thread_pool/thread_pool_instance.h"
#include "base/test/bind.h"
#include "base/test/scoped_run_loop_timeout.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "gpu/command_buffer/common/shared_image_usage.h"
#include "gpu/command_buffer/service/shared_context_state.h"
#include "gpu/command_buffer/service/shared_image/shared_image_factory.h"
#include "gpu/command_buffer/service/shared_image/shared_image_representation.h"
#include "gpu/command_buffer/service/texture_manager.h"
#include "gpu/ipc/common/nvidia_vsr.mojom.h"
#include "gpu/ipc/service/gpu_channel.h"
#include "gpu/ipc/service/gpu_channel_manager.h"
#include "gpu/ipc/service/shared_image_stub.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "third_party/skia/include/gpu/ganesh/GrDirectContext.h"
#include "ui/gl/gl_bindings.h"
#include "ui/gl/gl_context.h"
#include "ui/gl/gl_implementation.h"

namespace gpu {
namespace {

// Disabled by default: needs native NVIDIA EGL and the external official SDK.
// Diagnostic pixel readback in this fixture is never used by playback.
class NvidiaVsrNativeIntegrationTest : public GpuChannelTestCommon {
 public:
  NvidiaVsrNativeIntegrationTest();
  ~NvidiaVsrNativeIntegrationTest() override;

 protected:
  void SetUp() override;
  void TearDown() override;
  void CheckMode(gfx::Size input_size, uint32_t quality);
  void CheckProtectedBypass();

 private:
  void FillInput(const Mailbox& mailbox, int frame = 0);
  std::array<unsigned char, 4> ReadOutput(const Mailbox& mailbox,
                                          int quadrant = 0);
  raw_ptr<GpuChannel> channel_ = nullptr;
  mojo::Remote<mojom::NvidiaVsr> remote_;
  uint64_t count_ = 0;
};

NvidiaVsrNativeIntegrationTest::NvidiaVsrNativeIntegrationTest()
    : GpuChannelTestCommon(std::vector<int32_t>{}, false, true) {}
NvidiaVsrNativeIntegrationTest::~NvidiaVsrNativeIntegrationTest() = default;

void NvidiaVsrNativeIntegrationTest::SetUp() {
  ASSERT_EQ(gl::GetGLImplementation(), gl::kGLImplementationEGLGLES2);
  channel_ = CreateChannel(101, false);
  ASSERT_TRUE(channel_);
  ASSERT_TRUE(channel_->shared_image_stub());
  bool available = false;
  channel_->CreateNvidiaVsr(
      201, 202, remote_.BindNewPipeAndPassReceiver(),
      base::BindLambdaForTesting([&](bool value) { available = value; }));
  ASSERT_TRUE(available);
}

void NvidiaVsrNativeIntegrationTest::TearDown() {
  remote_.reset();
  base::RunLoop().RunUntilIdle();
  base::ThreadPoolInstance::Get()->FlushForTesting();
  base::RunLoop().RunUntilIdle();
  channel_ = nullptr;
  channel_manager()->RemoveChannel(101);
  base::RunLoop().RunUntilIdle();
  base::ThreadPoolInstance::Get()->FlushForTesting();
  base::RunLoop().RunUntilIdle();
}

void NvidiaVsrNativeIntegrationTest::FillInput(const Mailbox& mailbox,
                                               int frame) {
  auto* stub = channel_->shared_image_stub();
  ASSERT_TRUE(stub->shared_context_state()->MakeCurrent(nullptr, true));
  SharedImageRepresentationFactory representations(
      channel_manager()->shared_image_manager(),
      scoped_refptr<MemoryTracker>(stub->memory_tracker()));
  auto rep = representations.ProduceGLTexture(mailbox, true);
  ASSERT_TRUE(rep);
  auto access = rep->BeginScopedAccess(
      GL_SHARED_IMAGE_ACCESS_MODE_READWRITE_CHROMIUM,
      SharedImageRepresentation::AllowUnclearedAccess::kYes);
  ASSERT_TRUE(access);
  GLuint framebuffer = 0;
  glGenFramebuffersEXT(1, &framebuffer);
  glBindFramebufferEXT(GL_FRAMEBUFFER, framebuffer);
  glFramebufferTexture2DEXT(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                            rep->GetTexture()->service_id(), 0);
  ASSERT_EQ(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER),
            static_cast<GLenum>(GL_FRAMEBUFFER_COMPLETE));
  glDisable(GL_SCISSOR_TEST);
  if (stub->shared_context_state()->real_context()->HasExtension(
          "GL_EXT_window_rectangles")) {
    glWindowRectanglesEXT(GL_EXCLUSIVE_EXT, 0, nullptr);
  }
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  const std::array<std::array<float, 3>, 3> colors = {
      {{0.25f, 0.5f, 0.75f}, {0.75f, 0.25f, 0.5f}, {0.5f, 0.75f, 0.25f}}};
  glEnable(GL_SCISSOR_TEST);
  for (int quadrant = 0; quadrant < 4; ++quadrant) {
    const auto& color = colors[(frame + quadrant) % 3];
    glScissor((quadrant % 2) * rep->size().width() / 2,
              (quadrant / 2) * rep->size().height() / 2,
              rep->size().width() / 2, rep->size().height() / 2);
    glClearColor(color[0], color[1], color[2], 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
  }
  glDisable(GL_SCISSOR_TEST);
  glBindFramebufferEXT(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffersEXT(1, &framebuffer);
  rep->SetCleared();
  glFlush();
  stub->shared_context_state()->gr_context()->resetContext();
}

std::array<unsigned char, 4> NvidiaVsrNativeIntegrationTest::ReadOutput(
    const Mailbox& mailbox,
    int quadrant) {
  auto* stub = channel_->shared_image_stub();
  CHECK(stub->shared_context_state()->MakeCurrent(nullptr, true));
  SharedImageRepresentationFactory representations(
      channel_manager()->shared_image_manager(),
      scoped_refptr<MemoryTracker>(stub->memory_tracker()));
  auto rep = representations.ProduceGLTexture(mailbox, true);
  CHECK(rep);
  auto access = rep->BeginScopedAccess(
      GL_SHARED_IMAGE_ACCESS_MODE_READ_CHROMIUM,
      SharedImageRepresentation::AllowUnclearedAccess::kNo);
  CHECK(access);
  GLuint framebuffer = 0;
  glGenFramebuffersEXT(1, &framebuffer);
  glBindFramebufferEXT(GL_FRAMEBUFFER, framebuffer);
  glFramebufferTexture2DEXT(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                            rep->GetTexture()->service_id(), 0);
  CHECK_EQ(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER),
           static_cast<GLenum>(GL_FRAMEBUFFER_COMPLETE));
  std::array<unsigned char, 4> pixel{};
  GLint pack_buffer = 0;
  glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &pack_buffer);
  glReadPixels((quadrant % 2 ? 3 : 1) * rep->size().width() / 4,
               (quadrant / 2 ? 3 : 1) * rep->size().height() / 4, 1, 1, GL_RGBA,
               GL_UNSIGNED_BYTE, pixel.data());
  const GLenum error = glGetError();
  LOG(INFO) << "Native fixture read " << rep->size().ToString()
            << " PBO=" << pack_buffer << " GLerror=" << error
            << " rgba=" << static_cast<int>(pixel[0]) << ","
            << static_cast<int>(pixel[1]) << "," << static_cast<int>(pixel[2])
            << "," << static_cast<int>(pixel[3]);
  EXPECT_EQ(error, static_cast<GLenum>(GL_NO_ERROR));
  glBindFramebufferEXT(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffersEXT(1, &framebuffer);
  stub->shared_context_state()->gr_context()->resetContext();
  return pixel;
}

void NvidiaVsrNativeIntegrationTest::CheckMode(gfx::Size input_size,
                                               uint32_t quality) {
  base::test::ScopedRunLoopTimeout timeout(FROM_HERE, base::Seconds(30));
  auto* factory = channel_->shared_image_stub()->factory();
  const auto input = Mailbox::Generate();
  const auto output = Mailbox::Generate();
  const auto color = gfx::ColorSpace::CreateSRGB();
  const SharedImageUsageSet usage = SHARED_IMAGE_USAGE_GLES2_READ |
                                    SHARED_IMAGE_USAGE_GLES2_WRITE |
                                    SHARED_IMAGE_USAGE_RASTER_READ;
  const gfx::Size coded_size(input_size.width(), input_size.height() + 16);
  ASSERT_TRUE(factory->CreateSharedImage(
      input,
      SharedImageInfo(viz::SinglePlaneFormat::kRGBA_8888, coded_size, color,
                      kTopLeft_GrSurfaceOrigin, kOpaque_SkAlphaType, usage,
                      "NativeVsrInput"),
      kNullSurfaceHandle));
  ASSERT_TRUE(factory->CreateSharedImage(
      output,
      SharedImageInfo(viz::SinglePlaneFormat::kRGBX_8888, gfx::Size(1920, 1080),
                      color, kTopLeft_GrSurfaceOrigin, kOpaque_SkAlphaType,
                      SHARED_IMAGE_USAGE_GLES2_WRITE |
                          SHARED_IMAGE_USAGE_RASTER_READ |
                          SHARED_IMAGE_USAGE_DISPLAY_READ,
                      "NativeVsrOutput"),
      kNullSurfaceHandle));
  FillInput(input);
  ASSERT_FALSE(HasFatalFailure());
  const auto source_pixel = ReadOutput(input);
  EXPECT_NEAR(source_pixel[0], 64, 1);
  EXPECT_NEAR(source_pixel[1], 128, 1);
  EXPECT_NEAR(source_pixel[2], 191, 1);
  EXPECT_EQ(source_pixel[3], 255);
  const std::array<std::array<int, 3>, 3> colors = {
      {{64, 127, 191}, {191, 64, 127}, {127, 191, 64}}};
  for (int frame = 0; frame < 3; ++frame) {
    FillInput(input, frame);
    ASSERT_FALSE(HasFatalFailure());
    bool success = false;
    std::string status;
    float gpu_ms = 0;
    // Other GPU work may alter the owner's GL state while CUDA is running.
    // Force clipping outside the output; Publish must restore an unclipped
    // blit.
    base::RepeatingTimer ambient_state;
    ambient_state.Start(
        FROM_HERE, base::Milliseconds(1), base::BindLambdaForTesting([&] {
          auto context = channel_->shared_image_stub()->shared_context_state();
          if (!context->MakeCurrent(nullptr, true))
            return;
          context->set_need_context_state_reset(true);
          glEnable(GL_SCISSOR_TEST);
          glScissor(3000, 3000, 1, 1);
          if (context->real_context()->HasExtension(
                  "GL_EXT_window_rectangles")) {
            const std::array<GLint, 4> outside = {3000, 3000, 1, 1};
            glWindowRectanglesEXT(GL_INCLUSIVE_EXT, 1, outside.data());
          }
          context->gr_context()->resetContext();
        }));
    // A cold model may age out the first request while warming the processor.
    for (int attempt = 0; attempt < 3 && !success; ++attempt) {
      auto request = mojom::NvidiaVsrFrameRequest::New();
      request->generation = 1;
      request->frame_id = ++count_;
      request->release_count = count_;
      request->input = input;
      request->output = output;
      request->visible_rect =
          gfx::Rect(0, 8, input_size.width(), input_size.height());
      request->output_size = gfx::Size(1920, 1080);
      request->rgb_color_space = color;
      request->quality = quality;
      request->strength = 1.0f;
      base::RunLoop loop;
      remote_->Process(
          std::move(request),
          base::BindLambdaForTesting([&](bool value, const std::string& result,
                                         const SyncToken& ready, float ms) {
            success = value;
            status = result;
            gpu_ms = ms;
            LOG(INFO) << "Native fixture mode=" << quality
                      << " count=" << count_ << " success=" << value
                      << " status=" << result << " gpu_ms=" << ms;
            EXPECT_TRUE(ready.HasData());
            EXPECT_TRUE(ready.verified_flush());
            loop.Quit();
          }));
      loop.Run();
      if (!success && status != "queue expired or enhancement disabled")
        break;
    }
    ambient_state.Stop();
    ASSERT_TRUE(success) << status;
    EXPECT_GT(gpu_ms, 0);
    for (int quadrant = 0; quadrant < 4; ++quadrant) {
      const auto pixel = ReadOutput(output, quadrant);
      const auto& expected = colors[(frame + quadrant) % 3];
      for (int channel = 0; channel < 3; ++channel)
        EXPECT_NEAR(pixel[channel], expected[channel], 30);
      EXPECT_EQ(pixel[3], 255);
    }
  }
  EXPECT_TRUE(factory->DestroySharedImage(input));
  EXPECT_TRUE(factory->DestroySharedImage(output));
}

TEST_F(NvidiaVsrNativeIntegrationTest, DISABLED_UltraUpscaleAndNativeDenoise) {
  CheckMode(gfx::Size(1280, 720), 4);
  ASSERT_FALSE(HasFatalFailure());
  CheckMode(gfx::Size(1920, 1080), 11);
}

void NvidiaVsrNativeIntegrationTest::CheckProtectedBypass() {
  base::test::ScopedRunLoopTimeout timeout(FROM_HERE, base::Seconds(5));
  for (int flag = 0; flag < 2; ++flag) {
    auto request = mojom::NvidiaVsrFrameRequest::New();
    request->generation = 1;
    request->frame_id = ++count_;
    request->release_count = count_;
    // Deliberately unregistered: reaching resource lookup changes the status.
    request->input = Mailbox::Generate();
    request->output = Mailbox::Generate();
    request->visible_rect = gfx::Rect(1280, 720);
    request->output_size = gfx::Size(1920, 1080);
    request->rgb_color_space = gfx::ColorSpace::CreateSRGB();
    request->quality = 4;
    request->strength = 1.0f;
    request->protected_video = flag == 0;
    request->encrypted_track = flag == 1;
    base::RunLoop loop;
    remote_->Process(
        std::move(request),
        base::BindLambdaForTesting([&](bool success, const std::string& status,
                                       const SyncToken& ready, float gpu_ms) {
          EXPECT_FALSE(success);
          EXPECT_EQ(status, "ineligible or busy");
          EXPECT_EQ(gpu_ms, 0);
          EXPECT_TRUE(ready.HasData());
          EXPECT_TRUE(ready.verified_flush());
          loop.Quit();
        }));
    loop.Run();
  }
}

TEST_F(NvidiaVsrNativeIntegrationTest,
       DISABLED_ProtectedRequestsBypassBeforeResourceImport) {
  CheckProtectedBypass();
}

}  // namespace
}  // namespace gpu
