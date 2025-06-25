#ifndef RTPSRELAY_DRAIN_CONFIG_H
#define RTPSRELAY_DRAIN_CONFIG_H

#include <dds/DCPS/TimeDuration.h>  // Add this include

namespace RtpsRelay {

struct DrainConfig {
  bool enable_drain_feature{true};
  unsigned drain_rate_per_second{2}; // Default drain rate
  OpenDDS::DCPS::TimeDuration drain_check_interval{OpenDDS::DCPS::TimeDuration(0, 1000000)}; // 1 second
};

} 

#endif