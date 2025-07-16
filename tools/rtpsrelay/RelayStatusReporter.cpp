#include "RelayStatusReporter.h"

#include <ace/OS_NS_time.h>

namespace RtpsRelay {

RelayStatusReporter::RelayStatusReporter(const Config& config,
                                         GuidAddrSet& guid_addr_set,
                                         RelayStatusDataWriter_var writer,
                                         ACE_Reactor* reactor)
  : config_(config)
  , guid_addr_set_(guid_addr_set)
  , writer_(writer)
  , reactor_(reactor)
{
  if (config.publish_relay_status() != OpenDDS::DCPS::TimeDuration::zero_value) {
    reactor_->schedule_timer(this, 0, ACE_Time_Value(), config.publish_relay_status().value());
  }
}

int RelayStatusReporter::handle_timeout(const ACE_Time_Value&, const void*)
{
  report_relay_status();
  return 0;
}

void RelayStatusReporter::report_relay_status()
{
  RelayStatus status;
  {
    GuidAddrSet::Proxy proxy(guid_addr_set_);
    proxy.populate_relay_status(status);
  }
  status.relay_id(config_.relay_id());

  DDS::ReturnCode_t ret = writer_->write(status, DDS::HANDLE_NIL);
  if (ret != DDS::RETCODE_OK) {
    ACE_ERROR((LM_ERROR, ACE_TEXT("(%P|%t) ERROR: failed to write relay status\n")));
  }
}

}
