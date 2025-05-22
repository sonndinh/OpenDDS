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
  , guid_addr_set_(guid_addr_set)
  , status_writer_(status_writer)
  , reactor_(reactor)
  , drain_manager_(drain_manager)
{
  if (config.publish_relay_status() != OpenDDS::DCPS::TimeDuration::zero_value) {
    reactor_->schedule_timer(this, 0, ACE_Time_Value(), config.publish_relay_status().value());
  }
}

int RelayStatusReporter::handle_timeout(const ACE_Time_Value& /*now*/, const void* /*token*/)
{
  report_relay_status();
  return 0;
}

void RelayStatusReporter::report_relay_status()
{
  RelayStatus status;
  status.relay_id(config_.relay_id());
  status.admitting(guid_addr_set_.admitting());
  
  // Add drain status information if drain_manager is available
  if (drain_manager_) {
    status.drain_state(drain_manager_->get_state());
    status.remaining_participants(guid_addr_set_.get_participant_count());
    status.total_participants(drain_manager_->get_total_participants());
    status.drain_start_time(drain_manager_->get_drain_start_time());
    status.last_update_time(static_cast<unsigned long long>(ACE_OS::gettimeofday().sec()));
    status.current_drain_rate(drain_manager_->get_drain_rate());
  } else {
    // Default values if no drain manager is available
    status.drain_state(ACTIVE);
    status.remaining_participants(guid_addr_set_.get_participant_count());
    status.total_participants(guid_addr_set_.get_participant_count());
    status.drain_start_time(0);
    status.last_update_time(static_cast<unsigned long long>(ACE_OS::gettimeofday().sec()));
    status.current_drain_rate(0);
  }
  
  DDS::ReturnCode_t ret = status_writer_->write(status, DDS::HANDLE_NIL);
  if (ret != DDS::RETCODE_OK) {
    ACE_ERROR((LM_ERROR, ACE_TEXT("(%P|%t) ERROR: failed to write relay status\n")));
  }
}

}
