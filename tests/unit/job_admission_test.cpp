#include "nvvfx_vsr/job_admission.h"
#include <iostream>
#include <stdexcept>
using namespace nvvfx_vsr;
void Check(bool v,const char* text){if(!v)throw std::runtime_error(text);}
int main(){try{
  JobAdmission jobs;
  auto a=jobs.Reserve(1,10,0),b=jobs.Reserve(1,11,1);
  Check(a && b,"two-job limit rejected valid jobs");
  Check(!jobs.Reserve(1,12,2),"unbounded queue accepted third job");
  Check(!jobs.Reserve(1,10,2),"duplicate frame admitted");
  Check(jobs.Start(*a,2),"first job did not start");
  Check(!jobs.Start(*b,3),"two kernels admitted concurrently");
  jobs.CheckWatchdog(103);
  Check(jobs.disabled() && jobs.occupied()==2,"watchdog released GPU buffers");
  Check(!jobs.Reserve(2,13,103),"watchdog accepted new work");
  Check(jobs.CompleteSafely(*a),"drained job not released");
  Check(!jobs.Start(*b,104),"expired queued job started");
  Check(jobs.occupied()==1,"expired queue freed memory before safe completion");
  Check(jobs.CompleteSafely(*b) && jobs.occupied()==0,"safe bypass not released");
  JobAdmission queue;
  auto c=queue.Reserve(4,99,10);
  Check(c.has_value(),"fresh queue unavailable");
  Check(!queue.Start(*c,61),"51ms queue deadline ignored");
  Check(queue.occupied()==1,"late staging released before acknowledgement");
  queue.Quarantine(*c);
  Check(queue.disabled() && !queue.CompleteSafely(*c),"quarantine reused unsafe slot");
  Check(queue.occupied()==1,"quarantined resource dropped");
  JobAdmission normal;
  auto d=normal.Reserve(1,1,0);Check(normal.Start(*d,0),"normal start failed");
  normal.CheckWatchdog(100);Check(!normal.disabled(),"watchdog fired before >100ms");
  Check(normal.CompleteSafely(*d),"normal completion failed");
  auto e=normal.Reserve(2,1,101);Check(e.has_value(),"new generation did not allow repeated frame ID");
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
