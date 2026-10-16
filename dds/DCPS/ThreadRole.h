/*
 * Distributed under the OpenDDS License.
 * See: http://www.opendds.org/license.html
 */

#ifndef OPENDDS_DCPS_THREADROLE_H
#define OPENDDS_DCPS_THREADROLE_H

#include "dcps_export.h"
#include "PoolAllocator.h"

#ifdef OPENDDS_DISCOVERY_MUTEX_PROFILING
#  include <ace/os_include/sys/os_types.h>

#  include <cstdint>
#  include <map>
#endif

#if !defined (ACE_LACKS_PRAGMA_ONCE)
#  pragma once
#endif

OPENDDS_BEGIN_VERSIONED_NAMESPACE_DECL

namespace OpenDDS {
namespace DCPS {

#ifdef OPENDDS_DISCOVERY_MUTEX_PROFILING
/// Label the calling thread for profiling output. The OS thread id is appended.
OpenDDS_Dcps_Export void set_thread_role(const String& role);

/// Label of the calling thread, "other(tid N)" if never set.
OpenDDS_Dcps_Export const String& thread_role();

/// Called from ThreadStatusManager on every active/idle update of the calling thread.
OpenDDS_Dcps_Export void thread_busy_transition(bool active, bool nested);

struct ThreadBusySample {
  ThreadBusySample() : labeled(false), busy_ns(0), cpu_ns(-1), runq_ns(-1) {}

  String role;
  bool labeled; // false for threads without set_thread_role, whose idle time isn't marked
  std::int64_t busy_ns; // wall time spent active, including an in-progress busy period
  std::int64_t cpu_ns; // on-CPU time from /proc schedstat, -1 if unavailable
  std::int64_t runq_ns; // time runnable but waiting for a CPU, -1 if unavailable
};

typedef std::map<pid_t, ThreadBusySample> ThreadBusySnapshot;

/// Cumulative busy time of every thread seen so far, keyed by OS thread id.
OpenDDS_Dcps_Export ThreadBusySnapshot thread_busy_snapshot();
#endif

} // namespace DCPS
} // namespace OpenDDS

OPENDDS_END_VERSIONED_NAMESPACE_DECL

#endif
