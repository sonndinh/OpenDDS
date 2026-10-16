#!/usr/bin/env python3
'''Summarize the DiscoveryMutex profiling reports in an OpenDDS log file.

Requires a build with OPENDDS_DISCOVERY_MUTEX_PROFILING and logs produced with
DiscoveryMutexProfilingInterval set for the discovery config of interest.
'''

import argparse
import re
import sys
from collections import OrderedDict
from datetime import datetime, timedelta

LINE_RE = re.compile(
    r'^(?P<ts>\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d+)?.*?'
    r'DiscoveryMutex (?P<mutex>0x[0-9a-f]+) (?P<reason>\w+) (?P<kind>cumulative|interval)'
    r'(?: (?P<role>.+?\(tid (?P<tid>\d+)\)))?: (?P<body>.*)$')
HIST_RE = re.compile(r'<2\^(\d+)ns:(\d+)')
DOMAIN_RE = re.compile(r'SEDPTransportInst_[0-9a-f]+_(\d+)\(tid')
TS_FORMAT = '%Y-%m-%d %H:%M:%S.%f'


class Report:
    def __init__(self, mutex, reason, kind, ts, header):
        self.mutex = mutex
        self.reason = reason
        self.kind = kind
        self.ts = ts
        self.window_us = header.get('window_us', 0.0)
        self.try_failures = int(header.get('try_failures', 0))
        self.threads = OrderedDict()  # role -> dict of values

    def thread(self, role):
        return self.threads.setdefault(role, {})


def parse_pairs(body):
    tokens = body.split()
    values = {}
    for key, value in zip(tokens[::2], tokens[1::2]):
        try:
            values[key] = float(value)
        except ValueError:
            pass
    return values


def parse_log(path):
    reports = []
    current = {}  # (mutex, reason, kind) -> Report being filled
    with open(path, errors='replace') as f:
        for line in f:
            m = LINE_RE.search(line)
            if not m:
                continue
            key = (m['mutex'], m['reason'], m['kind'])
            body = m['body']
            if m['role'] is None:
                ts = datetime.strptime(m['ts'], TS_FORMAT) if m['ts'] else None
                report = Report(m['mutex'], m['reason'], m['kind'], ts, parse_pairs(body))
                current[key] = report
                reports.append(report)
                continue
            report = current.get(key)
            if report is None:
                continue
            values = report.thread(m['role'])
            values['tid'] = int(m['tid'])
            if body.startswith('wait_hist') or body.startswith('hold_hist'):
                name = body.split(None, 1)[0]
                values[name] = {int(b): int(c) for b, c in HIST_RE.findall(body)}
            else:
                values.update(parse_pairs(body))
    return reports


def hist_percentile(hist, pct):
    '''Upper bound in microseconds of the bucket containing the given percentile.'''
    total = sum(hist.values())
    if not total:
        return None
    target = total * pct / 100.0
    seen = 0
    for bucket in sorted(hist):
        seen += hist[bucket]
        if seen >= target:
            return (2 ** bucket) / 1e3 if bucket else 0.0
    return None


def fmt_us(value):
    if value is None:
        return '-'
    if abs(value) >= 1e6:
        return '{:.2f}s'.format(value / 1e6)
    if abs(value) >= 1e3:
        return '{:.2f}ms'.format(value / 1e3)
    return '{:.1f}us'.format(value)


def fmt_pct(num, den):
    if not den:
        return '-'
    return '{:.1f}%'.format(100.0 * num / den)


def short_role(role):
    return re.sub(r'_OPENDDS_SEDPTransportInst_[0-9a-f]+_(\d+)', r'_SEDP_\1', role)


def print_table(headers, rows):
    widths = [max(len(str(x)) for x in col) for col in zip(headers, *rows)]
    line = '  '.join('{:<%d}' % w for w in widths)
    print(line.format(*headers))
    print(line.format(*['-' * w for w in widths]))
    for row in rows:
        print(line.format(*[str(x) for x in row]))


