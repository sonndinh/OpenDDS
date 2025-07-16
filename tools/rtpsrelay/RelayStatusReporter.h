#ifndef RTPSRELAY_RELAY_STATUS_REPORTER_H_
#define RTPSRELAY_RELAY_STATUS_REPORTER_H_

#include "Config.h"
#include "GuidAddrSet.h"

#include <dds/rtpsrelaylib/RelayTypeSupportImpl.h>

namespace RtpsRelay {

class RelayStatusReporter : public ACE_Event_Handler {
public:
  RelayStatusReporter(const Config& config,
                      GuidAddrSet& guid_addr_set,
                      RelayStatusDataWriter_var writer,
                      ACE_Reactor* reactor);

  int handle_timeout(const ACE_Time_Value& now, const void* act);
  void report_relay_status();

private:
  const Config& config_;
  GuidAddrSet& guid_addr_set_;
  RelayStatusDataWriter_var writer_;
  ACE_Reactor* reactor_;
};

}

#endif // RTPSRELAY_RELAY_STATUS_REPORTER_H_
