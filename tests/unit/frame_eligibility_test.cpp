#include "nvvfx_vsr/frame_eligibility.h"
#include <iostream>
#include <stdexcept>
using namespace nvvfx_vsr;
void Check(bool value,const char* text) { if(!value) throw std::runtime_error(text); }
int main() { try {
  FrameEligibility frame;
  Check(ClassifyFrame(frame)!=FrameBypass::None,"unknown frame accepted");
  frame.gpu_image=true;frame.opaque=true;frame.bit_depth=8;
  frame.supported_format=true;frame.valid_sdr_color=true;frame.square_pixels=true;
  frame.input={1280,720};
  Check(ClassifyFrame(frame)==FrameBypass::None,"eligible SDR frame rejected");
  frame.encrypted_track=true;frame.gpu_image=false;
  Check(ClassifyFrame(frame)==FrameBypass::Protected,"encrypted track bypass priority lost");
  frame.encrypted_track=false;frame.protected_video=true;
  Check(ClassifyFrame(frame)==FrameBypass::Protected,"protected frame accepted");
  frame.protected_video=false;frame.hw_protected=true;
  Check(ClassifyFrame(frame)==FrameBypass::Protected,"hardware protected frame accepted");
  frame.hw_protected=false;
  Check(ClassifyFrame(frame)==FrameBypass::CpuFrame,"CPU frame accepted");
  frame.gpu_image=true;frame.hdr=true;
  Check(ClassifyFrame(frame)==FrameBypass::Color,"HDR frame accepted");
  frame.hdr=false;frame.bit_depth=10;
  Check(ClassifyFrame(frame)==FrameBypass::Format,"10-bit frame accepted");
  frame.bit_depth=8;frame.transformed=true;
  Check(ClassifyFrame(frame)==FrameBypass::Geometry,"transformed frame accepted");
  frame.transformed=false;frame.square_pixels=false;
  Check(ClassifyFrame(frame)==FrameBypass::Geometry,"non-square pixels accepted");
  frame.square_pixels=true;frame.input={3840,2160};
  Check(ClassifyFrame(frame)==FrameBypass::AboveTarget,"4K frame accepted");
  Check(ClassifyFrame(frame,{4096,2160})==FrameBypass::None,"eligible 4K SDR frame rejected");
  frame.encrypted_track=true;
  Check(ClassifyFrame(frame,{4096,2160})==FrameBypass::Protected,"4K protected gate lost");
  frame.encrypted_track=false;frame.hdr=true;
  Check(ClassifyFrame(frame,{4096,2160})==FrameBypass::Color,"4K HDR gate lost");
  frame.hdr=false;frame.input={7680,4320};
  Check(ClassifyFrame(frame,{4096,2160})==FrameBypass::AboveTarget,"8K resource cap lost");
  frame.input={1920,1080};frame.opaque=false;
  Check(ClassifyFrame(frame)==FrameBypass::Format,"alpha frame accepted");
  frame.opaque=true;frame.valid_sdr_color=false;
  Check(ClassifyFrame(frame)==FrameBypass::Color,"unknown colors accepted");
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;} }
