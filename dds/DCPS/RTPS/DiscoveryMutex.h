/*
 * Distributed under the OpenDDS License.
 * See: http://www.opendds.org/license.html
 */

#ifndef OPENDDS_DCPS_RTPS_DISCOVERYMUTEX_H
#define OPENDDS_DCPS_RTPS_DISCOVERYMUTEX_H

#include <dds/Versioned_Namespace.h>

#include <ace/Thread_Mutex.h>

#ifdef OPENDDS_DISCOVERY_PROFILING
#  ifndef ACE_HAS_CPP11
#    error "OPENDDS_DISCOVERY_PROFILING requires C++11"
#  endif
#  include <ace/Log_Msg.h>
#  include <ace/OS_NS_Thread.h>

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

#ifndef OPENDDS_DISCOVERY_PROFILING

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
    Histogram hist;

    Durations() : total(0), max(0) {}

    void add(Nanos ns)
    {
      total += ns;
      if (ns > max) {
        max = ns;
      }
      hist.add(ns);
    }
  };

  struct ThreadStats {
    std::uint64_t acquires; // successful acquire() and tryacquire()
    std::uint64_t contended; // acquire() calls that had to block
    std::uint64_t try_acquires; // successful tryacquire()
    Durations wait;
    Durations hold;

    ThreadStats() : acquires(0), contended(0), try_acquires(0) {}
  };

  typedef std::map<pid_t, ThreadStats> StatsMap;

  explicit DiscoveryMutex(const ACE_TCHAR* name = 0)
    : mutex_(name)
    , created_(Clock::now())
    , holder_(0)
    , try_failures_(0)
  {}

  ~DiscoveryMutex()
  {
    log_stats();
  }

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

  /// Caller must hold the lock or otherwise guarantee no concurrent access.
  void log_stats() const
  {
    const Nanos lifetime = to_nanos(Clock::now() - created_);
    ACE_DEBUG((LM_INFO, "(%P|%t) DiscoveryMutex %@ stats: lifetime_us %.3f threads %B try_failures %Q\n",
               this, lifetime / 1e3, stats_.size(),
               static_cast<ACE_UINT64>(try_failures_.load())));
    for (StatsMap::const_iterator it = stats_.begin(); it != stats_.end(); ++it) {
      const ThreadStats& s = it->second;
      ACE_DEBUG((LM_INFO, "(%P|%t) DiscoveryMutex %@ tid %d: acquires %Q contended %Q try_acquires %Q "
                 "wait_total_us %.3f wait_max_us %.3f hold_total_us %.3f hold_max_us %.3f\n",
                 this, static_cast<int>(it->first),
                 static_cast<ACE_UINT64>(s.acquires),
                 static_cast<ACE_UINT64>(s.contended),
                 static_cast<ACE_UINT64>(s.try_acquires),
                 s.wait.total / 1e3, s.wait.max / 1e3,
                 s.hold.total / 1e3, s.hold.max / 1e3));
      ACE_DEBUG((LM_INFO, "(%P|%t) DiscoveryMutex %@ tid %d: wait_hist%C\n",
                 this, static_cast<int>(it->first), s.wait.hist.to_string().c_str()));
      ACE_DEBUG((LM_INFO, "(%P|%t) DiscoveryMutex %@ tid %d: hold_hist%C\n",
                 this, static_cast<int>(it->first), s.hold.hist.to_string().c_str()));
    }
  }

  /// Caller must hold the lock.
  const StatsMap& stats() const { return stats_; }

private:
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
    ++s.acquires;
    if (contended) {
      ++s.contended;
    }
    s.wait.add(waited);
    holder_ = &s;
    hold_start_ = now;
  }

  ACE_Thread_Mutex mutex_;
  const Clock::time_point created_;
  StatsMap stats_;
  ThreadStats* holder_;
  Clock::time_point hold_start_;
  std::atomic<std::uint64_t> try_failures_;

  DiscoveryMutex(const DiscoveryMutex&);
  DiscoveryMutex& operator=(const DiscoveryMutex&);
};

#endif

} // namespace RTPS
} // namespace OpenDDS

OPENDDS_END_VERSIONED_NAMESPACE_DECL

#endif
