#pragma once
#include "nvvfx_vsr/processor_config.h"
namespace nvvfx_vsr {
enum class FrameBypass { None, Protected, CpuFrame, Format, Color, Geometry, AboveTarget };
// Adapters must explicitly establish every supported property. Protection is
// checked before any image import or access; this describes metadata only.
struct FrameEligibility {
  bool encrypted_track=false;
  bool protected_video=false;
  bool hw_protected=false;
  bool gpu_image=false;
  bool opaque=false;
  unsigned bit_depth=0;
  bool supported_format=false;
  bool valid_sdr_color=false;
  bool hdr=false;
  bool transformed=false;
  bool square_pixels=false;
  Dimensions input{};
};
FrameBypass ClassifyFrame(const FrameEligibility& frame,
                          Dimensions target={1920,1080});
}
