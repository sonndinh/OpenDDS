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

  // Override all required virtual methods:
  void on_data_available(DDS::DataReader_ptr reader) override;
  
  void on_requested_deadline_missed(DDS::DataReader_ptr,
                                   const DDS::RequestedDeadlineMissedStatus&) override {}
  
  void on_requested_incompatible_qos(DDS::DataReader_ptr, 
                                    const DDS::RequestedIncompatibleQosStatus&) override {}
  
  void on_sample_rejected(DDS::DataReader_ptr,
                         const DDS::SampleRejectedStatus&) override {}
  
  void on_liveliness_changed(DDS::DataReader_ptr,
                            const DDS::LivelinessChangedStatus&) override {}
  
  void on_subscription_matched(DDS::DataReader_ptr,
                              const DDS::SubscriptionMatchedStatus&) override {}
  
  void on_sample_lost(DDS::DataReader_ptr,
                     const DDS::SampleLostStatus&) override {}

private:
  // Change the parameter type from unsigned long to ParameterValue
  void process_command(const std::string& command, ParameterValue parameter);

  std::string relay_id_;
  DrainManager& drain_manager_;
};

}

#endif