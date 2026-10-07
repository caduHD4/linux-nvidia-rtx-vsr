// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "media/base/video_renderer_sink.h"
namespace media {
uint64_t VideoRendererSink::NvidiaVsrFullscreenEpoch() const { return 0; }
void VideoRendererSink::SetNvidiaVsrFullscreenCallback(base::RepeatingClosure) {}
void VideoRendererSink::PaintNvidiaVsrOriginal(scoped_refptr<VideoFrame>,
                                             uint64_t,uint64_t) {}
}  // namespace media
