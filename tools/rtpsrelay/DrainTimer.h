#ifndef RTPSRELAY_DRAIN_TIMER_H
#define RTPSRELAY_DRAIN_TIMER_H

#include "DrainManager.h"

#include <ace/Event_Handler.h>
#include <ace/Reactor.h>

namespace RtpsRelay {

class GuidAddrSet;

class DrainTimer : public ACE_Event_Handler {
public:
  DrainTimer(DrainManager& drain_manager, 
             GuidAddrSet& guid_addr_set,
             unsigned check_interval_ms);
  
  ~DrainTimer();
  
  int handle_timeout(const ACE_Time_Value& tv, const void* arg) override;
  
  void start();
  void stop();
  
private:
  DrainManager& drain_manager_;
  GuidAddrSet& guid_addr_set_;
  unsigned check_interval_ms_;
  bool active_{false};
  long timer_id_{-1};
};

} // namespace RtpsRelay

#endif // RTPSRELAY_DRAIN_TIMER_H