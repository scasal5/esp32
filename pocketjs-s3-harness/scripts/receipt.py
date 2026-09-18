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
    binary=a.build/(description['project_name']+'.bin')
    files={}
    for path in [a.build/'sdkconfig',root/'dependencies.lock',binary,
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
                 source_sha256={str(f.relative_to(root)):sha(f) for folder in ('main','app','config','scripts','components') for f in (root/folder).rglob('*')
                                if f.is_file() and not {'__pycache__','node_modules','dist'}.intersection(f.parts)},
                 bin_bytes=binary.stat().st_size)
    if compiler: receipt['compiler_version']=subprocess.check_output([compiler,'--version'],text=True)
    for name in ('CMakeLists.txt','sdkconfig.defaults','partitions.csv'):
        receipt['source_sha256'][name]=sha(root/name)
    if a.mode=='baseline':
        receipt['product_sources']={str(f.relative_to(root.parent)):sha(f) for f in (root.parent/'firmware/main').rglob('*') if f.is_file() and f.suffix in ('.c','.cpp','.h')}
    receipt['dependencies_text']=(root/'dependencies.lock').read_text()
    if (a.build/'source-inputs.json').exists():
        receipt['verified_build_sources']=json.loads((a.build/'source-inputs.json').read_text())
    receipt['native_receipts']=[json.loads(f.read_text()) for f in (root/'.deps/pocketjs/hosts/esp-idf/components').glob('*/lib/esp32s3/build-receipt.json')]
    guest_package=root/'out/app/harness.pocket'
    if a.mode in ('pocket','headless') and guest_package.exists():
        receipt['guest_package']={'sha256':sha(guest_package),'bytes':guest_package.stat().st_size}
    (a.build/'receipt.json').write_text(json.dumps(receipt,indent=2))
    print('Receipt:',a.build/'receipt.json')


if __name__=='__main__':main()
