"""SPIFFS image roundtrip with content verification; never format on the device."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
from gate0 import ROOT, sha, run
from flash_ota0 import read_mac


def files(folder):
    return {p.relative_to(folder).as_posix(): sha(p.read_bytes())
            for p in folder.rglob('*') if p.is_file()}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action',choices=['prepare','install','restore'])
    parser.add_argument('--backup',type=Path,required=True)
    parser.add_argument('--mkspiffs',type=Path,required=True)
    parser.add_argument('--package',type=Path)
    parser.add_argument('--port')
    parser.add_argument('--expect-mac',default='44:1B:F6:84:DA:88')
    a=parser.parse_args()
    backup=a.backup.resolve()
    if backup.is_relative_to(ROOT):parser.error('Private files must remain outside the repository')
    original=backup/'assets.bin'
    record=json.loads((backup/'gate0.json').read_text())
    if sha(original.read_bytes())!=record['region_sha256']['assets']:
        raise RuntimeError('Original assets backup is not intact')
    version=subprocess.check_output([str(a.mkspiffs),'--version'],text=True)
    for requirement in ('SPIFFS_OBJ_NAME_LEN: 32','SPIFFS_OBJ_META_LEN: 4',
                        'SPIFFS_USE_MAGIC: 1','SPIFFS_USE_MAGIC_LENGTH: 1',
                        'SPIFFS_ALIGNED_OBJECT_INDEX_TABLES: 0'):
        if requirement not in version:raise RuntimeError('SPIFFS geometry mismatch: '+requirement)
    geometry=['-b','4096','-p','256','-s',str(0x700000)]
    merged=backup/'assets-harness.bin'
    proof=backup/'assets-roundtrip.json'
    if a.action=='prepare':
        if not a.package:parser.error('--package required')
        if proof.exists():parser.error('Use the existing verified image or a fresh backup directory')
        work=Path(tempfile.mkdtemp(prefix='assets-roundtrip-',dir=backup))
        extracted=work/'original';stage=work/'staged';verify=work/'verified'
        subprocess.run([str(a.mkspiffs),*geometry,'-u',extracted.as_posix(),original.as_posix()],check=True)
        before=files(extracted)
        if 'harness.pocket' in before:raise RuntimeError('Existing harness.pocket must not be overwritten')
        shutil.copytree(extracted,stage,dirs_exist_ok=True)
        shutil.copy2(a.package,stage/'harness.pocket')
        subprocess.run([str(a.mkspiffs),*geometry,'-c',stage.as_posix(),merged.as_posix()],check=True)
        subprocess.run([str(a.mkspiffs),*geometry,'-u',verify.as_posix(),merged.as_posix()],check=True)
        expected={**before,'harness.pocket':sha(a.package.read_bytes())}
        if files(verify)!=expected:raise RuntimeError('SPIFFS content roundtrip failed; DO NOT FLASH')
        proof.write_text(json.dumps(dict(original_sha256=sha(original.read_bytes()),merged_sha256=sha(merged.read_bytes()),
            files=expected,tool_sha256=sha(a.mkspiffs.read_bytes()),tool_version=version),indent=2))
        print('Verified all original files plus harness.pocket. Image ready; device unchanged.')
        return
    checked=json.loads(proof.read_text())
    if not a.port:parser.error('--port is required for physical writes')
    if read_mac(a.port)!=a.expect_mac.upper():raise RuntimeError('Board identity mismatch')
    if a.action=='install':
        if sha(merged.read_bytes())!=checked['merged_sha256']:raise RuntimeError('Staged image changed')
        # Refuse to overwrite assets uploaded since the backup.
        run(a.port,'verify_flash','0x900000',original)
        image=merged
    else:image=original
    run(a.port,'write_flash','0x900000',image)
    run(a.port,'verify_flash','0x900000',image)


if __name__=='__main__':main()
