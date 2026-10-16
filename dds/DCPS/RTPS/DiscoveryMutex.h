/*
 * Distributed under the OpenDDS License.
 * See: http://www.opendds.org/license.html
 */

#ifndef OPENDDS_DCPS_RTPS_DISCOVERYMUTEX_H
#define OPENDDS_DCPS_RTPS_DISCOVERYMUTEX_H

#include <dds/Versioned_Namespace.h>

#include <ace/Thread_Mutex.h>

#ifdef OPENDDS_DISCOVERY_MUTEX_PROFILING
#  ifndef ACE_HAS_CPP11
#    error "OPENDDS_DISCOVERY_MUTEX_PROFILING requires C++11"
#  endif
#  include <ace/Guard_T.h>
#  include <ace/Log_Msg.h>
#  include <ace/OS_NS_Thread.h>

#  include <dds/DCPS/ThreadRole.h>

#  include <atomic>
#  include <chrono>
#  include <cstdint>
#  include <map>
#  include <sstream>
#  include <string>
#endif

#if !defined (ACE_LACKS_PRAGMA_ONCE)
#  pragma once
#endif

OPENDDS_BEGIN_VERSIONED_NAMESPACE_DECL

namespace OpenDDS {
namespace RTPS {

#ifndef OPENDDS_DISCOVERY_MUTEX_PROFILING

typedef ACE_Thread_Mutex DiscoveryMutex;

#else

/// Drop-in replacement for ACE_Thread_Mutex that records per-thread wait and hold times.
class DiscoveryMutex {
public:
  typedef std::chrono::steady_clock Clock;
  typedef std::int64_t Nanos;

  /// Bucket i counts samples in [2^(i-1), 2^i) ns; bucket 0 counts 0 ns.
  struct Histogram {
    static const int BUCKETS = 41;
    std::uint64_t counts[BUCKETS];

    Histogram() : counts() {}

    void add(Nanos ns)
    {
      int b = 0;
      for (std::uint64_t v = static_cast<std::uint64_t>(ns); v && b < BUCKETS - 1; v >>= 1) {
        ++b;
      }
      ++counts[b];
    }

    void subtract(const Histogram& other)
    {
      for (int b = 0; b < BUCKETS; ++b) {
        counts[b] -= other.counts[b];
      }
    }

    std::string to_string() const
    {
      std::ostringstream os;
      for (int b = 0; b < BUCKETS; ++b) {
        if (counts[b]) {
          os << " <2^" << b << "ns:" << counts[b];
        }
      }
      return os.str();
    }
  };

  struct Durations {
    Nanos total;
    Nanos max;
    Nanos interval_max; // reset by each report
    Histogram hist;

    Durations() : total(0), max(0), interval_max(0) {}

    void add(Nanos ns)
    {
      total += ns;
      if (ns > max) {
        max = ns;
      }
      if (ns > interval_max) {
        interval_max = ns;
      }
      hist.add(ns);
    }

    void subtract(const Durations& other)
    {
      total -= other.total;
      hist.subtract(other.hist);
    }
  };

  struct ThreadStats {
    DCPS::String role;
    std::uint64_t acquires; // successful acquire() and tryacquire()
    std::uint64_t contended; // acquire() calls that had to block
    std::uint64_t try_acquires; // successful tryacquire()
    Durations wait;
    Durations hold;

    ThreadStats() : acquires(0), contended(0), try_acquires(0) {}

    void subtract(const ThreadStats& other)
    {
      acquires -= other.acquires;
      contended -= other.contended;
      try_acquires -= other.try_acquires;
      wait.subtract(other.wait);
      hold.subtract(other.hold);
    }
  };

  typedef std::map<pid_t, ThreadStats> StatsMap;

  explicit DiscoveryMutex(const ACE_TCHAR* name = 0)
    : mutex_(name)
    , holder_(0)
    , try_failures_(0)
    , reporting_enabled_(false)
  {
    initial_.time = Clock::now();
    initial_.busy = DCPS::thread_busy_snapshot();
    previous_ = initial_;
  }

  ~DiscoveryMutex()
  {
    if (reporting_enabled_) {
      log_report("final");
    }
  }

  /// Enables the final report at destruction; periodic reports are driven by the caller.
  void enable_reporting() { reporting_enabled_ = true; }

  int acquire()
  {
    if (mutex_.tryacquire() == 0) {
      on_acquired(Clock::now(), 0, false);
      return 0;
    }
    const Clock::time_point start = Clock::now();
    const int result = mutex_.acquire();
    if (result == 0) {
      const Clock::time_point now = Clock::now();
      on_acquired(now, to_nanos(now - start), true);
    }
    return result;
  }

  int tryacquire()
  {
    const int result = mutex_.tryacquire();
    if (result == 0) {
      on_acquired(Clock::now(), 0, false);
      ++holder_->try_acquires;
    } else {
      ++try_failures_;
    }
    return result;
  }

  int release()
  {
    holder_->hold.add(to_nanos(Clock::now() - hold_start_));
    return mutex_.release();
  }

  int remove() { return mutex_.remove(); }

  // Used by ACE_Condition. A condition wait bypasses acquire()/release(),
  // so its blocked time is counted as hold time.
  ACE_thread_mutex_t& lock() { return mutex_.lock(); }
  const ACE_thread_mutex_t& lock() const { return mutex_.lock(); }

