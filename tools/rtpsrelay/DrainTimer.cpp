#include "DrainTimer.h"
#include "GuidAddrSet.h"

namespace RtpsRelay {

DrainTimer::DrainTimer(DrainManager& drain_manager, 
                       GuidAddrSet& guid_addr_set,
                       const OpenDDS::DCPS::TimeDuration& check_interval)
  : drain_manager_(drain_manager)
  , guid_addr_set_(guid_addr_set)
  , check_interval_(check_interval)
{}

DrainTimer::~DrainTimer()
{
  stop();
}

int DrainTimer::handle_timeout(const ACE_Time_Value&, const void*)
{
  // Process a single drain cycle
  drain_manager_.process_drain_cycle(guid_addr_set_);
  
  // Return 0 to keep the timer active - this is what makes it periodic
  return 0;
}

void DrainTimer::start()
{
  if (!active_) {
    // Convert TimeDuration to ACE_Time_Value
    ACE_Time_Value interval(check_interval_.value().sec(), 
                           check_interval_.value().usec());
    
    // Schedule as a periodic timer - first argument is initial delay (zero),
    // second is the recurring interval
    timer_id_ = ACE_Reactor::instance()->schedule_timer(
      this, 
      nullptr, 
      ACE_Time_Value::zero,
      interval);
      
    if (timer_id_ != -1) {
      active_ = true;
      ACE_DEBUG((LM_INFO, "(%P|%t) INFO: DrainTimer::start "
                 "Started periodic drain timer with interval %d.%06d seconds\n",
                 interval.sec(), interval.usec()));
    } else {
      ACE_ERROR((LM_ERROR, "(%P|%t) ERROR: DrainTimer::start "
                 "Failed to schedule periodic drain timer\n"));
    }
  }
}

void DrainTimer::stop()
{
  if (active_ && timer_id_ != -1) {
    ACE_Reactor::instance()->cancel_timer(timer_id_);
    timer_id_ = -1;
    active_ = false;
    ACE_DEBUG((LM_INFO, "(%P|%t) INFO: DrainTimer::stop "
               "Stopped periodic drain timer\n"));
  }
}

} // namespace RtpsRelay