#pragma once
#include "nvvfx_vsr/vsr_processor.h"
namespace nvvfx_vsr {
// Worker-only bridge. Caller owns textures and context, and must establish the
// input GL fence before Run. No Chromium SharedImage is accessed by this class.
class GlVsrBridge {
 public:
  static std::unique_ptr<GlVsrBridge> Create(ProcessorConfig config,
      CUctx_st* context,unsigned input_texture,unsigned output_texture,std::string* error);
  ~GlVsrBridge();
  GlVsrBridge(const GlVsrBridge&)=delete;
  GlVsrBridge& operator=(const GlVsrBridge&)=delete;
  bool Run(VsrProcessor& processor,std::string* error);
  // False means caller must retain textures and CUDA/GL contexts.
  bool Close(std::string* error);
 private:
  struct Impl;
  explicit GlVsrBridge(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};
}
