#include "nvvfx_vsr/vsr_processor.h"
#include <cuda.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace nvvfx_vsr;
void Check(bool value, const std::string& message) {
  if (!value) throw std::runtime_error(message);
}
void Cuda(CUresult r) { Check(r == CUDA_SUCCESS, "CUDA test status " + std::to_string(r)); }
void Test(ProcessorConfig cfg, CUcontext context, const char* sdk, int iterations) {
  CUdeviceptr in=0,out=0;
  auto in_pitch=std::size_t(cfg.input.width)*4;
  auto out_pitch=std::size_t(cfg.output.width)*4;
  Cuda(cuMemAlloc(&in,in_pitch*cfg.input.height));
  Cuda(cuMemAlloc(&out,out_pitch*cfg.output.height));
  std::vector<unsigned char> source(in_pitch*cfg.input.height);
  for(int y=0;y<cfg.input.height;++y) for(int x=0;x<cfg.input.width;++x) {
    auto p=std::size_t(y)*in_pitch+x*4;
    source[p]=64; source[p+1]=128; source[p+2]=192; source[p+3]=255;
  }
  Cuda(cuMemcpyHtoD(in,source.data(),source.size()));
  std::string error;
  auto processor=VsrProcessor::Create(cfg,sdk,context,&error);
  Check(bool(processor),error);
  Check(!processor->Submit({in,1,cfg.input},{out,out_pitch,cfg.output},&error),
        "invalid pitch accepted");
  Check(processor->Submit({in,in_pitch,cfg.input},{out,out_pitch,cfg.output},&error),error);
  Check(processor->Drain(&error),error);
  Check(processor->LastGpuMilliseconds()>0,"first drained job timing missing");
  std::vector<double> timings;
  for(int i=0;i<iterations+3;++i) {
    Check(processor->Submit({in,in_pitch,cfg.input},{out,out_pitch,cfg.output},&error),error);
    Check(!processor->Submit({in,in_pitch,cfg.input},{out,out_pitch,cfg.output},&error),
          "second submission accepted before completion acknowledged");
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    ProcessState state;
    do {
      state=processor->Poll(&error);
      Check(state!=ProcessState::Failed,error);
      Check(std::chrono::steady_clock::now()<deadline,"GPU job timed out");
    } while(state==ProcessState::Pending);
    if(i>=3) timings.push_back(processor->LastGpuMilliseconds());
  }
  std::vector<unsigned char> result(out_pitch*cfg.output.height);
  Cuda(cuMemcpyDtoH(result.data(),out,result.size())); // Diagnostic-only readback.
  auto p=std::size_t(cfg.output.height/2)*out_pitch+cfg.output.width/2*4;
  Check(result[p]>20 && result[p]<100 && result[p+1]>90 && result[p+1]<165 &&
        result[p+2]>150 && result[p+2]<225,"channel/black output regression");
  Check(result[p+3]==255,"opaque alpha not preserved");
  Check(processor->Submit({in,in_pitch,cfg.input},{out,out_pitch,cfg.output},&error),error);
  Check(processor->Drain(&error),error);
  Check(processor->LastGpuMilliseconds()>0,"drained job timing missing");
  processor.reset();
  Cuda(cuMemFree(in)); Cuda(cuMemFree(out));
  std::sort(timings.begin(),timings.end());
  std::cout<<"mode="<<cfg.quality<<" frames="<<iterations<<" p50="
    <<timings[timings.size()/2]<<" p95="<<timings[(timings.size()-1)*95/100]
    <<" p99="<<timings[(timings.size()-1)*99/100]<<" ms PASS\n";
}
int main(int argc,char** argv) {
  try {
    Check(argc>=2,"SDK path required");
    std::string error;
    Check(!VsrProcessor::Create({{1280,720},{1920,1080},4,1},"/nonexistent",nullptr,&error),
          "null context unexpectedly accepted");
    Check(!VsrProcessor::PreloadRuntime("/nonexistent",&error),
          "missing preload runtime unexpectedly loaded");
    Check(VsrProcessor::PreloadRuntime(argv[1],&error),error);
    Cuda(cuInit(0)); CUdevice device; CUcontext context;
    Cuda(cuDeviceGet(&device,0)); Cuda(cuDevicePrimaryCtxRetain(&context,device));
    Cuda(cuCtxSetCurrent(context));
    Check(!VsrProcessor::Create({{1280,720},{1920,1080},4,1},"/nonexistent",context,&error),
          "missing runtime unexpectedly loaded");
    Check(error.find("cannot load")!=std::string::npos,"runtime failure diagnostic missing");
    int iterations=argc>2 ? std::atoi(argv[2]) : 30;
    Check(iterations>0,"iterations must be positive");
    Test({{1280,720},{1920,1080},4,1.0F},context,argv[1],iterations);
    Test({{1920,1080},{1920,1080},11,1.0F},context,argv[1],iterations);
    Cuda(cuDevicePrimaryCtxRelease(device));
  } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
