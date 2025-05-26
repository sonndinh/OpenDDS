#include "DrainManager.h"

#include <dds/DCPS/TimeTypes.h>
#include <ace/OS_NS_time.h>

namespace RtpsRelay {

DrainManager::DrainManager(const DrainConfig& config, const std::string& relay_id)
  : relay_id_(relay_id)
  , drain_rate_per_second_(config.drain_rate_per_second)
  , drain_check_interval_ms_(config.drain_check_interval_ms)
{}

void DrainManager::set_state(DrainState state)
{
  if (state_ == state) {
    return; // No change
  }
  
  if (state == DRAINING && state_ == ACTIVE) {
    // Starting to drain, record the start time
    drain_start_time_ = std::chrono::steady_clock::now();
  }
  
  state_ = state;
}

void DrainManager::set_drain_rate(unsigned rate)
{
  drain_rate_per_second_ = rate;
}

bool DrainManager::is_participant_removed(const OpenDDS::DCPS::GUID_t& guid) const
{
  return removed_participants_.find(guid) != removed_participants_.end();
}

void DrainManager::process_drain_cycle(GuidAddrSet& guid_addr_set)
{
  if (state_ != DRAINING) {
    return;
  }
  
  // Get the current count of participants
  if (total_participants_ == 0) {
    total_participants_ = guid_addr_set.get_participant_count();
    remaining_participants_ = total_participants_;
  }
  
  // Calculate how many participants to remove in this cycle
  unsigned to_remove = drain_rate_per_second_;
  if (to_remove > remaining_participants_) {
    to_remove = remaining_participants_;
  }
  
  if (to_remove == 0) {
    // All participants have been removed
    set_state(DRAINED);
    return;
  }
  
  // Remove participants
  std::vector<OpenDDS::DCPS::GUID_t> removed;
  guid_addr_set.remove_next_batch(to_remove, removed);
  
  // Add to the removed set
  for (const auto& guid : removed) {
    removed_participants_.insert(guid);
  }
  
  remaining_participants_ -= removed.size();
  
  if (remaining_participants_ == 0) {
    set_state(DRAINED);
  }
}

void DrainManager::update_status(RelayStatus& status) const
{
  // Update using the new DrainStatus struct
  status.drain_status().state(state_);
  status.drain_status().remaining_participants(remaining_participants_);
  status.drain_status().total_participants(total_participants_);
  status.drain_status().start_time(get_drain_start_time());
  status.drain_status().last_update_time(static_cast<unsigned long long>(ACE_OS::gettimeofday().sec()));
  status.drain_status().rate_per_second(drain_rate_per_second_);
}

}