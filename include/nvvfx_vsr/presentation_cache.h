#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <utility>
namespace nvvfx_vsr {
// Calls must be serialized by the owner's lock. Payload owns its resources.
template<class Payload> class PresentationCache {
 public:
  explicit PresentationCache(std::size_t capacity=24);
  std::uint64_t generation() const;
  void Invalidate();
  bool Complete(std::uint64_t generation,std::uint64_t id,Payload enhanced);
  Payload Select(std::uint64_t id,Payload original);
  std::size_t size() const;
 private:
  struct Entry {Payload enhanced{}; bool decided=false;};
  std::uint64_t generation_=1,last_id_=0;
  bool have_last_=false;
  Payload last_choice_{};
  std::size_t capacity_;
  std::map<std::uint64_t,Entry> entries_;
  std::deque<std::uint64_t> order_;
  Entry* Ensure(std::uint64_t id);
};
}

namespace nvvfx_vsr {
template<class P> PresentationCache<P>::PresentationCache(std::size_t capacity)
    :capacity_(capacity ? capacity : 1) {}
template<class P> std::uint64_t PresentationCache<P>::generation() const { return generation_; }
template<class P> void PresentationCache<P>::Invalidate() {
  ++generation_; entries_.clear();order_.clear();last_choice_=P{};have_last_=false;
}
template<class P> typename PresentationCache<P>::Entry* PresentationCache<P>::Ensure(std::uint64_t id) {
  auto found=entries_.find(id);
  if(found!=entries_.end()) return &found->second;
  if(entries_.size()>=capacity_) {entries_.erase(order_.front());order_.pop_front();}
  order_.push_back(id);return &entries_.emplace(id,Entry{}).first->second;
}
template<class P> bool PresentationCache<P>::Complete(std::uint64_t generation,
    std::uint64_t id,P enhanced) {
  if(generation!=generation_ || !enhanced || (have_last_ && id==last_id_)) return false;
  auto* entry=Ensure(id);
  if(entry->decided || entry->enhanced) return false;
  entry->enhanced=std::move(enhanced);return true;
}
template<class P> P PresentationCache<P>::Select(std::uint64_t id,P original) {
  if(have_last_ && last_id_==id) return last_choice_;
  // Only the current displayed frame needs a frozen payload. Retaining old
  // outputs would starve the three-slot GPU pool; keep just decision markers.
  if(have_last_) {
    auto old=entries_.find(last_id_);
    if(old!=entries_.end()) old->second.enhanced=P{};
  }
  auto* entry=Ensure(id);
  P selected=entry->enhanced ? entry->enhanced : std::move(original);
  entry->decided=true;
  last_id_=id;have_last_=true;last_choice_=selected;
  return selected;
}
template<class P> std::size_t PresentationCache<P>::size() const { return entries_.size(); }
}
