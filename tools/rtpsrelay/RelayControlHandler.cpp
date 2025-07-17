#include "RelayControlHandler.h"

#include <dds/rtpsrelaylib/RelayTypeSupportImpl.h>
#include <dds/rtpsrelaylib/RelayC.h>

#include <ace/Log_Msg.h>

namespace RtpsRelay {

RelayControlHandler::RelayControlHandler(const std::string& relay_id)
  : relay_id_(relay_id)
{}

void RelayControlHandler::on_data_available(DDS::DataReader_ptr reader)
{
  RelayConfigDataReader_var control_reader = RelayConfigDataReader::_narrow(reader);
  if (!control_reader) {
    ACE_ERROR((LM_ERROR, ACE_TEXT("(%P|%t) ERROR: RelayControlHandler::on_data_available: ")
              ACE_TEXT("Failed to narrow RelayControlDataReader\n")));
    return;
  }

  RelayConfig control;
  DDS::SampleInfo info;
  while (control_reader->take_next_sample(control, info) == DDS::RETCODE_OK) {
    if (info.valid_data && control.relay_id() == relay_id_) {
      for (const auto& p : control.config()) {
        TheServiceParticipant->config_store()->set(p.first.c_str(), p.second);
      }
    }
  }
}

}
