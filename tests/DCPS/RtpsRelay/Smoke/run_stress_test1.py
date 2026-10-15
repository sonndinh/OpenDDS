#!/usr/bin/env python3

# TODO:
#   n:m relationship between publisher and subscriber
#   Detect failures

import sys
import os
import signal
import atexit
import inspect
import subprocess
import argparse
import platform
import shutil
from random import randint, uniform, choice
from time import time, sleep, strftime
from heapq import heappush, heappop
from pathlib import Path
from shutil import rmtree

# From PerlACE
def concat_path(pathlist, path):
    return pathlist + os.pathsep + str(path)

def add_path(name, value):
    if name in os.environ:
        os.environ[name] = concat_path(os.environ[name], value)

def add_lib_path(value):
    # Set the library path supporting various platforms.
    for env in ('PATH', 'DYLD_LIBRARY_PATH', 'LD_LIBRARY_PATH', 'SHLIB_PATH'):
        add_path(env, value)

DDS_ROOT = Path(os.environ['DDS_ROOT'])

add_lib_path(DDS_ROOT / 'tests' / 'DCPS' / 'ConsolidatedMessengerIdl')
add_lib_path(DDS_ROOT / 'tests' / 'DCPS' / 'common')

dpm_path = DDS_ROOT / 'tools' / 'scripts' / 'dpm.py'

all_start = time()

def log_time(t):
    return format(t - all_start, '.3f')


def log(*args, **kw):
    kw.setdefault('flush', True)
    print(log_time(time()), *args, **kw)


class Call:
    def __init__(self, func, *args, **kw):
        self.func = func
        self.args = args
        self.kw = kw

    def __call__(self):
        return self.func(*self.args, **self.kw)

    def __repr__(self):
        func = self.func
        if inspect.ismethod(func):
            func = func.__func__
        return f'{func.__qualname__}{self.args}{self.kw}'


class Event:
    def __init__(self, call, at, priority=0):
        self.call = call
        self.at = at
        self.priority = priority

    def __call__(self):
        return self.call()

    def __repr__(self):
        t = log_time(self.at)
        return f'{repr(self.call)} @ {t} ({self.priority})'

    def _cmp(self):
        return (self.at, self.priority)

    def __lt__(self, other):
        return self._cmp() < other._cmp()


class EventLoop:
    def __init__(self):
        self.q = []

    def once(self, prefix, event):
        log('SCHEDULE', prefix, repr(event))
        heappush(self.q, event)

    def start_end(self, start_call, from_now, end_call, duration):
        start = time() + from_now
        self.once('START', Event(start_call, start, 0))
        self.once('END', Event(end_call, start + duration, 1))

    def run_events(self):
        while True:
            event = heappop(self.q)
            now = time()
            if event.at > now:
                sleep(event.at - now)
            event.call()


class ProcMan:
    def __init__(self):
        self.procs = {}
        atexit.register(self.int_all)

    def run(self, nick, *cmd, **env):
        env_dict = os.environ.copy()
        env_dict.update(env)
        proc = subprocess.Popen(cmd, preexec_fn=os.setsid, env=env_dict)
        log('RUN', nick, proc.pid, proc.args)
        self.procs[proc.pid] = (nick, proc)
        return proc

    def signal_group(self, pid, sig):
        nick, proc = self.procs[pid]
        del self.procs[pid]
        log(sig.name, nick, pid, proc.args)
        os.killpg(os.getpgid(pid), sig)

    def kill(self, pid):
        self.signal_group(pid, signal.SIGKILL)

    def int(self, pid):
        self.signal_group(pid, signal.SIGINT)

    def int_all(self):
        print('INT ALL START')
        for pid in list(self.procs.keys()):
            self.int(pid)
        print('INT ALL END')


class Sched:
    def __init__(self, **params):
        self.params = params

    def ready(self, event_loop, proc_man):
        self.event_loop = event_loop
        self.proc_man = proc_man

    def schedule(self):
        raise NotImplmentedError()


