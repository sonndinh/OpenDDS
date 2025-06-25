#include "RelayStatusReporter.h"
#include "DrainManager.h"

#include <ace/OS_NS_time.h>

namespace RtpsRelay {

RelayStatusReporter::RelayStatusReporter(const Config& config,
                                         const GuidAddrSet& guid_addr_set,
                                         RelayStatusDataWriter_var status_writer,
                                         ACE_Reactor* reactor,
                                         DrainManager* drain_manager)
  : config_(config)
  , config_(config)
  , guid_addr_set_(guid_addr_set)
  , status_writer_(status_writer)
  , reactor_(reactor)
  , drain_manager_(drain_manager)
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
  status.relay_id(config_.relay_id());
  status.admitting(guid_addr_set_.admitting());
  
  // Initialize the drain_status with default values
  DrainStatus drain_status;
  drain_status.state(ACTIVE);
  drain_status.remaining_participants(guid_addr_set_.get_participant_count());
  drain_status.total_participants(guid_addr_set_.get_participant_count());
  drain_status.start_time(0);
  drain_status.last_update_time(static_cast<unsigned long long>(ACE_OS::gettimeofday().sec()));
  drain_status.rate_per_second(0);
  
  // If drain_manager exists, let it update the status
  if (drain_manager_) {
    drain_manager_->update_status(status);
  } else {
    // Otherwise use the default values
    status.drain_status(drain_status);
  }
  
  DDS::ReturnCode_t ret = status_writer_->write(status, DDS::HANDLE_NIL);
  if (ret != DDS::RETCODE_OK) {
    ACE_ERROR((LM_ERROR, ACE_TEXT("(%P|%t) ERROR: failed to write relay status\n")));
  }
}

}
