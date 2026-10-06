#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
namespace nvvfx_vsr {
// Serialized by the GPU worker. Queue expiry/watchdog never prove that a
// texture is safe to reuse; only a physical completion acknowledgement does.
class JobAdmission {
 public:
  std::optional<std::size_t> Reserve(std::uint64_t generation,
      std::uint64_t frame_id,std::uint64_t now_ms);
  bool Start(std::size_t slot,std::uint64_t now_ms);
  bool CompleteSafely(std::size_t slot);
  void Quarantine(std::size_t slot);
  void CheckWatchdog(std::uint64_t now_ms);
  void Disable() { disabled_=true; }
  bool disabled() const { return disabled_; }
  std::size_t occupied() const;
 private:
  enum class State { Free, Queued, Running, Quarantined };
  struct Slot {
    State state=State::Free;
    std::uint64_t generation=0,id=0,queued_ms=0,started_ms=0;
  };
  std::array<Slot,3> slots_{};
  bool disabled_=false;
};
}