class ProcSched(Sched):
    def __init__(self, **params):
        super().__init__(**params)
        self.running = {}

    def ready(self, event_loop, proc_man):
        super().ready(event_loop, proc_man)
        self.uid_start = self.params['uid_start']
        self.uid_count = 0
        self.inc_uid_count(self.params['uid_count'])

    def inc_uid_count(self, count, start_now=False):
        for uid in range(self.uid_start, self.uid_start + count):
            self.uid_start += 1
            self.uid_count += 1
            self.schedule(uid, start_now=start_now)

    def start_prog(self, uid, run_no, running_for):
        raise NotImplmentedError()

    def start(self, uid, run_no, running_for, kill):
        self.running[uid] = self.start_prog(uid, run_no, running_for, kill).pid

    def end(self, uid, run_no, kill):
        pid = self.running.pop(uid)
        if kill:
            self.proc_man.kill(pid)
        else:
            self.proc_man.int(pid)
        self.schedule(uid, run_no + 1)

    def schedule(self, uid, run_no=1, start_now=False):
        running_for = randint(self.params['min_dur'], self.params['max_dur'])
        kill = self.params.get('kill_chance', 0) > uniform(0, 1)
        if start_now:
            start_in = 0
        else:
            start_in = randint(self.params['min_start'], self.params['max_start'])
        self.event_loop.start_end(
            Call(self.start, uid, run_no, running_for, kill),
            start_in,
            Call(self.end, uid, run_no, kill),
            running_for + 2,
        )


class DummySched(ProcSched):
    def start_prog(self, uid, run_no, running_for, kill):
        return self.proc_man.run(f'dummy-{uid}-{run_no}', '/usr/bin/env', 'sleep', '1000000')

class CommonSched(ProcSched):
    def __init__(self, **params):
        ProcSched.__init__(self, **params)
        self.participant_relay_uid = params.get('participant_relay_uid')


    def start_common_prog(self, uid, run_no, running_for, kill, nick, prog, *args, **env):
        relay_uid = self.participant_relay_uid or randint(1, 2)
        cmd = list(args)
        log_path = f'{nick}_{uid}.log'
        if run_no == 1 and os.path.exists(log_path):
            os.remove(log_path)
        cmd.extend(['-DCPSConfigFile', f'participant_relay_{relay_uid}.ini',
                    '-ORBDebugLevel', '1',
                    '-DCPSDebugLevel', '1',
                    '-ORBVerboseLogging', '1',
                    '-DCPSTransportDebugLevel', '1',
                    '-ORBLogFile', log_path,
                    '-DCPSPendingTimeout', '3',
                    '-s',
                    '-p', f'{uid}'])
        nick = f'{nick}-{uid}-{run_no}'
        return self.proc_man.run(nick, f'./{prog}', *cmd, **env)


class PublisherSched(CommonSched):
    def start_prog(self, uid, run_no, running_for, kill):
        return self.start_common_prog(uid, run_no, running_for, kill, 'publisher', 'publisher',
                                      '-OpenDDSAuthIdentityCA', 'file:./DPM/ca/identity/cert.pem',
                                      '-OpenDDSAuthIdentityCertificate', f'file:./DPM/identity/publisher_{uid}/cert.pem',
                                      '-OpenDDSAuthPrivateKey', f'file:./DPM/identity/publisher_{uid}/key.pem',
                                      '-OpenDDSAccessPermissionsCA', 'file:./DPM/ca/permissions/cert.pem',
                                      '-OpenDDSAccessGovernance', 'file:./DPM/signed/governance.xml.p7s',
                                      '-OpenDDSAccessPermissions', f'file:./DPM/signed/publisher_{uid}_permissions.xml.p7s',
                                      '-DCPSSecurity', '1',
                                      )

class SubscriberSched(CommonSched):
    def start_prog(self, uid, run_no, running_for, kill):
        return self.start_common_prog(uid, run_no, running_for, kill, 'subscriber', 'subscriber',
                                      '-OpenDDSAuthIdentityCA', 'file:./DPM/ca/identity/cert.pem',
                                      '-OpenDDSAuthIdentityCertificate', f'file:./DPM/identity/subscriber_{uid}/cert.pem',
                                      '-OpenDDSAuthPrivateKey', f'file:./DPM/identity/subscriber_{uid}/key.pem',
                                      '-OpenDDSAccessPermissionsCA', 'file:./DPM/ca/permissions/cert.pem',
                                      '-OpenDDSAccessGovernance', 'file:./DPM/signed/governance.xml.p7s',
                                      '-OpenDDSAccessPermissions', f'file:./DPM/signed/subscriber_{uid}_permissions.xml.p7s',
                                      '-DCPSSecurity', '1',
                                      )


