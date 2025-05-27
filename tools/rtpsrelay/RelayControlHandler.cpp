#include "RelayControlHandler.h"
#include "DrainManager.h"

#include <dds/rtpsrelaylib/RelayTypeSupportImpl.h>
#include <dds/rtpsrelaylib/RelayC.h>

#include <ace/Log_Msg.h>

namespace RtpsRelay {

RelayControlHandler::RelayControlHandler(const std::string& relay_id, DrainManager& drain_manager)
  : relay_id_(relay_id)
  , drain_manager_(drain_manager)
{}

void RelayControlHandler::on_data_available(DDS::DataReader_ptr reader)
{
  RelayControlDataReader_var control_reader = RelayControlDataReader::_narrow(reader);
  if (!control_reader) {
    ACE_ERROR((LM_ERROR, ACE_TEXT("(%P|%t) ERROR: RelayControlHandler::on_data_available: ")
              ACE_TEXT("Failed to narrow RelayControlDataReader\n")));
    return;
  }

  RelayControl control;
  DDS::SampleInfo info;
  while (control_reader->take_next_sample(control, info) == DDS::RETCODE_OK) {
    if (info.valid_data && control.relay_id == relay_id_) {
      process_command(control.command, control.parameter);
    }
  }
}

void RelayControlHandler::process_command(const std::string& command, unsigned long parameter)
{
  // Handle drain control commands
  if (command == CMD_SET_DRAIN_STATE) {
    if (parameter <= 3) { // Updated to <= 3 to include the new state
      ACE_DEBUG((LM_INFO, ACE_TEXT("(%P|%t) INFO: RelayControlHandler::process_command: ")
                ACE_TEXT("Setting drain state to %d\n"), parameter));
      drain_manager_.set_state(static_cast<DrainState>(parameter));
    } else {
      ACE_ERROR((LM_ERROR, ACE_TEXT("(%P|%t) ERROR: RelayControlHandler::process_command: ")
                ACE_TEXT("Invalid drain state value: %d\n"), parameter));
    }
  } else if (command == CMD_SET_DRAIN_RATE) {
    ACE_DEBUG((LM_INFO, ACE_TEXT("(%P|%t) INFO: RelayControlHandler::process_command: ")
              ACE_TEXT("Setting drain rate to %d participants/sec\n"), parameter));
    drain_manager_.set_drain_rate(parameter);
  } else {
    ACE_DEBUG((LM_DEBUG, ACE_TEXT("(%P|%t) DEBUG: RelayControlHandler::process_command: ")
              ACE_TEXT("Unknown command: %C\n"), command.c_str()));
  }
}

}