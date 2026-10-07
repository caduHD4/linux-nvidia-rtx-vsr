#include "media/renderers/nvidia_vsr_presentation_cache.h"
#include <string>
#include <vector>

#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/task_environment.h"
#include "media/renderers/video_renderer_impl.h"
#include "media/base/mock_filters.h"
#include "media/base/test_helpers.h"
#include "testing/gtest/include/gtest/gtest.h"
namespace media {

class VideoRendererImplVsrSelectionTestPeer {
 public:
  static void Record(VideoRendererImpl& renderer,uint64_t id) {
    base::AutoLock lock(renderer.lock_);
    renderer.SyncNvidiaVsrFullscreen_Locked();
    renderer.nvidia_vsr_frame_generations_[id]=renderer.nvidia_vsr_cache_->generation();
  }
  static void FullscreenChanged(VideoRendererImpl& renderer) {
    renderer.OnNvidiaVsrFullscreenChanged();
  }
  static std::size_t QueuedMetadata(VideoRendererImpl& renderer) {
    base::AutoLock lock(renderer.lock_);
    return renderer.nvidia_vsr_frame_generations_.size();
  }
  static void CompleteLate(VideoRendererImpl& renderer,uint64_t generation,
                           scoped_refptr<VideoFrame> original,
                           scoped_refptr<VideoFrame> enhanced) {
    renderer.OnNvidiaVsrCompleted(generation,
        original->unique_id().GetUnsafeValue(),std::move(enhanced));
  }
  static void ChangeConfig(VideoRendererImpl& renderer,bool encrypted) {
    base::AutoLock lock(renderer.lock_);
    renderer.InvalidateNvidiaVsrConfig_Locked(encrypted);
  }
  static bool Candidate(VideoRendererImpl& renderer,uint64_t id) {
    base::AutoLock lock(renderer.lock_);
    return renderer.IsNvidiaVsrFrameCandidate_Locked(id);
  }
  static bool Complete(VideoRendererImpl& renderer,
                       const scoped_refptr<VideoFrame>& original,
                       scoped_refptr<VideoFrame> enhanced) {
    base::AutoLock lock(renderer.lock_);
    renderer.SyncNvidiaVsrFullscreen_Locked();
    return renderer.nvidia_vsr_cache_->Complete(
        renderer.nvidia_vsr_cache_->generation(),
        original->unique_id().GetUnsafeValue(), std::move(enhanced));
  }

  static scoped_refptr<VideoFrame> Select(
      VideoRendererImpl& renderer, scoped_refptr<VideoFrame> original) {
    base::AutoLock lock(renderer.lock_);
    return renderer.SelectNvidiaVsrFrame_Locked(std::move(original));
  }

  static void Invalidate(VideoRendererImpl& renderer) {
    base::AutoLock lock(renderer.lock_);
    renderer.nvidia_vsr_cache_->Invalidate();
    renderer.nvidia_vsr_presented_timestamp_.reset();
  }
};

namespace {

class VsrSelectionMediaLog : public MediaLog {
 public:
  ~VsrSelectionMediaLog() override { InvalidateLog(); }
  std::vector<std::string> selections;

 protected:
  void AddLogRecordLocked(std::unique_ptr<MediaLogRecord> record) override {
    const auto* message = record->params.FindString("info");
    if (message && message->starts_with("NVIDIA VSR selected_enhanced="))
      selections.push_back(*message);
  }
};

class FullscreenSelectionSink : public VideoRendererSink {
 public:
  uint64_t epoch=1;
  uint64_t NvidiaVsrFullscreenEpoch() const override { return epoch; }
  void Start(RenderCallback*) override {}
  void Stop() override {}
  void PaintNvidiaVsrOriginal(scoped_refptr<VideoFrame> frame,
                            uint64_t,uint64_t) override {
    PaintSingleFrame(std::move(frame),true);
  }
  scoped_refptr<VideoFrame> painted;
  bool repaint_duplicate=false;
  void PaintSingleFrame(scoped_refptr<VideoFrame> frame,bool duplicate) override {
    painted=std::move(frame);repaint_duplicate=duplicate;
  }
};

class NvidiaVsrSelectionLogTest : public testing::Test {
 protected:
  NvidiaVsrSelectionLogTest()
      : renderer_(task_environment_.GetMainThreadTaskRunner(), &sink_,
                  base::BindRepeating([] {
                    return std::vector<std::unique_ptr<VideoDecoder>>();
                  }),
                  true, &media_log_, nullptr, MediaPlayerLoggingID()) {}

