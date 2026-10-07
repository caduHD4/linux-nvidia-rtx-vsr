#include "nvvfx_vsr/job_admission.h"
namespace nvvfx_vsr {
std::size_t JobAdmission::occupied() const {
  std::size_t count=0;
  for(const auto& slot:slots_) if(slot.state!=State::Free) ++count;
  return count;
}
std::optional<std::size_t> JobAdmission::Reserve(std::uint64_t generation,
    std::uint64_t id,std::uint64_t now_ms) {
  if(disabled_ || occupied()>=2) return std::nullopt;
  for(const auto& slot:slots_)
    if(slot.state!=State::Free && slot.generation==generation && slot.id==id)
      return std::nullopt;
  for(std::size_t i=0;i<slots_.size();++i) if(slots_[i].state==State::Free) {
    slots_[i]={State::Queued,generation,id,now_ms,0};return i;
  }
  return std::nullopt;
}
bool JobAdmission::Start(std::size_t slot,std::uint64_t now_ms) {
  if(disabled_ || slot>=slots_.size() || slots_[slot].state!=State::Queued ||
     now_ms<slots_[slot].queued_ms || now_ms-slots_[slot].queued_ms>50)
    return false;
  for(const auto& other:slots_) if(other.state==State::Running) return false;
  slots_[slot].state=State::Running;slots_[slot].started_ms=now_ms;return true;
}
bool JobAdmission::FinishInference(std::size_t slot) {
  if(slot>=slots_.size() || slots_[slot].state!=State::Running) return false;
  slots_[slot].state=State::Publishing;return true;
}
bool JobAdmission::CompleteSafely(std::size_t slot) {
  if(slot>=slots_.size() || slots_[slot].state==State::Free ||
     slots_[slot].state==State::Quarantined) return false;
  slots_[slot]=Slot{};return true;
}
void JobAdmission::Quarantine(std::size_t slot) {
  disabled_=true;
  if(slot<slots_.size() && slots_[slot].state!=State::Free)
    slots_[slot].state=State::Quarantined;
}
void JobAdmission::CheckWatchdog(std::uint64_t now_ms) {
  for(const auto& slot:slots_) if(slot.state==State::Running &&
      now_ms>=slot.started_ms && now_ms-slot.started_ms>100) disabled_=true;
}
}
