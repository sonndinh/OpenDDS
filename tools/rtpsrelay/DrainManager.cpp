#include "DrainManager.h"
#include "GuidAddrSet.h"

#include <ace/OS_NS_time.h>
#include <ace/Log_Msg.h>

namespace RtpsRelay {

DrainManager::DrainManager(const Config& config, const std::string& relay_id)
  : relay_id_(relay_id)
  , state_(DrainState::DS_ACTIVE)
  , drain_interval_ms_(config.drain_interval_ms())
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

void DrainManager::set_drain_interval(unsigned interval_ms)
{
  drain_interval_ms_ = interval_ms;
  
  ACE_DEBUG((LM_INFO, ACE_TEXT("(%P|%t) INFO: DrainManager::set_drain_interval: ")
            ACE_TEXT("Setting drain interval to %d ms\n"), interval_ms));
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
  // CHANGE this calculation
  unsigned to_remove = 1; // Default to 1 participant per cycle
  
  // If we have a valid interval, calculate rate: participants per second = 1000 / interval_ms
  if (drain_interval_ms_ > 0) {
    to_remove = static_cast<unsigned>(1000 / drain_interval_ms_);
    if (to_remove == 0) {
      to_remove = 1; // Minimum of 1 participant
    }
  }
  
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
    removed_participants_.emplace(guid);
  }
  
  remaining_participants_ -= removed.size();
  
  if (remaining_participants_ == 0) {
    set_state(DrainState::DS_DRAINED);
  }
}

DDS::Time_t DrainManager::get_drain_start_time() const
{
  DDS::Time_t result = {0, 0}; // Initialize to zero

  if (state_ == DrainState::DS_DRAINING || state_ == DrainState::DS_DRAINED) {
    // Convert the steady_clock time to system_clock time for epoch reference
    auto now = std::chrono::system_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(
      now.time_since_epoch());
    
    // Fill in the DDS::Time_t structure
    result.sec = static_cast<CORBA::Long>(duration.count());
    
    // Get nanoseconds part and convert to nanoseconds
    auto nsec = std::chrono::duration_cast<std::chrono::nanoseconds>(
      now.time_since_epoch()) % std::chrono::seconds(1);
    result.nanosec = static_cast<CORBA::ULong>(nsec.count());
  }
  
  return result;
}

void DrainManager::update_status(RelayStatus& status) const
{
  // Update using the DrainStatus struct
  DrainStatus drain_status;
  drain_status.state(state_);
  drain_status.remaining_participants(remaining_participants_);
  drain_status.total_participants(total_participants_);
  drain_status.start_time(get_drain_start_time()); 
  drain_status.drain_interval_ms(drain_interval_ms_);
  status.drain_status(drain_status);
}

}