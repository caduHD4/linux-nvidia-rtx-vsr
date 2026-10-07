#include "nvvfx_vsr/presentation_cache.h"
#include <memory>
#include <stdexcept>
#include <iostream>
using Cache=nvvfx_vsr::PresentationCache<std::shared_ptr<int>>;
void Check(bool value,const char* text) {if(!value) throw std::runtime_error(text);}
int main(){try{
  auto original=std::make_shared<int>(1),enhanced=std::make_shared<int>(2);
  Cache cache(3);
  auto gen=cache.generation();
  Check(cache.Complete(gen,10,enhanced),"valid ready result rejected");
  Check(cache.Select(10,original)==enhanced,"ready result not selected");
  for(int refresh=0;refresh<180;++refresh)
    Check(cache.Select(10,original)==enhanced,"repeated refresh changed selection");
  Check(cache.Select(11,original)==original,"pending result delayed original");
  Check(!cache.Complete(gen,11,enhanced),"late result accepted after original selected");
  Check(cache.Select(11,original)==original,"late result changed first presentation");
  cache.Invalidate();
  Check(!cache.Complete(gen,10,enhanced),"seek accepted stale generation");
  Check(cache.Select(10,original)==original,"seek reused old enhanced result");
  Check(cache.Complete(cache.generation(),12,enhanced),"new frame ID rejected");
  Check(cache.Select(12,original)==enhanced,"distinct ID with repeated PTS not handled");
  for(int id=20;id<200;++id) cache.Complete(cache.generation(),id,enhanced);
  Check(cache.size()<=3,"capacity not bounded");
  Check(cache.Select(200,original)==original,"uncached frame failed fallback");
  cache.Complete(cache.generation(),201,enhanced);
  cache.Complete(cache.generation(),202,enhanced);
  cache.Complete(cache.generation(),203,enhanced);
  Check(!cache.Complete(cache.generation(),200,enhanced),"eviction lost last frame decision");
  Check(cache.Select(200,original)==original,"eviction changed repeated frame");
  Cache skipped(8);
  auto past=std::make_shared<int>(10),future=std::make_shared<int>(13);
  Check(skipped.Complete(skipped.generation(),90,past),"past completion rejected");
  Check(skipped.Complete(skipped.generation(),80,future),"future completion rejected");
  skipped.Select(100,std::make_shared<int>(12));
  Check(skipped.DiscardCompletedIf([](const auto& value){return *value<12;})==1,
        "skipped output not discarded");
  Check(past.use_count()==1,"skipped output still holds GPU lease");
  Check(!skipped.Complete(skipped.generation(),90,past),"discarded result accepted again");
  Check(skipped.Select(80,original)==future,"future output incorrectly discarded");
  for(int refresh=0;refresh<180;++refresh)
    Check(skipped.Select(80,original)==future,"pruning changed repeated presentation");
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
