/*
 * Distributed under the OpenDDS License.
 * See: http://www.opendds.org/license.html
 */

#ifndef OPENDDS_DCPS_THREADROLE_H
#define OPENDDS_DCPS_THREADROLE_H

#include "dcps_export.h"
#include "PoolAllocator.h"

#if !defined (ACE_LACKS_PRAGMA_ONCE)
#  pragma once
#endif

OPENDDS_BEGIN_VERSIONED_NAMESPACE_DECL

namespace OpenDDS {
namespace DCPS {

#ifdef OPENDDS_DISCOVERY_PROFILING
/// Label the calling thread for profiling output. The OS thread id is appended.
OpenDDS_Dcps_Export void set_thread_role(const String& role);

/// Label of the calling thread, "other(tid N)" if never set.
OpenDDS_Dcps_Export const String& thread_role();
#endif

} // namespace DCPS
} // namespace OpenDDS

OPENDDS_END_VERSIONED_NAMESPACE_DECL

#endif
