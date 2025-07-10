#ifndef RTPSRELAY_DRAIN_MANAGER_H
#define RTPSRELAY_DRAIN_MANAGER_H

#include "Config.h"
#include <dds/rtpsrelaylib/RelayC.h>
#include <dds/DCPS/TimeDuration.h>
#include <dds/DCPS/GuidUtils.h>
#include <dds/DdsDcpsInfrastructureC.h>

#include <chrono>
#include <set>
#include <string>
#include <vector>

namespace RtpsRelay {

class GuidAddrSet;

class DrainManager {
public:
  DrainManager(const Config& config, const std::string& relay_id);
  
  void set_state(DrainState state);
  DrainState get_state() const { return state_; }

  
  // Use this method instead
  void set_drain_interval(unsigned interval_ms);
  unsigned get_drain_interval_ms() const { return drain_interval_ms_; }
  
  // Check if a participant has been removed during draining
  bool is_participant_removed(const OpenDDS::DCPS::GUID_t& guid) const;
  
  // Periodic drain process
  void process_drain_cycle(GuidAddrSet& guid_addr_set);
  
  // Get values for status reporting
  unsigned get_total_participants() const { return total_participants_; }
  DDS::Time_t get_drain_start_time() const;
  void update_status(RelayStatus& status) const;
  
private:
  std::string relay_id_;
  DrainState state_{DrainState::DS_ACTIVE};
  
  // Use this member instead
  unsigned drain_interval_ms_{0};  // milliseconds between participant removals
  
  OpenDDS::DCPS::TimeDuration drain_check_interval_;
  std::chrono::steady_clock::time_point drain_start_time_;
  unsigned total_participants_{0};
  unsigned remaining_participants_{0};
  std::set<OpenDDS::DCPS::GUID_t> removed_participants_;
};

}

#endif