class OneshotSched(Sched):
    def __init__(self, **params):
        super().__init__(**params)
        self.interval = params['interval']

    def ready(self, event_loop, proc_man):
        super().ready(event_loop, proc_man)
        self.schedule()

    def schedule(self):
        self.event_loop.once('ONESHOT', Event(Call(self.callback), time() + self.interval, 3))

    def callback(self):
        raise NotImplmentedError()


class LoadSched(OneshotSched):
    def __init__(self, other_scheds, **params):
        OneshotSched.__init__(self, **params)
        self.cpu_count = len(os.sched_getaffinity(0))
        self.log_file = Path('load.csv').open('w')
        self.other_scheds = other_scheds
        self.uid_count = params['uid_count']
        self.max_uid_count = params['max_uid_count']
        self.min_1min_load = params['min_1min_load']
        self.min_5min_load = params['min_5min_load']

    def callback(self):
        load_1min, load_5min, load_15min = [a / self.cpu_count for a in os.getloadavg()]
        if load_1min <= self.min_1min_load and load_5min <= self.min_5min_load:
            if self.max_uid_count > 0 and self.uid_count >= self.max_uid_count:
                log('Max count reached:', self.uid_count)
            else:
                inc = self.params['ramp_up']
                self.uid_count += inc
                log('Increase count to', self.uid_count)
                for sched in self.other_scheds:
                    sched.inc_uid_count(inc, start_now=True)
        else:
            log('Currently at load limit:', load_1min, load_5min)
        data = (log_time(time()), format(load_1min, '.2f'), format(load_5min, '.2f'), self.uid_count)
        print(*data)
        print(*data, sep=',', file=self.log_file)
        self.schedule()


class ExitSched(OneshotSched):

    def __init__(self, **params):
        OneshotSched.__init__(self, **params)

    def callback(self):
        sys.exit(0)

relay_config = {
    1: {
        'vertical_address': '4444',
        'horizontal_address': '127.0.0.1:11444',
        'meta_discovery_address': '127.0.0.1:8081',
    },
    2: {
        'vertical_address': '5444',
        'horizontal_address': '127.0.0.1:11544',
        'meta_discovery_address': '127.0.0.1:8082',
    }
}

class RelaySched(OneshotSched):
    # Prefix of the environment variables for the relay's application domain discovery config.
    DISCOVERY_ENV_PREFIX = 'OPENDDS_RTPS_DISCOVERY_APPLICATION_RTPS_DISCOVERY_'

    def __init__(self, **params):
        OneshotSched.__init__(self, **params)
        self.uid = params['uid']
        self.heap_profile = params.get('heap_profile', False)
        self.discovery_mutex_profile = params.get('discovery_mutex_profile', False)
        self.discovery_cpu_profile = params.get('discovery_cpu_profile', False)
        self.sedp_tid_file = params.get('sedp_tid_file')

    def start(self, proc_man):
        self.proc_man = proc_man
        return self.callback()

    def callback(self):
        discovery_profile = self.discovery_mutex_profile or self.discovery_cpu_profile
        log_prefix = 'relay_discovery' if discovery_profile else 'relay'
        log_path = f'{log_prefix}_{self.uid}.log'
        if os.path.exists(log_path):
            os.remove(log_path)
        valgrind = []
        if self.heap_profile:
            valgrind = [
                'valgrind', '--tool=massif', '--threshold=.0001',
                '--max-snapshots=200', '--detailed-freq=1', '--xtree-memory=full'
            ]
        config_file = 'stress_relay_discovery.ini' if discovery_profile else f'stress_relay_{self.uid}.ini'
        env = {}
        if self.discovery_mutex_profile:
            env[self.DISCOVERY_ENV_PREFIX + 'DISCOVERY_MUTEX_PROFILING_INTERVAL'] = '5'
        if self.discovery_cpu_profile:
            env[self.DISCOVERY_ENV_PREFIX + 'SEDP_THREAD_ID_FILE'] = str(self.sedp_tid_file)
        return self.proc_man.run(f'relay-{self.uid}',
                                 *valgrind,
                                 DDS_ROOT / 'bin' / 'RtpsRelay',
                                 '-DCPSConfigFile', config_file,
                                 '-ORBVerboseLogging', '1',
                                 '-ApplicationDomain', '42',
                                 '-RelayDomain', '0',
                                 '-IdentityCA', './DPM/ca/identity/cert.pem',
                                 '-PermissionsCA', './DPM/ca/permissions/cert.pem',
                                 '-IdentityCertificate', f'./DPM/identity/relay_{self.uid}/cert.pem',
                                 '-IdentityKey', f'./DPM/identity/relay_{self.uid}/key.pem',
                                 '-Governance', './DPM/signed/governance.xml.p7s',
                                 '-Permissions', f'./DPM/signed/relay_{self.uid}_permissions.xml.p7s',
                                 '-DCPSSecurity', '1',
                                 '-UserData', f'relay{self.uid}',
                                 '-VerticalAddress', relay_config[self.uid]['vertical_address'],
                                 '-HorizontalAddress', relay_config[self.uid]['horizontal_address'],
                                 '-MetaDiscoveryAddress', relay_config[self.uid]['meta_discovery_address'],
                                 '-LogDiscovery', '1',
                                 '-LogActivity', '1',
                                 '-LogRelayStatistics', '300',
                                 '-Lifespan', '300',
                                 '-Id', f'relay{self.uid}',
                                 '-PublishRelayStatus', '10',
                                 '-PublishRelayStatusLiveliness', '30',
                                 '-RestartDetection', '1',
                                 '-LogThreadStatus', '0',
                                 '-AdmissionControlQueueSize', '10',
                                 '-AdmissionControlQueueDuration', '5',
                                 '-AdmissionMaxParticipantsRange', '9500-10000',
                                 '-LogUtilizationChanges', '1',
                                 '-ORBLogFile', log_path,
                                 **env,
                                 )

