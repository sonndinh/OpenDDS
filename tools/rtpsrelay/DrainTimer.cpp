#include "DrainTimer.h"

namespace RtpsRelay {

DrainTimer::DrainTimer(DrainManager& drain_manager, 
                       GuidAddrSet& guid_addr_set,
                       unsigned check_interval_ms)
  : drain_manager_(drain_manager)
  , guid_addr_set_(guid_addr_set)
  , check_interval_ms_(check_interval_ms)
{}

DrainTimer::~DrainTimer()
{
  stop();
}

int DrainTimer::handle_timeout(const ACE_Time_Value&, const void*)
{
  drain_manager_.process_drain_cycle(guid_addr_set_);
  return 0;
}

void DrainTimer::start()
{
  if (!active_) {
    ACE_Time_Value interval(0, check_interval_ms_ * 1000);
    timer_id_ = ACE_Reactor::instance()->schedule_timer(
      this, 
      nullptr, 
      ACE_Time_Value::zero,
      interval);
    active_ = true;
  }
}

void DrainTimer::stop()
{
  if (active_ && timer_id_ != -1) {
    ACE_Reactor::instance()->cancel_timer(timer_id_);
    timer_id_ = -1;
    active_ = false;
  }
}

}