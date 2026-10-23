"""Byte-audit portable and generated-native shared session execution.

This is a correctness comparison, not a performance benchmark. It compares
canonical state and emitted commands after every tested field and each eye's
derived pixels. It does not certify the host UI, audio playback or networking.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('portable','native','rom','out'):parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args();args.out=args.out.resolve();args.out.mkdir(parents=True,exist_ok=False)
    report={'passed':False,'scope':__doc__,'executables':{},'byte_audits':[]}
    try:
        for name in ('portable','native'):
            exe=getattr(args,name).resolve();dest=args.out/name
            result=subprocess.run([str(exe),str(args.rom.resolve()),str(dest)],stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,text=True,creationflags=subprocess.CREATE_NO_WINDOW if sys.platform=='win32' else 0)
            report['executables'][name]={'sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),
                                        'exit':result.returncode,'output':result.stdout}
            (args.out/(name+'.log')).write_text(result.stdout)
            if result.returncode:raise RuntimeError(name+': '+result.stdout)
        names=['session-trace.bin']+[f'p{seat}-eye{eye}.png' for seat in range(1,5) for eye in range(2)]
        for name in names:
            left=(args.out/'portable'/name).read_bytes();right=(args.out/'native'/name).read_bytes()
            hashes=[hashlib.sha256(x).hexdigest() for x in (left,right)]
            equal=left==right
            if (hashes[0]==hashes[1])!=equal:raise RuntimeError('Hash/byte equality audit failed')
            report['byte_audits'].append({'artifact':name,'bytes':[len(left),len(right)],'hashes':hashes,'equal':equal})
            if not equal:
                off=next((i for i,(a,b) in enumerate(zip(left,right)) if a!=b),min(len(left),len(right)))
                raise RuntimeError(f'{name}: first differing byte {off}')
        report['passed']=True
    except Exception as error:report['error']=str(error)
    (args.out/'report.json').write_text(json.dumps(report,indent=2))
    print(json.dumps({'passed':report['passed'],'byte_audits':len(report['byte_audits']),'error':report.get('error')},indent=2))
    return 0 if report['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
