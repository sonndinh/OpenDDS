/*
 * Distributed under the OpenDDS License.
 * See: http://www.opendds.org/license.html
 */

#include <DCPS/DdsDcps_pch.h> // Only the _pch include should start with DCPS/

#include "ThreadRole.h"

#include <ace/Guard_T.h>
#include <ace/OS_NS_Thread.h>
#include <ace/Thread_Mutex.h>

#include <chrono>
#include <sstream>

OPENDDS_BEGIN_VERSIONED_NAMESPACE_DECL

namespace OpenDDS {
namespace DCPS {

#ifdef OPENDDS_DISCOVERY_PROFILING
namespace {
  struct BusyEntry {
    BusyEntry() : labeled(false), busy_ns(0), busy_since(0) {}

    ACE_Thread_Mutex mutex;
    String role;
    bool labeled;
    std::int64_t busy_ns;
    std::int64_t busy_since; // 0 while idle
  };

  // Entries outlive their threads so stats remain readable after a thread exits.
  struct Registry {
    ACE_Thread_Mutex mutex;
    std::map<pid_t, BusyEntry*> entries;
  };

  Registry& registry()
  {
    static Registry* const r = new Registry;
    return *r;
  }

  struct ThreadState {
    ThreadState() : tid(ACE_OS::thr_gettid()), entry(0), depth(0) {}

    const pid_t tid;
    String role;
    BusyEntry* entry;
    size_t depth;
  };

  ThreadState& thread_state()
  {
    static thread_local ThreadState state;
    return state;
  }

  std::int64_t now_ns()
  {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
  }

  String with_tid(const String& role, pid_t tid)
  {
    std::ostringstream os;
    os << role << "(tid " << tid << ')';
    return os.str().c_str();
  }

  BusyEntry& entry_for(ThreadState& ts)
  {
    if (!ts.entry) {
      Registry& r = registry();
      ACE_Guard<ACE_Thread_Mutex> g(r.mutex);
      BusyEntry*& e = r.entries[ts.tid];
      if (!e) {
        e = new BusyEntry;
      }
      ts.entry = e;
    }
    return *ts.entry;
  }
}

void set_thread_role(const String& role)
{
  ThreadState& ts = thread_state();
  ts.role = with_tid(role, ts.tid);
  BusyEntry& e = entry_for(ts);
  ACE_Guard<ACE_Thread_Mutex> g(e.mutex);
  e.role = ts.role;
  e.labeled = true;
}

const String& thread_role()
{
  ThreadState& ts = thread_state();
  if (ts.role.empty()) {
    ts.role = with_tid("other", ts.tid);
  }
  return ts.role;
}

void thread_busy_transition(bool active, bool nested)
{
  ThreadState& ts = thread_state();
  if (nested) {
    if (active) {
      ++ts.depth;
    } else if (ts.depth) {
      --ts.depth;
    }
  }
  // Same rules as ThreadStatusManager::Thread::update.
  if (nested && !(active && ts.depth == 1) && !(!active && ts.depth == 0)) {
    return;
  }

  BusyEntry& e = entry_for(ts);
  const std::int64_t now = now_ns();
  ACE_Guard<ACE_Thread_Mutex> g(e.mutex);
  if (e.role.empty()) {
    e.role = thread_role();
  }
  if (active && !e.busy_since) {
    e.busy_since = now;
  } else if (!active && e.busy_since) {
    e.busy_ns += now - e.busy_since;
    e.busy_since = 0;
  }
}

ThreadBusySnapshot thread_busy_snapshot()
{
  ThreadBusySnapshot snapshot;
  Registry& r = registry();
  ACE_Guard<ACE_Thread_Mutex> rg(r.mutex);
  const std::int64_t now = now_ns();
  for (std::map<pid_t, BusyEntry*>::const_iterator it = r.entries.begin(); it != r.entries.end(); ++it) {
    BusyEntry& e = *it->second;
    ACE_Guard<ACE_Thread_Mutex> eg(e.mutex);
    ThreadBusySample& sample = snapshot[it->first];
    sample.role = e.role;
    sample.labeled = e.labeled;
    sample.busy_ns = e.busy_ns + (e.busy_since ? now - e.busy_since : 0);
  }
  return snapshot;
}
#endif

} // namespace DCPS
} // namespace OpenDDS

OPENDDS_END_VERSIONED_NAMESPACE_DECL
