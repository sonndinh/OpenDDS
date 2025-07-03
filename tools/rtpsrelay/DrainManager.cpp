#include "DrainManager.h"
#include "GuidAddrSet.h"

#include <ace/OS_NS_time.h>
#include <ace/Log_Msg.h>

namespace RtpsRelay {

DrainManager::DrainManager(const Config& config, const std::string& relay_id)
  : relay_id_(relay_id)
  , state_(DrainState::DS_ACTIVE)
  , drain_rate_per_second_(config.drain_rate_per_second())
  , drain_check_interval_(config.drain_check_interval())
{
  // Initialization code
}

void DrainManager::set_state(DrainState state)
{
  if (state_ == state) {
    return; // No change
  }
  
  if (state == DrainState::DS_DRAINING && state_ == DrainState::DS_ACTIVE) {
    // Starting to drain, record the start time
    drain_start_time_ = std::chrono::steady_clock::now();
    ACE_DEBUG((LM_INFO, ACE_TEXT("(%P|%t) INFO: DrainManager::set_state: ")
              ACE_TEXT("Starting drain process for relay %C\n"), relay_id_.c_str()));
  } else if (state == DrainState::DS_DRAINED) {
    ACE_DEBUG((LM_INFO, ACE_TEXT("(%P|%t) INFO: DrainManager::set_state: ")
              ACE_TEXT("Drain process completed for relay %C\n"), relay_id_.c_str()));
  } else if (state == DrainState::DS_PAUSED) {
    ACE_DEBUG((LM_INFO, ACE_TEXT("(%P|%t) INFO: DrainManager::set_state: ")
              ACE_TEXT("Relay %C is now paused - not admitting new participants\n"), relay_id_.c_str()));
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
  if (state_ != DrainState::DS_DRAINING) {
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
    set_state(DrainState::DS_DRAINED);
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
    set_state(DrainState::DS_DRAINED);
  }
}

unsigned long long DrainManager::get_drain_start_time() const
{
  if (state_ == DrainState::DS_DRAINING || state_ == DrainState::DS_DRAINED) {
    // Convert the steady_clock time to epoch time (seconds since Jan 1, 1970)
    auto now = std::chrono::system_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(
      now.time_since_epoch());
    return static_cast<unsigned long long>(duration.count());
  }
  return 0; // Return 0 if not draining or drained
}

void DrainManager::update_status(RelayStatus& status) const
{
  // Update using the DrainStatus struct
  DrainStatus drain_status;
  drain_status.state(state_);
  drain_status.remaining_participants(remaining_participants_);
  drain_status.total_participants(total_participants_);
  drain_status.start_time(get_drain_start_time());

  drain_status.rate_per_second(drain_rate_per_second_);
  status.drain_status(drain_status);
}

}