class MonitorSched(OneshotSched):
    def __init__(self, **params):
        OneshotSched.__init__(self, **params)

    def callback(self):
        log_path = f'monitor.log'
        if os.path.exists(log_path):
            os.remove(log_path)
        return self.proc_man.run('monitor', './monitor',
                                 '-DCPSConfigFile', 'monitor.ini',
                                 '-DCPSDebugLevel', '1',
                                 '-ORBVerboseLogging', '1',
                                 '-DCPSTransportDebugLevel', '1',
                                 '-ORBLogFile', log_path,
                                 '-DCPSPendingTimeout', '3',
                                 )

def run_sim(*scheds, proc_man=None):
    event_loop = EventLoop()
    proc_man = proc_man or ProcMan()
    for sched in scheds:
        sched.ready(event_loop, proc_man)
    event_loop.run_events()


def check_cpu_profile_prereqs(flamegraph_dir):
    problems = []
    if not shutil.which('perf'):
        problems.append('perf is not in PATH')
    try:
        paranoid = int(Path('/proc/sys/kernel/perf_event_paranoid').read_text())
    except (OSError, ValueError):
        paranoid = None
    if paranoid is not None and paranoid > 2 and os.geteuid() != 0:
        problems.append(f'kernel.perf_event_paranoid is {paranoid}; it must be <= 2 to profile a thread')
    for name in ('stackcollapse-perf.pl', 'flamegraph.pl'):
        if not (flamegraph_dir / name).is_file():
            problems.append(f'{flamegraph_dir / name} not found (see --flamegraph-dir)')
    if problems:
        sys.exit('ERROR: cannot use --discovery-cpu-profile:\n  ' + '\n  '.join(problems))


def wait_for_sedp_tid(tid_file, relay_proc, timeout):
    deadline = time() + timeout
    while time() < deadline:
        if tid_file.exists():
            text = tid_file.read_text().strip()
            if text.isdigit():
                return int(text)
        if relay_proc.poll() is not None:
            raise RuntimeError(f'The relay exited with code {relay_proc.returncode} '
                               'before writing the SEDP thread id file')
        sleep(0.05)
    raise RuntimeError(f'The relay did not write {tid_file} within {timeout} seconds')