def summarize_mutex(mutex, reports, show_timeline):
    cumulative = [r for r in reports if r.kind == 'cumulative']
    intervals = [r for r in reports if r.kind == 'interval']
    if not cumulative:
        return
    latest = cumulative[-1]

    domain = None
    for role in latest.threads:
        m = DOMAIN_RE.search(role)
        if m:
            domain = m.group(1)
            break

    reasons = sorted({r.reason for r in reports})
    periods = [r.window_us for r in intervals if r.reason == 'periodic']
    print('=' * 100)
    print('DiscoveryMutex {}  domain {}'.format(mutex, domain if domain is not None else '?'))
    print('  reports: {} cumulative, {} interval ({})'.format(
        len(cumulative), len(intervals), ', '.join(reasons)))
    print('  covered: {} (latest {} report{})'.format(
        fmt_us(latest.window_us), latest.reason,
        ' at ' + latest.ts.strftime(TS_FORMAT) if latest.ts else ''))
    if periods:
        print('  reporting interval: ~{}'.format(fmt_us(sorted(periods)[len(periods) // 2])))
    if 'final' not in reasons:
        print('  no final report (process killed or still running)')
    print('  try_failures: {}'.format(latest.try_failures))

    print('\nCumulative per thread:')

    def order(item):
        role, v = item
        busy = v.get('busy_us')
        hold_ratio = v.get('hold_us', 0.0) / busy if busy else -1.0
        return ('SEDPTransportInst' not in role, -hold_ratio)

    ordered = sorted(latest.threads.items(), key=order)
    rows = []
    for role, v in ordered:
        busy = v.get('busy_us')
        hold = v.get('hold_us', 0.0)
        wait = v.get('wait_us', 0.0)
        cpu = v.get('cpu_us')
        rows.append([
            short_role(role),
            int(v.get('acquires', 0)), int(v.get('contended', 0)),
            fmt_us(wait), fmt_us(v.get('wait_max_us')),
            fmt_us(hold), fmt_us(v.get('hold_max_us')),
            fmt_us(busy), fmt_us(v.get('outside_us')),
            fmt_pct(hold, busy) if busy else '-',
            fmt_pct(wait, busy) if busy else '-',
            '{:.2f}%'.format(v['busy_pct']) if 'busy_pct' in v else '-',
            fmt_us(cpu), fmt_us(v.get('runq_us')),
        ])
    print_table(['thread', 'acq', 'cont', 'wait', 'wait_max', 'hold', 'hold_max', 'busy',
                 'outside', 'hold/busy', 'wait/busy', 'util', 'cpu', 'runq'], rows)

    print('\nHold/wait distribution (cumulative, bucket upper bounds):')
    rows = []
    for role, v in ordered:
        for name in ('hold_hist', 'wait_hist'):
            hist = v.get(name, {})
            if not sum(hist.values()):
                continue
            rows.append([short_role(role), name.split('_')[0]] +
                        [fmt_us(hist_percentile(hist, p)) for p in (50, 90, 99, 100)])
    print_table(['thread', 'what', 'p50', 'p90', 'p99', 'max'], rows)

    notes = []
    for role, v in latest.threads.items():
        busy, cpu = v.get('busy_us'), v.get('cpu_us')
        if busy is not None and cpu is not None and cpu > busy * 1.05 and cpu - busy > 100:
            notes.append('{}: cpu exceeds busy by {} ({}); some work runs outside Event hooks'.format(
                short_role(role), fmt_us(cpu - busy), fmt_pct(cpu - busy, busy)))
        if role.startswith('other(') and busy is not None:
            notes.append('{}: labeled after its first lock acquisition, so lock stats show '
                         '"other" but busy time is tracked'.format(short_role(role)))
        if v.get('runq_us', 0) > 0.05 * (busy or 0) and v.get('runq_us', 0) > 1000:
            notes.append('{}: spent {} runnable but waiting for a CPU'.format(
                short_role(role), fmt_us(v['runq_us'])))
    for r in intervals:
        for role, v in r.threads.items():
            if v.get('outside_us', 0) < 0:
                notes.append('{}: negative outside time in an interval ({}); a hold started '
                             'before busy tracking began'.format(short_role(role), fmt_us(v['outside_us'])))
                break
    if notes:
        print('\nNotes:')
        for note in OrderedDict.fromkeys(notes):
            print('  - ' + note)

    if not show_timeline or not intervals:
        return

    print('\nInterval timeline (intervals with activity, t = interval end since mutex creation):')
    start = intervals[0].ts - timedelta(microseconds=intervals[0].window_us) if intervals[0].ts else None
    peaks = {}
    rows = []
    for r in intervals:
        for role, v in r.threads.items():
            busy = v.get('busy_us')
            if not v.get('acquires') and not busy:
                continue
            offset = (r.ts - start).total_seconds() if r.ts and start else None
            rows.append([
                '{:.1f}s'.format(offset) if offset is not None else '-',
                r.reason, short_role(role),
                int(v.get('acquires', 0)), int(v.get('contended', 0)),
                fmt_us(v.get('wait_us')), fmt_us(v.get('hold_us')),
                fmt_us(busy), fmt_pct(v.get('hold_us', 0.0), busy) if busy else '-',
                '{:.2f}%'.format(v['busy_pct']) if 'busy_pct' in v else '-',
                fmt_us(v.get('cpu_us')),
            ])
            if 'busy_pct' in v and v['busy_pct'] > peaks.get(role, (-1, None))[0]:
                peaks[role] = (v['busy_pct'], offset)
    print_table(['t', 'reason', 'thread', 'acq', 'cont', 'wait', 'hold', 'busy', 'hold/busy',
                 'util', 'cpu'], rows)

    if peaks:
        print('\nPeak interval utilization:')
        for role, (pct, offset) in peaks.items():
            print('  {}: {:.2f}% at t={}'.format(
                short_role(role), pct, '{:.1f}s'.format(offset) if offset is not None else '?'))


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('log', help='log file to analyze')
    parser.add_argument('--mutex', help='only report this DiscoveryMutex address')
    parser.add_argument('--no-timeline', action='store_true', help='omit the per-interval timeline')
    args = parser.parse_args()

    reports = parse_log(args.log)
    if not reports:
        sys.exit('No DiscoveryMutex reports found in ' + args.log)

    by_mutex = OrderedDict()
    for r in reports:
        if args.mutex and r.mutex != args.mutex:
            continue
        by_mutex.setdefault(r.mutex, []).append(r)
    if not by_mutex:
        sys.exit('No reports for DiscoveryMutex ' + args.mutex)

    for mutex, mutex_reports in by_mutex.items():
        summarize_mutex(mutex, mutex_reports, not args.no_timeline)
        print()


if __name__ == '__main__':
    main()