  scoped_refptr<VideoFrame> SelectEnhanced(
      const scoped_refptr<VideoFrame>& original) {
    auto enhanced = VideoFrame::CreateBlackFrame(gfx::Size(32, 32));
    EXPECT_TRUE(VideoRendererImplVsrSelectionTestPeer::Complete(
        renderer_, original, enhanced));
    EXPECT_EQ(VideoRendererImplVsrSelectionTestPeer::Select(renderer_, original),
              enhanced);
    return enhanced;
  }

  std::string SelectionMessage(uint64_t count, uint64_t generation,
                               const scoped_refptr<VideoFrame>& original) {
    return "NVIDIA VSR selected_enhanced=" + base::NumberToString(count) +
           " generation=" + base::NumberToString(generation) + " frame_id=" +
           base::NumberToString(original->unique_id().GetUnsafeValue());
  }

  base::test::TaskEnvironment task_environment_;
  VsrSelectionMediaLog media_log_;
  FullscreenSelectionSink sink_;
  VideoRendererImpl renderer_;
};

TEST_F(NvidiaVsrSelectionLogTest, WindowedPlayerNeverSelectsEnhanced) {
  sink_.epoch=0;
  auto original=VideoFrame::CreateBlackFrame(gfx::Size(32,32));
  auto enhanced=VideoFrame::CreateBlackFrame(gfx::Size(32,32));
  ASSERT_TRUE(VideoRendererImplVsrSelectionTestPeer::Complete(renderer_,original,enhanced));
  EXPECT_EQ(VideoRendererImplVsrSelectionTestPeer::Select(renderer_,original),original);
  VideoRendererImplVsrSelectionTestPeer::Record(renderer_,42);
  EXPECT_FALSE(VideoRendererImplVsrSelectionTestPeer::Candidate(renderer_,42));
}

TEST_F(NvidiaVsrSelectionLogTest, ExitAndReenterNeverReuseFrozenEnhancedFrame) {
  auto original=VideoFrame::CreateBlackFrame(gfx::Size(32,32));
  SelectEnhanced(original);
  sink_.epoch=2;
  EXPECT_EQ(VideoRendererImplVsrSelectionTestPeer::Select(renderer_,original),original);
  sink_.epoch=3;
  EXPECT_EQ(VideoRendererImplVsrSelectionTestPeer::Select(renderer_,original),original);
}

TEST_F(NvidiaVsrSelectionLogTest, RapidExitReentryInvalidatesEvenWithoutWindowedRender) {
  auto original=VideoFrame::CreateBlackFrame(gfx::Size(32,32));
  SelectEnhanced(original);
  sink_.epoch=3; // Exit and reentry happened between renderer callbacks.
  EXPECT_EQ(VideoRendererImplVsrSelectionTestPeer::Select(renderer_,original),original);
}

TEST_F(NvidiaVsrSelectionLogTest, ExitDropsQueuedMetadata) {
  VideoRendererImplVsrSelectionTestPeer::Record(renderer_,42);
  ASSERT_EQ(VideoRendererImplVsrSelectionTestPeer::QueuedMetadata(renderer_),1u);
  sink_.epoch=2;
  VideoRendererImplVsrSelectionTestPeer::Select(renderer_,
      VideoFrame::CreateBlackFrame(gfx::Size(32,32)));
  EXPECT_EQ(VideoRendererImplVsrSelectionTestPeer::QueuedMetadata(renderer_),0u);
}

TEST_F(NvidiaVsrSelectionLogTest, LateCompletionCannotCrossExitReentry) {
  SelectEnhanced(VideoFrame::CreateBlackFrame(gfx::Size(32,32)));
  auto future=VideoFrame::CreateBlackFrame(gfx::Size(32,32));
  auto enhanced=VideoFrame::CreateBlackFrame(gfx::Size(32,32));
  sink_.epoch=3;
  VideoRendererImplVsrSelectionTestPeer::CompleteLate(renderer_,1,future,enhanced);
  EXPECT_EQ(VideoRendererImplVsrSelectionTestPeer::Select(renderer_,future),future);
}

TEST_F(NvidiaVsrSelectionLogTest, PausedExitRepaintsOriginalWithoutRender) {
  auto original=VideoFrame::CreateBlackFrame(gfx::Size(32,32));
  SelectEnhanced(original);
  sink_.epoch=2;
  VideoRendererImplVsrSelectionTestPeer::FullscreenChanged(renderer_);
  EXPECT_EQ(sink_.painted,original);
  EXPECT_TRUE(sink_.repaint_duplicate);
}

TEST_F(NvidiaVsrSelectionLogTest, PausedRapidExitReentryRepaintsOriginalWithoutRender) {
  auto original=VideoFrame::CreateBlackFrame(gfx::Size(32,32));
  SelectEnhanced(original);
  sink_.epoch=3;
  VideoRendererImplVsrSelectionTestPeer::FullscreenChanged(renderer_);
  EXPECT_EQ(sink_.painted,original);
}

TEST_F(NvidiaVsrSelectionLogTest, InitiallyEncryptedRendererKeepsBypass) {
  testing::NiceMock<MockDemuxerStream> stream(DemuxerStream::VIDEO);
  testing::NiceMock<MockRendererClient> client;
  stream.set_video_decoder_config(TestVideoConfig::LargeEncrypted());
  renderer_.Initialize(&stream,nullptr,&client,base::BindRepeating(
      [](const std::vector<base::TimeDelta>&,std::vector<base::TimeTicks>*) {
        return false;
      }),base::BindOnce([](PipelineStatus){}));
  task_environment_.RunUntilIdle();
  VideoRendererImplVsrSelectionTestPeer::Record(renderer_,42);
  EXPECT_FALSE(VideoRendererImplVsrSelectionTestPeer::Candidate(renderer_,42));
}

TEST_F(NvidiaVsrSelectionLogTest, QueuedFrameKeepsItsEnqueueGeneration) {
  VideoRendererImplVsrSelectionTestPeer::Record(renderer_,42);
  EXPECT_TRUE(VideoRendererImplVsrSelectionTestPeer::Candidate(renderer_,42));
  VideoRendererImplVsrSelectionTestPeer::ChangeConfig(renderer_,false);
  EXPECT_FALSE(VideoRendererImplVsrSelectionTestPeer::Candidate(renderer_,42));
  VideoRendererImplVsrSelectionTestPeer::Record(renderer_,43);
  EXPECT_TRUE(VideoRendererImplVsrSelectionTestPeer::Candidate(renderer_,43));
}
TEST_F(NvidiaVsrSelectionLogTest, EncryptedToClearDrainKeepsConservativeBypass) {
  VideoRendererImplVsrSelectionTestPeer::ChangeConfig(renderer_,true);
  VideoRendererImplVsrSelectionTestPeer::ChangeConfig(renderer_,false);
  VideoRendererImplVsrSelectionTestPeer::Record(renderer_,42);
  EXPECT_FALSE(VideoRendererImplVsrSelectionTestPeer::Candidate(renderer_,42));
}

TEST_F(NvidiaVsrSelectionLogTest, CountsUniqueSelectionsAcrossRefreshesSparsely) {
  for (uint64_t count = 1; count <= 240; ++count) {
    auto original = VideoFrame::CreateBlackFrame(gfx::Size(16, 16));
    auto enhanced = SelectEnhanced(original);
    // PaintFirstFrame and Render share the selection helper. Display refreshes
    // of the same choice must not inflate its cumulative selection count.
    for (int refresh = 0; refresh < 180; ++refresh) {
      EXPECT_EQ(VideoRendererImplVsrSelectionTestPeer::Select(renderer_, original),
                enhanced);
    }
    const size_t expected_logs = 1 + count / 120;
    ASSERT_EQ(media_log_.selections.size(), expected_logs);
    if (count == 1 || count % 120 == 0)
      EXPECT_EQ(media_log_.selections.back(), SelectionMessage(count, 1, original));
  }
}

TEST_F(NvidiaVsrSelectionLogTest, CountsOnlySelectionsAndDistinguishesGeneration) {
  auto original = VideoFrame::CreateBlackFrame(gfx::Size(16, 16));
  auto enhanced = VideoFrame::CreateBlackFrame(gfx::Size(32, 32));
  EXPECT_TRUE(VideoRendererImplVsrSelectionTestPeer::Complete(
      renderer_, original, enhanced));
  EXPECT_TRUE(media_log_.selections.empty());  // Completion is not selection.
  EXPECT_EQ(VideoRendererImplVsrSelectionTestPeer::Select(renderer_, original),
            enhanced);
  ASSERT_EQ(media_log_.selections.size(), 1u);
  EXPECT_EQ(media_log_.selections[0], SelectionMessage(1, 1, original));

  auto fallback = VideoFrame::CreateBlackFrame(gfx::Size(16, 16));
  for (int refresh = 0; refresh < 180; ++refresh)
    EXPECT_EQ(VideoRendererImplVsrSelectionTestPeer::Select(renderer_, fallback),
              fallback);
  EXPECT_TRUE(enhanced->HasOneRef());  // Accounting retains no frame or lease.
  scoped_refptr<VideoFrame> last_original;
  for (uint64_t count = 2; count <= 119; ++count) {
    last_original = VideoFrame::CreateBlackFrame(gfx::Size(16, 16));
    SelectEnhanced(last_original);
  }
  EXPECT_EQ(media_log_.selections.size(), 1u);

  VideoRendererImplVsrSelectionTestPeer::Invalidate(renderer_);
  SelectEnhanced(last_original);  // Same ID is a new choice in the new generation.
  ASSERT_EQ(media_log_.selections.size(), 2u);
  EXPECT_EQ(media_log_.selections.back(), SelectionMessage(120, 2, last_original));
}

TEST(NvidiaVsrPresentationCacheTest, ReadyAndPendingChoicesStayFixedAt180Hz) {
  auto original=VideoFrame::CreateBlackFrame(gfx::Size(16,16));
  auto enhanced=VideoFrame::CreateBlackFrame(gfx::Size(32,32));
  NvidiaVsrPresentationCache cache;
  const auto id=original->unique_id().GetUnsafeValue();
  EXPECT_TRUE(cache.Complete(cache.generation(),id,enhanced));
  for(int i=0;i<180;++i) EXPECT_EQ(cache.Select(id,original),enhanced);
  auto next=VideoFrame::CreateBlackFrame(gfx::Size(16,16));
  const auto next_id=next->unique_id().GetUnsafeValue();
  EXPECT_EQ(cache.Select(next_id,next),next);
  EXPECT_FALSE(cache.Complete(cache.generation(),next_id,enhanced));
  for(int i=0;i<180;++i) EXPECT_EQ(cache.Select(next_id,next),next);
}
TEST(NvidiaVsrPresentationCacheTest, SeekRejectsOldGenerationDespiteRepeatedPts) {
  auto original=VideoFrame::CreateBlackFrame(gfx::Size(16,16));
  auto enhanced=VideoFrame::CreateBlackFrame(gfx::Size(32,32));
  NvidiaVsrPresentationCache cache;
  const auto generation=cache.generation();
  cache.Invalidate();
  EXPECT_FALSE(cache.Complete(generation,original->unique_id().GetUnsafeValue(),enhanced));
  EXPECT_EQ(cache.Select(original->unique_id().GetUnsafeValue(),original),original);
  auto next=VideoFrame::CreateBlackFrame(gfx::Size(16,16));
  EXPECT_EQ(original->timestamp(),next->timestamp());
  EXPECT_TRUE(cache.Complete(cache.generation(),next->unique_id().GetUnsafeValue(),enhanced));
  EXPECT_EQ(cache.Select(next->unique_id().GetUnsafeValue(),next),enhanced);
}
TEST(NvidiaVsrPresentationCacheTest, ReleasesPreviousOutputWhenNextFrameSelected) {
  auto original=VideoFrame::CreateBlackFrame(gfx::Size(16,16));
  auto enhanced=VideoFrame::CreateBlackFrame(gfx::Size(32,32));
  NvidiaVsrPresentationCache cache(3);
  cache.Complete(cache.generation(),1,enhanced);
  cache.Select(1,original);
  EXPECT_FALSE(enhanced->HasOneRef());
  cache.Select(2,original);
  EXPECT_TRUE(enhanced->HasOneRef());
  for(uint64_t id=3;id<100;++id) cache.Complete(cache.generation(),id,enhanced);
  EXPECT_LE(cache.size(),3u);
}
TEST(NvidiaVsrPresentationCacheTest, SkippedOutputsReleaseLeasesAndKeepFuture) {
  auto original=VideoFrame::CreateBlackFrame(gfx::Size(16,16));
  auto past=VideoFrame::CreateBlackFrame(gfx::Size(32,32));
  auto future=VideoFrame::CreateBlackFrame(gfx::Size(32,32));
  past->set_timestamp(base::Milliseconds(10));
  original->set_timestamp(base::Milliseconds(20));
  future->set_timestamp(base::Milliseconds(30));
  NvidiaVsrPresentationCache cache;
  cache.Complete(cache.generation(),90,past);
  cache.Complete(cache.generation(),80,future);
  cache.Select(100,original);
  EXPECT_EQ(cache.DiscardCompletedIf([&](const auto& ready) {
    return ready->timestamp()<original->timestamp();
  }),1u);
  EXPECT_TRUE(past->HasOneRef());
  EXPECT_FALSE(cache.Complete(cache.generation(),90,past));
  EXPECT_EQ(cache.Select(80,original),future);
  for(int refresh=0;refresh<180;++refresh)
    EXPECT_EQ(cache.Select(80,original),future);
}
}  // namespace
}  // namespace media
