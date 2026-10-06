#include "nvvfx_vsr/frame_eligibility.h"
namespace nvvfx_vsr {
FrameBypass ClassifyFrame(const FrameEligibility& f,Dimensions target) {
  if(f.encrypted_track || f.protected_video || f.hw_protected)
    return FrameBypass::Protected;
  if(!f.gpu_image) return FrameBypass::CpuFrame;
  if(!f.opaque || f.bit_depth!=8 || !f.supported_format)
    return FrameBypass::Format;
  if(f.hdr || !f.valid_sdr_color) return FrameBypass::Color;
  if(f.transformed || !f.square_pixels || f.input.width<=0 || f.input.height<=0)
    return FrameBypass::Geometry;
  if(!SelectProcessorConfig(f.input,target)) return FrameBypass::AboveTarget;
  return FrameBypass::None;
}
}
