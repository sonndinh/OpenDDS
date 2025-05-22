#ifndef RTPSRELAY_DRAIN_CONFIG_H
#define RTPSRELAY_DRAIN_CONFIG_H

namespace RtpsRelay {

struct DrainConfig {
  bool enable_drain_feature{true};
  unsigned drain_rate_per_second{2}; // Default drain rate
  unsigned drain_check_interval_ms{1000}; // How often to process draining
};

} 

#endif 