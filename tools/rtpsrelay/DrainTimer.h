#ifndef RTPSRELAY_DRAIN_TIMER_H
#define RTPSRELAY_DRAIN_TIMER_H

#include "DrainManager.h"

#include <ace/Event_Handler.h>
#include <ace/Reactor.h>

namespace RtpsRelay {

class GuidAddrSet;

// DrainTimer - A periodic task that triggers drain cycles at regular intervals
class DrainTimer : public ACE_Event_Handler {
public:
  DrainTimer(DrainManager& drain_manager, 
             GuidAddrSet& guid_addr_set,
             const OpenDDS::DCPS::TimeDuration& check_interval);
  
  ~DrainTimer();
  
  // Called by ACE_Reactor when the timer expires
  int handle_timeout(const ACE_Time_Value& tv, const void* arg) override;
  
  // Start the periodic timer
  void start();
  
  // Stop the periodic timer
  void stop();
  
private:
  DrainManager& drain_manager_;
  GuidAddrSet& guid_addr_set_;
  OpenDDS::DCPS::TimeDuration check_interval_;
  bool active_{false};
  long timer_id_{-1};
};

} // namespace RtpsRelay

#endif // RTPSRELAY_DRAIN_TIMER_H