class CpuProfiler:
    def __init__(self, base_name, flamegraph_dir):
        self.data = Path(f'{base_name}.perf.data')
        self.folded = Path(f'{base_name}.folded')
        self.svg = Path(f'{base_name}.svg')
        self.flamegraph_dir = flamegraph_dir
        self.title = base_name
        self.proc = None

    def start(self, tid):
        # DWARF unwinding because OpenSSL and libc are built without frame pointers.
        # --no-buildid: perf's build-id pass unwinds every sample with addr2line and can take hours.
        cmd = ['perf', 'record', '-F', '499', '--call-graph', 'dwarf,32768',
               '--no-buildid', '--no-buildid-cache', '-t', str(tid), '-o', str(self.data)]
        log('RUN', 'perf', ' '.join(cmd))
        self.proc = subprocess.Popen(cmd)
        deadline = time() + 5
        while not self.data.exists():
            if self.proc.poll() is not None:
                raise RuntimeError(f'perf record exited with code {self.proc.returncode}')
            if time() > deadline:
                raise RuntimeError('perf record did not start recording')
            sleep(0.05)

    def stop(self):
        if self.proc and self.proc.poll() is None:
            log('SIGINT perf')
            self.proc.send_signal(signal.SIGINT)
            try:
                self.proc.wait(timeout=120)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait()

    def render(self):
        log('Rendering flame graph', self.svg)
        script = subprocess.Popen(
            ['perf', 'script', '-i', str(self.data), '--no-inline',
             '-F', 'comm,pid,tid,time,period,event,ip,sym,dso'],
            stdout=subprocess.PIPE)
        with open(self.folded, 'w') as folded:
            collapse = subprocess.Popen(
                ['perl', str(self.flamegraph_dir / 'stackcollapse-perf.pl')],
                stdin=script.stdout, stdout=folded)
        script.stdout.close()
        if collapse.wait() != 0 or script.wait() != 0:
            raise RuntimeError('Failed to convert the perf data to folded stacks')
        with open(self.folded) as folded, open(self.svg, 'w') as svg:
            subprocess.check_call(
                ['perl', str(self.flamegraph_dir / 'flamegraph.pl'),
                 '--title', self.title, '--width', '1800'],
                stdin=folded, stdout=svg)


