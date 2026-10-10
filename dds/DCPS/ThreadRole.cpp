/*
 * Distributed under the OpenDDS License.
 * See: http://www.opendds.org/license.html
 */

#include <DCPS/DdsDcps_pch.h> // Only the _pch include should start with DCPS/

#include "ThreadRole.h"

#include <ace/OS_NS_Thread.h>

#include <sstream>

OPENDDS_BEGIN_VERSIONED_NAMESPACE_DECL

namespace OpenDDS {
namespace DCPS {

#ifdef OPENDDS_DISCOVERY_PROFILING
namespace {
  String& current_role()
  {
    static thread_local String role;
    return role;
  }

  String with_tid(const String& role)
  {
    std::ostringstream os;
    os << role << "(tid " << ACE_OS::thr_gettid() << ')';
    return os.str().c_str();
  }
}

void set_thread_role(const String& role)
{
  current_role() = with_tid(role);
}

const String& thread_role()
{
  String& role = current_role();
  if (role.empty()) {
    role = with_tid("other");
  }
  return role;
}
#endif

} // namespace DCPS
} // namespace OpenDDS

OPENDDS_END_VERSIONED_NAMESPACE_DECL
