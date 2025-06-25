#ifndef RTPSRELAY_RELAY_STATUS_REPORTER_H_
#define RTPSRELAY_RELAY_STATUS_REPORTER_H_

#include "Config.h"
#include "GuidAddrSet.h"

#include <dds/rtpsrelaylib/RelayTypeSupportImpl.h>

// Forward declaration for the DrainManager
namespace RtpsRelay {
class DrainManager;
}

namespace RtpsRelay {

class RelayStatusReporter : public ACE_Event_Handler {
public:
  RelayStatusReporter(const Config& config,
                      const GuidAddrSet& guid_addr_set,
                      RelayStatusDataWriter_var status_writer,
                      ACE_Reactor* reactor,
                      DrainManager* drain_manager = nullptr);

private:
  int handle_timeout(const ACE_Time_Value& now, const void* token) override;

  const Config& config_;
  void report_relay_status();

  const Config& config_;
  const GuidAddrSet& guid_addr_set_;
  RelayStatusDataWriter_var status_writer_;
  ACE_Reactor* reactor_;
  DrainManager* drain_manager_;
};

}

#endif // RTPSRELAY_RELAY_STATUS_REPORTER_H_
