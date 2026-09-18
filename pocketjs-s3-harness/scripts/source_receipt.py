"""Refuse to issue a build receipt if compilation inputs changed during build."""
import argparse
import hashlib
import json
from pathlib import Path


def snapshot(root,mode):
    folders=[root/'main',root/'components']
    if mode=='baseline':folders.append(root.parent/'firmware/main')
    files=[p for folder in folders for p in folder.rglob('*') if p.is_file()]
    files.extend(root/name for name in ('CMakeLists.txt','sdkconfig.defaults','partitions.csv',
        'pocket.host.json','scripts/prepare_quickjs.py','scripts/host_contract.py'))
    return {str(p.relative_to(root.parent)):hashlib.sha256(p.read_bytes()).hexdigest() for p in files}


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('action',choices=('snapshot','verify'))
    p.add_argument('--mode',required=True)
    p.add_argument('--out',type=Path,required=True)
    a=p.parse_args();root=Path(__file__).resolve().parents[1]
    current=snapshot(root,a.mode)
    if a.action=='snapshot':
        a.out.parent.mkdir(parents=True,exist_ok=True)
        a.out.write_text(json.dumps(current,indent=2));return
    previous=json.loads(a.out.read_text())
    changed=[name for name in previous.keys()|current.keys() if previous.get(name)!=current.get(name)]
    if changed:raise SystemExit('Sources changed during compilation; rebuild before flashing: '+', '.join(changed))


if __name__=='__main__':main()
