import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p=argparse.ArgumentParser()
    p.add_argument('--build',type=Path,required=True)
    p.add_argument('--mode',required=True)
    p.add_argument('--profile',required=True)
    a=p.parse_args()
    root=Path(__file__).resolve().parents[1]
    description=json.loads((a.build/'project_description.json').read_text())
    files={}
    for path in [a.build/'sdkconfig',root/'dependencies.lock',a.build/'ws183_harness.bin',
                 root/'toolchain.lock.json',root/'pocket.host.json']:
        if path.exists(): files[str(path.relative_to(root))]=sha(path)
    native=[]
    for component in ('pocketjs_ui_core','pocketjs_render_rgb565'):
        folder=root/'.deps/pocketjs/hosts/esp-idf/components'/component/'lib/esp32s3'
        for path in folder.glob('*'):
            native.append(dict(file=str(path.relative_to(root)),sha256=sha(path)))
    compiler=description.get('c_compiler','')
    receipt=dict(mode=a.mode,profile=a.profile,commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
                 dirty=subprocess.check_output(['git','status','--porcelain'],cwd=root,text=True).splitlines(),
                 idf=os.environ.get('IDF_PATH'),compiler=compiler,files=files,native_archives=native,
                 sdkconfig=(a.build/'sdkconfig').read_text(),
                 source_sha256={str(f.relative_to(root)):sha(f) for folder in ('main','app','config','scripts') for f in (root/folder).rglob('*')
                                if f.is_file() and not {'__pycache__','node_modules','dist'}.intersection(f.parts)},
                 bin_bytes=(a.build/'ws183_harness.bin').stat().st_size)
    if compiler: receipt['compiler_version']=subprocess.check_output([compiler,'--version'],text=True)
    (a.build/'receipt.json').write_text(json.dumps(receipt,indent=2))
    print('Receipt:',a.build/'receipt.json')


if __name__=='__main__':main()