if __name__ == '__main__':
    argp = argparse.ArgumentParser()
    argp.add_argument('count', type=int,
        help='Fixed count of client pairs. If using --ramp-up, ' +
          'then this is the max count with 0 meaning unlimited.')
    argp.add_argument('--max-time', type=int, default=0)
    argp.add_argument('-r', '--ramp-up', type=int, default=0,
        help='Gradually increase load in these increments')
    argp.add_argument('--dummy', action='store_true')
    profile_group = argp.add_mutually_exclusive_group()
    profile_group.add_argument('--heap-profile', action='store_true',
        help='Profile the relay heap with massif')
    profile_group.add_argument('--discovery-mutex-profile', action='store_true',
        help='Run one relay, connect all participants to relay 1, and log ' +
          'the discovery mutex statistics of its application domain')
    profile_group.add_argument('--discovery-cpu-profile', action='store_true',
        help='Run one relay, connect all participants to relay 1, and ' +
          'profile the CPU of its application domain SEDP thread with perf')
    argp.add_argument('--flamegraph-dir', type=Path,
        default=Path('~/codes/FlameGraph').expanduser(),
        help='FlameGraph checkout used by --discovery-cpu-profile')
    argp.add_argument('--tid-timeout', type=int, default=10,
        help='Seconds to wait for the relay to write the SEDP thread id')
    argp.add_argument('--delete-perf-data', action='store_true',
        help='Delete the perf data and folded stacks after rendering the flame graph')
    args = argp.parse_args()
    discovery_profile = args.discovery_mutex_profile or args.discovery_cpu_profile
    if args.discovery_cpu_profile:
        check_cpu_profile_prereqs(args.flamegraph_dir)

    # Generate keys, certificates, and permissions documents
    subprocess.check_call([dpm_path, '--init'])

    governance = '''<?xml version="1.0" encoding="UTF-8"?>
<dds xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:noNamespaceSchemaLocation="http://www.omg.org/spec/DDS-SECURITY/20170901/omg_shared_ca_permissions.xsd">
  <domain_access_rules>
    <domain_rule>
      <domains>
        <id>42</id>
      </domains>
      <allow_unauthenticated_participants>FALSE</allow_unauthenticated_participants>
      <enable_join_access_control>TRUE</enable_join_access_control>
      <discovery_protection_kind>NONE</discovery_protection_kind>
      <liveliness_protection_kind>NONE</liveliness_protection_kind>
      <rtps_protection_kind>ENCRYPT</rtps_protection_kind>
      <topic_access_rules>
        <topic_rule>
          <topic_expression>TestOnly*</topic_expression>
          <enable_discovery_protection>FALSE</enable_discovery_protection>
          <enable_liveliness_protection>FALSE</enable_liveliness_protection>
          <enable_read_access_control>FALSE</enable_read_access_control>
          <enable_write_access_control>FALSE</enable_write_access_control>
          <metadata_protection_kind>NONE</metadata_protection_kind>
          <data_protection_kind>NONE</data_protection_kind>
        </topic_rule>
        <topic_rule>
          <topic_expression>*</topic_expression>
          <enable_discovery_protection>FALSE</enable_discovery_protection>
          <enable_liveliness_protection>FALSE</enable_liveliness_protection>
          <enable_read_access_control>TRUE</enable_read_access_control>
          <enable_write_access_control>TRUE</enable_write_access_control>
          <metadata_protection_kind>NONE</metadata_protection_kind>
          <data_protection_kind>NONE</data_protection_kind>
        </topic_rule>
      </topic_access_rules>
    </domain_rule>
  </domain_access_rules>
</dds>

    '''
    with open('stress_governance.xml', 'w') as file:
        print(governance, file=file)
    subprocess.check_call([dpm_path, '--sign', 'stress_governance.xml', 'governance.xml.p7s'])

    for i in range(1,3):
        relay_name = f'relay_{i}'
        relay_sn = f'CN={relay_name}'
        subprocess.check_call([dpm_path, '--gencert',  relay_name, f'/{relay_sn}'])
        permissions_name = 'stress_permissions.xml'
        permissions = f'''<?xml version="1.0" encoding="utf-8"?>
<dds xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:noNamespaceSchemaLocation="http://www.omg.org/spec/DDS-SECURITY/20170901/omg_shared_ca_permissions.xsd">
  <permissions>
    <grant name="MessengerPermissions">
      <subject_name>{relay_sn}</subject_name>
      <validity>
        <!-- Format is CCYY-MM-DDThh:mm:ss[Z|(+|-)hh:mm] in GMT -->
        <not_before>2014-09-15T01:00:00</not_before>
        <not_after>2035-09-15T01:00:00</not_after>
      </validity>
      <allow_rule>
        <domains>
          <id>42</id>
        </domains>
      </allow_rule>
      <default>DENY</default>
    </grant>
  </permissions>
</dds>
        '''
        with open(permissions_name, 'w') as file:
            print(permissions, file=file)
        subprocess.check_call([dpm_path, '--sign', permissions_name, f'{relay_name}_permissions.xml.p7s'])

    for i in range(1,args.count + 1):
        publisher_name = f'publisher_{i}'
        publisher_sn = f'CN={publisher_name}'
        subprocess.check_call([dpm_path, '--gencert', publisher_name, f'/{publisher_sn}'])
        permissions = f'''<?xml version="1.0" encoding="utf-8"?>
<dds xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:noNamespaceSchemaLocation="http://www.omg.org/spec/DDS-SECURITY/20170901/omg_shared_ca_permissions.xsd">
  <permissions>
    <grant name="MessengerPermissions">
      <subject_name>{publisher_sn}</subject_name>
      <validity>
        <!-- Format is CCYY-MM-DDThh:mm:ss[Z|(+|-)hh:mm] in GMT -->
        <not_before>2024-09-15T01:00:00</not_before>
        <not_after>2035-09-15T01:00:00</not_after>
      </validity>
      <allow_rule>
        <domains>
          <id>42</id>
        </domains>
        <publish>
          <topics>
            <topic>*</topic>
          </topics>
          <partitions>
            <partition>{i}</partition>
          </partitions>
        </publish>
        <subscribe>
          <topics>
            <topic>*</topic>
          </topics>
          <partitions>
            <partition>{i}</partition>
          </partitions>
        </subscribe>
      </allow_rule>
      <default>DENY</default>
    </grant>
  </permissions>
</dds>
        '''
        with open(permissions_name, 'w') as file:
            print(permissions, file=file)
        subprocess.check_call([dpm_path, '--sign', permissions_name, f'{publisher_name}_permissions.xml.p7s'])

        subscriber_name = f'subscriber_{i}'
        subscriber_sn = f'CN={subscriber_name}'
        subprocess.check_call([dpm_path, '--gencert', subscriber_name, f'/{subscriber_sn}'])
        permissions = f'''<?xml version="1.0" encoding="utf-8"?>
<dds xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:noNamespaceSchemaLocation="http://www.omg.org/spec/DDS-SECURITY/20170901/omg_shared_ca_permissions.xsd">
  <permissions>
    <grant name="MessengerPermissions">
      <subject_name>{subscriber_sn}</subject_name>
      <validity>
        <!-- Format is CCYY-MM-DDThh:mm:ss[Z|(+|-)hh:mm] in GMT -->
        <not_before>2024-09-15T01:00:00</not_before>
        <not_after>2035-09-15T01:00:00</not_after>
      </validity>
      <allow_rule>
        <domains>
          <id>42</id>
        </domains>
        <publish>
          <topics>
            <topic>*</topic>
          </topics>
          <partitions>
            <partition>{i}</partition>
          </partitions>
        </publish>
        <subscribe>
          <topics>
            <topic>*</topic>
          </topics>
          <partitions>
            <partition>{i}</partition>
          </partitions>
        </subscribe>
      </allow_rule>
      <default>DENY</default>
    </grant>
  </permissions>
</dds>
        '''
        with open(permissions_name, 'w') as file:
            print(permissions, file=file)
        subprocess.check_call([dpm_path, '--sign', permissions_name, f'{subscriber_name}_permissions.xml.p7s'])

    common = dict(
        ramp_up=args.ramp_up,
        uid_start=1,
        uid_count=args.ramp_up if args.ramp_up else args.count,
    )
    if discovery_profile:
        common['participant_relay_uid'] = 1
    if args.dummy:
        main_scheds = [DummySched(**common, min_start=1, max_start=5, min_dur=1, max_dur=10)]
    else:
        main_scheds = [
            PublisherSched(**common,
                min_start=0, max_start=0,
                min_dur=100, max_dur=500, kill_chance=0.1
            ),
            SubscriberSched(**common,
                min_start=0, max_start=0,
                min_dur=2, max_dur=10, kill_chance=0.5
            ),
        ]
    other_scheds = [MonitorSched(interval=0)]
    if not args.discovery_cpu_profile:
        other_scheds.append(RelaySched(uid=1, interval=0, heap_profile=args.heap_profile,
                                       discovery_mutex_profile=args.discovery_mutex_profile))
    if not discovery_profile:
        other_scheds.append(RelaySched(uid=2, interval=0, heap_profile=args.heap_profile))
    if args.max_time:
        other_scheds.append(ExitSched(interval=args.max_time))
    if args.ramp_up:
        other_scheds.append(LoadSched(
            main_scheds, interval=10, min_1min_load=0.70, min_5min_load=0.80,
            max_uid_count=args.count, **common))

    proc_man = ProcMan()
    profiler = None
    tid_file = Path('sedp_tid.txt').resolve()
    tid_tmp_file = tid_file.with_name(tid_file.name + '.tmp')
    try:
        if args.discovery_cpu_profile:
            for stale in (tid_file, tid_tmp_file):
                stale.unlink(missing_ok=True)
            # Start the relay and attach perf before the participants generate load.
            relay = RelaySched(uid=1, interval=0, discovery_cpu_profile=True, sedp_tid_file=tid_file)
            relay_proc = relay.start(proc_man)
            try:
                tid = wait_for_sedp_tid(tid_file, relay_proc, args.tid_timeout)
                log('SEDP thread id', tid)
                profiler = CpuProfiler(f'sedp_cpu_{args.count}_{strftime("%Y%m%d_%H%M%S")}',
                                       args.flamegraph_dir)
                profiler.start(tid)
            except RuntimeError as e:
                sys.exit(f'ERROR: {e}')
        run_sim(*main_scheds, *other_scheds, proc_man=proc_man)
    finally:
        try:
            if profiler:
                # Stop perf first so that shutdown work isn't in the profile.
                profiler.stop()
            proc_man.int_all()
            if profiler:
                try:
                    profiler.render()
                    print('Flame graph:', profiler.svg)
                    if args.delete_perf_data:
                        profiler.data.unlink(missing_ok=True)
                        profiler.folded.unlink(missing_ok=True)
                except (RuntimeError, subprocess.CalledProcessError) as e:
                    print(f'ERROR: failed to render the flame graph: {e}', file=sys.stderr)
        finally:
            for scratch in (tid_file, tid_tmp_file):
                scratch.unlink(missing_ok=True)
