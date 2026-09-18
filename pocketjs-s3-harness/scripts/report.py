"""Evidence-first report: absent measurements are NOT-YET, never inferred PASS."""
import argparse
import json
from pathlib import Path
import statistics


def events(path):
    output=[]
    for line in path.read_text(errors='replace').splitlines():
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
    # Slope is descriptive, not proof of a leak. Compare identical scenario phases.
    for scenario in range(6):
        group=[s for s in samples if s['scenario']==scenario]
        if len(group)>2:
            t=[s['time_us']/1e6 for s in group]
            if max(t)>min(t):
                result[f'scenario_{scenario}_internal_bytes_per_second']=statistics.linear_regression(t,[s['internal_free'] for s in group]).slope
    return result


def evaluate(rows,receipt):
    problems=[]
    memory=memory_summary(rows)
    if not memory:problems.append('missing memory samples')
    else:
        for name,limit in [('internal_min',48*1024),('largest_internal_block',16*1024),('psram_free',1024*1024)]:
            if memory[name]<limit:problems.append(f'{name} below {limit}')
    if receipt.get('mode')!='pocket':problems.append('not build 3')
    if receipt.get('bin_bytes',1<<32)>4*1024*1024:problems.append('app exceeds 4 MiB or unmeasured')
    complete=[r for r in rows if r.get('type')=='run_complete']
    if not any(r.get('elapsed_us',0)>=7200_000000 and not r.get('fault',True) for r in complete):problems.append('missing successful 2h soak')
    if not any(r.get('type')=='wifi' and r.get('connected') for r in rows):problems.append('STA connection unverified')
    for test in ('static','golden','battery','input','restore','runaway'):
        if not any(r.get('type')=='invariant' and r.get('test')==test and r.get('pass') is True for r in rows):
            problems.append(test+' unverified')
    if any(r.get('type')=='guest_fault' or r.get('pass') is False for r in rows):problems.append('observed fault/failing check')
    timings=[r['timing']['irq_to_present'] for r in rows if r.get('type')=='scenario' and r.get('scenario')!=5 and r.get('timing',{}).get('irq_to_present',{}).get('count',0)>0]
    if not timings:problems.append('no correlated physical touch measurements')
    elif any(t['p95_upper_us']>50000 for t in timings):problems.append('partial touch p95 >50ms; repeat with double bounce')
    return dict(status='NOT-YET' if problems else 'PASS',causes=problems,memory=memory)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--log',type=Path,required=True)
    p.add_argument('--receipt',type=Path,required=True)
    p.add_argument('--baseline-log',type=Path)
    p.add_argument('--baseline-receipt',type=Path)
    p.add_argument('--out',type=Path,required=True)
    a=p.parse_args();receipt=json.loads(a.receipt.read_text());rows=events(a.log)
    result=evaluate(rows,receipt);result['receipt']=str(a.receipt)
    if a.baseline_receipt:
        base=json.loads(a.baseline_receipt.read_text())
        if base.get('mode')!='native' or base.get('profile')!=receipt.get('profile') or base.get('compiler')!=receipt.get('compiler'):
            result['causes'].append('builds 2/3 configurations are not comparable');result['status']='NOT-YET'
        else:result['app_delta_bytes']=receipt['bin_bytes']-base['bin_bytes']
    if a.baseline_log:
        result['build2_memory']=memory_summary(events(a.baseline_log))
    a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2))
    print(json.dumps(result,indent=2))


if __name__=='__main__':main()
