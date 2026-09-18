"""Evidence-first report: absent measurements are NOT-YET, never inferred PASS."""
import argparse
import json
from pathlib import Path
import statistics


# Device JSON: firmware must emit type=invariant test=<name>. A name in this
# list without a producer in main/*.c can never PASS.
DEVICE_INVARIANTS=('static','golden','battery','input','runaway','transfer_recovery')
# Host files (gate0/assets), not serial JSON from the guest.
HOST_GATES=('restore',)
# Computed from memory samples in this file; firmware must not invent it.
DERIVED_GATES=('memory_stability',)
REQUIRED_INVARIANTS=DEVICE_INVARIANTS
REQUIRED_INVARIANTS_ALL=DEVICE_INVARIANTS+HOST_GATES+DERIVED_GATES


def events(path):
    output=[]
    for line in path.read_text(errors='replace').splitlines():
        line=line.strip()
        # USB console can leave a prompt before a complete telemetry record.
        # Never extract arbitrary braces from logs or repair corrupted JSON.
        if line.startswith('ws183>'):
            line=line[len('ws183>'):].lstrip()
        try:
            item=json.loads(line)
            if isinstance(item,dict):output.append(item)
        except ValueError:pass
    return output


def memory_summary(rows):
    samples=[r for r in rows if r.get('type')=='memory']
    if not samples:return None
    keys=('internal_free','internal_min','largest_internal_block','psram_free','guest_heap_used')
    result={key:min(s[key] for s in samples) for key in keys}
    result['samples']=len(samples)
    # USB CDC drops whole lines when the host stops draining: IDF's VFS fails
    # fast after 50 ms. seq is monotonic from boot, so a hole means evidence was
    # lost, not that a sample was skipped. A slope fitted over an incomplete
    # series is not a leak result, so continuity is a precondition, not a note.
    numbers=[s['seq'] for s in samples if isinstance(s.get('seq'),int)]
    if len(numbers)!=len(samples):result['seq_gaps']=None
    elif numbers!=sorted(numbers):result['seq_gaps'],result['seq_restart']=None,True
    else:result['seq_gaps']=(numbers[-1]-numbers[0]+1)-len(numbers)
    # Slope is descriptive, not proof of a leak. Compare identical scenario phases.
    for scenario in range(6):
        group=[s for s in samples if s['scenario']==scenario]
        if len(group)>2:
            t=[s['time_us']/1e6 for s in group]
            if max(t)>min(t):
                result[f'scenario_{scenario}_internal_bytes_per_second']=statistics.linear_regression(t,[s['internal_free'] for s in group]).slope
    return result


def memory_stability(samples):
    """Equivalent-cycle internal heap: first vs last visit to scenario 0.

    Cycles are contiguous runs of scenario 0, so dwell time is not a constant.
    First-vs-last (not 0-vs-1) so a slow leak over a 2 h soak is visible.
    """
    ordered=sorted((s for s in samples if 'time_us' in s), key=lambda s:s['time_us'])
    cycles,cur,prev= [],None,None
    for item in ordered:
        scenario=item.get('scenario')
        if scenario==0:
            if prev!=0:
                cur=[]
                cycles.append(cur)
            cur.append(item)
        prev=scenario
    cycles=[c for c in cycles if len(c)>=2]
    if len(cycles)<2:return 'unverified'
    first=statistics.mean(s['internal_free'] for s in cycles[0])
    last=statistics.mean(s['internal_free'] for s in cycles[-1])
    return 'pass' if abs(first-last)<=4096 else 'fail'


