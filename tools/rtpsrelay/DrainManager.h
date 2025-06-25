#ifndef RTPSRELAY_DRAIN_MANAGER_H
#define RTPSRELAY_DRAIN_MANAGER_H

#include "DrainConfig.h"
// This includes the RelayC.h which should have the GUID_t definition
#include <dds/rtpsrelaylib/RelayC.h>

#include <chrono>
#include <set>
#include <string>
#include <vector>

namespace RtpsRelay {

class GuidAddrSet;

class DrainManager {
public:
  explicit DrainManager(const DrainConfig& config, const std::string& relay_id);
  
  // State management
  void set_state(DrainState state);
  DrainState get_state() const { return state_; }
  
  // Drain rate control
  void set_drain_rate(unsigned rate);
  unsigned get_drain_rate() const { return drain_rate_per_second_; }
  
  // Check if a participant has been removed during draining
  bool is_participant_removed(const GUID_t& guid) const;
  
  // Periodic drain process
  void process_drain_cycle(GuidAddrSet& guid_addr_set);
  
  // Get values for status reporting
  unsigned get_total_participants() const { return total_participants_; }
  unsigned long long get_drain_start_time() const;
  
  // Update the relay status with drain information
  void update_status(RelayStatus& status) const;
  
private:
  std::string relay_id_;
  DrainState state_{DS_ACTIVE}; // Changed from ACTIVE to DS_ACTIVE
  unsigned drain_rate_per_second_;
  OpenDDS::DCPS::TimeDuration drain_check_interval_;
  std::chrono::steady_clock::time_point drain_start_time_;
  unsigned total_participants_{0};
  unsigned remaining_participants_{0};
  // Use the GUID_t defined in RelayC.h
  std::set<GUID_t> removed_participants_;
  // Remove participants
  std::vector<OpenDDS::DCPS::GUID_t> removed;
};

} 

#endif 