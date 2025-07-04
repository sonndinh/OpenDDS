#ifndef RTPSRELAY_RELAY_CONTROL_HANDLER_H
#define RTPSRELAY_RELAY_CONTROL_HANDLER_H

#include "DrainManager.h"

#include <dds/DdsDcpsSubscriptionC.h>
#include <dds/DCPS/DataReaderImpl.h>
#include <dds/DCPS/Definitions.h>

namespace RtpsRelay {

class RelayControlHandler : public OpenDDS::DCPS::LocalObject<DDS::DataReaderListener> {
public:
  RelayControlHandler(const std::string& relay_id, DrainManager& drain_manager);

  void on_data_available(DDS::DataReader_ptr reader) override;
  void on_requested_deadline_missed(DDS::DataReader_ptr, const DDS::RequestedDeadlineMissedStatus&) override {}

private:
  // Change the parameter type from unsigned long to ParameterValue
  void process_command(const std::string& command, ParameterValue parameter);

  std::string relay_id_;
  DrainManager& drain_manager_;
};

}

#endif