def evaluate(rows,receipt):
    problems=[]
    memory=memory_summary(rows)
    if not memory:problems.append('missing memory samples')
    else:
        for name,limit in [('internal_min',48*1024),('largest_internal_block',16*1024),('psram_free',1024*1024)]:
            if memory[name]<limit:problems.append(f'{name} below {limit}')
        if memory.get('seq_restart'):problems.append('device restarted during capture')
        elif memory['seq_gaps'] is None:problems.append('memory sample continuity unverified: records carry no seq')
        elif memory['seq_gaps']:problems.append(f"{memory['seq_gaps']} memory samples lost on the console")
    if receipt.get('mode')!='pocket':problems.append('not build 3')
    if receipt.get('bin_bytes',1<<32)>4*1024*1024:problems.append('app exceeds 4 MiB or unmeasured')
    complete=[r for r in rows if r.get('type')=='run_complete']
    if not any(r.get('kind')=='soak' and r.get('sta_at_start') and r.get('elapsed_us',0)>=7200_000000 and not r.get('fault',True) for r in complete):problems.append('missing successful 2h soak')
    for wifi in (False,True):
        if not any(r.get('kind')=='smoke' and r.get('wifi_enabled') is wifi and
                   (not wifi or r.get('sta_at_start')) and r.get('ticks',0)>=10000 and not r.get('fault',True) for r in complete):
            problems.append('missing 10k smoke: '+('STA' if wifi else 'Wi-Fi never initialized'))
    if sum(r.get('type')=='reconnect' and r.get('was_connected') is True for r in rows)<11:
        problems.append('induced reconnects unverified')
    if not any(r.get('type')=='wifi' and r.get('connected') for r in rows):problems.append('STA connection unverified')
    for test in DEVICE_INVARIANTS:
        if not any(r.get('type')=='invariant' and r.get('test')==test and r.get('pass') is True for r in rows):
            problems.append(test+' unverified')
    if any(r.get('type')=='guest_fault' or r.get('pass') is False or
           (r.get('type')=='guest_start' and r.get('error')!=0) for r in rows):problems.append('observed fault/failing check')
    if not receipt.get('restore_verified') and not any(r.get('type')=='restore' and r.get('pass') is True for r in rows):
        problems.append('restore unverified')
    if memory:
        stability=memory_stability([r for r in rows if r.get('type')=='memory'])
        if stability=='unverified':problems.append('equivalent-cycle memory stability unverified')
        elif stability=='fail':problems.append('equivalent-cycle memory stability failed')
    else:
        problems.append('equivalent-cycle memory stability unverified')
    timings=[r['timing']['irq_to_present'] for r in rows if r.get('type')=='scenario' and r.get('scenario')!=5 and r.get('timing',{}).get('irq_to_present',{}).get('count',0)>0]
    if not timings:problems.append('no correlated physical touch measurements')
    elif any(t['p95_upper_us']>50000 for t in timings):problems.append('partial touch p95 >50ms; repeat with double bounce')
    return dict(status='NOT-YET' if problems else 'PASS',causes=problems,memory=memory)


def comparison_policy(receipt):
    prefixes=('CONFIG_SPIRAM','CONFIG_COMPILER_','CONFIG_FREERTOS_','CONFIG_ESP_DEFAULT_CPU_',
              'CONFIG_ESP_TASK_WDT','CONFIG_ESP_WIFI','CONFIG_LOG_','CONFIG_HARNESS_')
    values={}
    for line in receipt.get('sdkconfig','').splitlines():
        if line.startswith('# CONFIG_') and line.endswith(' is not set'):
            line=line[2:-11]+'=n'
        if '=' in line:
            key,value=line.split('=',1)
            if key.startswith(prefixes):values[key]=value
    return values


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--log',type=Path,required=True,action='append')
    p.add_argument('--receipt',type=Path,required=True)
    p.add_argument('--baseline-log',type=Path)
    p.add_argument('--baseline-receipt',type=Path)
    p.add_argument('--restore-proof',type=Path,help='Host restore evidence JSON; not a device invariant')
    p.add_argument('--out',type=Path,required=True)
    a=p.parse_args();receipt=json.loads(a.receipt.read_text());rows=[r for path in a.log for r in events(path)]
    if a.restore_proof:
        proof=json.loads(a.restore_proof.read_text())
        receipt['restore_verified']=bool(proof.get('flash_sha256') or proof.get('merged_sha256') or proof.get('pass') is True)
    result=evaluate(rows,receipt);result['receipt']=str(a.receipt)
    if a.baseline_receipt:
        base=json.loads(a.baseline_receipt.read_text())
        if (base.get('mode')!='native' or base.get('profile')!=receipt.get('profile') or
            base.get('compiler')!=receipt.get('compiler') or comparison_policy(base)!=comparison_policy(receipt) or
            not base.get('verified_build_sources') or base.get('verified_build_sources')!=receipt.get('verified_build_sources')):
            result['causes'].append('builds 2/3 configurations are not comparable');result['status']='NOT-YET'
        else:result['app_delta_bytes']=receipt['bin_bytes']-base['bin_bytes']
    if a.baseline_log:
        result['build2_memory']=memory_summary(events(a.baseline_log))
    a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2))
    print(json.dumps(result,indent=2))


if __name__=='__main__':main()