  /// Log stats since creation and since the previous report. Caller must not hold the lock.
  void log_report(const char* reason)
  {
    Report current;
    {
      // Take the raw mutex so the report isn't counted in the stats.
      ACE_Guard<ACE_Thread_Mutex> g(mutex_);
      current.stats = stats_;
      for (StatsMap::iterator it = stats_.begin(); it != stats_.end(); ++it) {
        it->second.wait.interval_max = 0;
        it->second.hold.interval_max = 0;
      }
    }
    current.time = Clock::now();
    current.busy = DCPS::thread_busy_snapshot();
    current.try_failures = try_failures_.load();

    ACE_Guard<ACE_Thread_Mutex> g(report_mutex_);
    log_section(reason, "cumulative", current, initial_, false);
    log_section(reason, "interval", current, previous_, true);
    previous_ = current;
  }

private:
  struct Report {
    Report() : try_failures(0) {}

    Clock::time_point time;
    StatsMap stats;
    DCPS::ThreadBusySnapshot busy;
    std::uint64_t try_failures;
  };

  void log_section(const char* reason, const char* kind,
                   const Report& current, const Report& base, bool interval) const
  {
    const Nanos window = to_nanos(current.time - base.time);
    ACE_DEBUG((LM_INFO, "(%P|%t) DiscoveryMutex %@ %C %C: window_us %.3f threads %B try_failures %Q\n",
               this, reason, kind, window / 1e3, current.stats.size(),
               static_cast<ACE_UINT64>(current.try_failures - base.try_failures)));

    for (StatsMap::const_iterator it = current.stats.begin(); it != current.stats.end(); ++it) {
      ThreadStats s = it->second;
      const StatsMap::const_iterator prev = base.stats.find(it->first);
      if (prev != base.stats.end()) {
        s.subtract(prev->second);
      }

      const DCPS::ThreadBusySample* busy = find_busy(current.busy, it->first);
      const DCPS::ThreadBusySample* busy_base = find_busy(base.busy, it->first);
      const Nanos busy_ns = busy ? busy->busy_ns - (busy_base ? busy_base->busy_ns : 0) : 0;
      if (interval && !s.acquires && !(busy && busy->labeled && busy_ns)) {
        continue;
      }

      const char* const role = s.role.c_str();
      ACE_DEBUG((LM_INFO, "(%P|%t) DiscoveryMutex %@ %C %C %C: acquires %Q contended %Q try_acquires %Q "
                 "wait_us %.3f wait_max_us %.3f hold_us %.3f hold_max_us %.3f\n",
                 this, reason, kind, role,
                 static_cast<ACE_UINT64>(s.acquires),
                 static_cast<ACE_UINT64>(s.contended),
                 static_cast<ACE_UINT64>(s.try_acquires),
                 s.wait.total / 1e3, (interval ? s.wait.interval_max : s.wait.max) / 1e3,
                 s.hold.total / 1e3, (interval ? s.hold.interval_max : s.hold.max) / 1e3));

      if (busy && busy->labeled) {
        const Nanos cpu_ns = (busy->cpu_ns >= 0 && busy_base && busy_base->cpu_ns >= 0) ?
          busy->cpu_ns - busy_base->cpu_ns : busy->cpu_ns;
        const Nanos runq_ns = (busy->runq_ns >= 0 && busy_base && busy_base->runq_ns >= 0) ?
          busy->runq_ns - busy_base->runq_ns : busy->runq_ns;
        ACE_DEBUG((LM_INFO, "(%P|%t) DiscoveryMutex %@ %C %C %C: busy_us %.3f outside_us %.3f busy_pct %.2f "
                   "cpu_us %.3f runq_us %.3f\n",
                   this, reason, kind, role, busy_ns / 1e3,
                   (busy_ns - s.wait.total - s.hold.total) / 1e3,
                   window ? 100.0 * busy_ns / window : 0.0,
                   cpu_ns / 1e3, runq_ns / 1e3));
      }

      ACE_DEBUG((LM_INFO, "(%P|%t) DiscoveryMutex %@ %C %C %C: wait_hist%C\n",
                 this, reason, kind, role, s.wait.hist.to_string().c_str()));
      ACE_DEBUG((LM_INFO, "(%P|%t) DiscoveryMutex %@ %C %C %C: hold_hist%C\n",
                 this, reason, kind, role, s.hold.hist.to_string().c_str()));
    }
  }

  static const DCPS::ThreadBusySample* find_busy(const DCPS::ThreadBusySnapshot& snapshot, pid_t tid)
  {
    const DCPS::ThreadBusySnapshot::const_iterator it = snapshot.find(tid);
    return it == snapshot.end() ? 0 : &it->second;
  }

  static Nanos to_nanos(Clock::duration d)
  {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(d).count();
  }

  static pid_t current_tid()
  {
    static thread_local const pid_t tid = ACE_OS::thr_gettid();
    return tid;
  }

  // Called with mutex_ held, so stats_ needs no extra synchronization.
  void on_acquired(Clock::time_point now, Nanos waited, bool contended)
  {
    ThreadStats& s = stats_[current_tid()];
    if (s.role.empty()) {
      s.role = DCPS::thread_role();
    }
    ++s.acquires;
    if (contended) {
      ++s.contended;
    }
    s.wait.add(waited);
    holder_ = &s;
    hold_start_ = now;
  }

  ACE_Thread_Mutex mutex_;
  StatsMap stats_;
  ThreadStats* holder_;
  Clock::time_point hold_start_;
  std::atomic<std::uint64_t> try_failures_;
  std::atomic<bool> reporting_enabled_;

  ACE_Thread_Mutex report_mutex_;
  Report initial_;
  Report previous_;

  DiscoveryMutex(const DiscoveryMutex&);
  DiscoveryMutex& operator=(const DiscoveryMutex&);
};

#endif

} // namespace RTPS
} // namespace OpenDDS

OPENDDS_END_VERSIONED_NAMESPACE_DECL

#endif
