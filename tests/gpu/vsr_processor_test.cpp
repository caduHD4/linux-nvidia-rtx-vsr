#include "nvvfx_vsr/vsr_processor.h"
#include <cuda.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cmath>
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
  const auto center=std::size_t(cfg.output.height/2)*out_pitch+cfg.output.width/2*4;
  auto CheckCurrentFrame=[&](const unsigned char* expected) {
    unsigned char pixel[4]{};
    Cuda(cuMemcpyDtoH(pixel,out+center,sizeof(pixel))); // Diagnostic-only.
    for(int channel=0;channel<3;++channel)
      Check(std::abs(int(pixel[channel])-int(expected[channel]))<=30,
            "processor returned black or a previous frame");
    Check(pixel[3]==255,"processor current-frame alpha invalid");
  };
  const unsigned char colors[3][3]={{192,64,128},{128,192,64},{64,128,192}};
  CheckCurrentFrame(colors[2]);
  std::vector<double> timings;
  for(int i=0;i<iterations+3;++i) {
    if(i<3) {
      for(std::size_t pixel=0;pixel<source.size();pixel+=4)
        for(int channel=0;channel<3;++channel) source[pixel+channel]=colors[i][channel];
      Cuda(cuMemcpyHtoD(in,source.data(),source.size()));
    }
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
    if(i<3) CheckCurrentFrame(colors[i]);
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
  Check(processor->Close(&error),error);
  Check(processor->Close(&error),"processor Close not idempotent");
  processor.reset();
  Cuda(cuMemFree(in)); Cuda(cuMemFree(out));
  std::sort(timings.begin(),timings.end());
  std::cout<<"sharpness="<<(std::getenv("NVVFX_VSR_SHARPNESS") ? std::getenv("NVVFX_VSR_SHARPNESS") : "0")<<" mode="<<cfg.quality<<" frames="<<iterations<<" p50="
    <<timings[timings.size()/2]<<" p95="<<timings[(timings.size()-1)*95/100]
    <<" p99="<<timings[(timings.size()-1)*99/100]<<" ms PASS\n";
}
// Diagnostic-only readback: compare identical inputs with/without post-sharpen.
void TestSharpen(CUcontext context, const char* sdk) {
  ProcessorConfig cfg{{640,360},{640,360},11,1.0F};
  const std::size_t pitch=cfg.input.width*4,bytes=pitch*cfg.input.height;
  std::vector<unsigned char> input(bytes);
  for(int y=0;y<cfg.input.height;++y) for(int x=0;x<cfg.input.width;++x) {
    auto at=y*pitch+x*4;
    auto v=static_cast<unsigned char>(128+48*std::sin(x*0.4));
    input[at]=input[at+1]=input[at+2]=v;input[at+3]=255;
  }
  CUdeviceptr in=0,out=0;Cuda(cuMemAlloc(&in,bytes));Cuda(cuMemAlloc(&out,bytes));
  Cuda(cuMemcpyHtoD(in,input.data(),bytes));
  std::string error;
  auto run=[&](const char* sharpness) {
    setenv("NVVFX_VSR_SHARPNESS",sharpness,1);
    auto p=VsrProcessor::Create(cfg,sdk,context,&error);Check(bool(p),error);
    for(int i=0;i<4;++i) {
      Check(p->Submit({in,pitch,cfg.input},{out,pitch,cfg.output},&error),error);
      Check(p->Drain(&error),error);
    }
    std::vector<unsigned char> pixels(bytes);
    Cuda(cuMemcpyDtoH(pixels.data(),out,bytes));
    std::cout<<"sharpness="<<sharpness<<" gpu_ms="<<p->LastGpuMilliseconds()<<'\n';
    Check(p->Close(&error),error);return pixels;
  };
  const auto baseline=run("0"),enhanced=run("0.35");
  double before=0,after=0;int changed=0;
  for(int y=16;y<cfg.output.height-16;++y) for(int x=16;x<cfg.output.width-16;++x) {
    auto at=y*pitch+x*4;
    before+=std::abs(int(baseline[at])-int(baseline[at-4]));
    after+=std::abs(int(enhanced[at])-int(enhanced[at-4]));
    changed+=baseline[at]!=enhanced[at];
    Check(enhanced[at+3]==255,"sharpen lost opaque alpha");
    for(int c=0;c<3;++c) Check(std::abs(int(enhanced[at+c])-int(baseline[at+c]))<=32,
                              "mild sharpen changed tones excessively");
  }
  Check(changed>1000 && after>before*1.01,"post-sharpen did not increase local contrast");
  const auto invalid=run("nan");
  Check(invalid==baseline,"invalid sharpening must fall back to disabled");
  unsetenv("NVVFX_VSR_SHARPNESS");Cuda(cuMemFree(in));Cuda(cuMemFree(out));
  std::cout<<"sharpen contrast="<<before<<" -> "<<after<<" PASS\n";
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
    setenv("NVVFX_VSR_SHARPNESS","0.35",1);
    Test({{1280,720},{1920,1080},4,1.0F},context,argv[1],iterations);
    Test({{1920,1080},{1920,1080},11,1.0F},context,argv[1],iterations);
    Test({{1920,1080},{2560,1440},4,1.0F},context,argv[1],iterations);
    Test({{1280,720},{2560,1440},4,1.0F},context,argv[1],iterations);
    TestSharpen(context,argv[1]);
    Cuda(cuDevicePrimaryCtxRelease(device));
  } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
