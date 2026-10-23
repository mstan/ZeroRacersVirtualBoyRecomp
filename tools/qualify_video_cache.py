"""Compare cached and uncached frozen-view presentation in the real host.

Runs serially. Each side independently exercises native and interpreter paths,
both eyes per seat, the local canvas, repeated views, current-build restore,
rewind, PCM and native-save protection. This is not a performance measurement.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('framework','runtime','rom','route','out'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--players',type=int,choices=(2,3,4),required=True)
    p.add_argument('--color-package',type=Path)
    a=p.parse_args();a.out=a.out.resolve();a.out.mkdir(parents=True,exist_ok=False)
    report={'passed':False,'scope':__doc__,'players':a.players,'audits':[]}
    try:
        for enabled in (0,1):
            dest=a.out/str(enabled)
            command=[sys.executable,str(Path(__file__).with_name('qualify_runtime_session.py')),
                '--out',str(dest),'--players',str(a.players),'--video-cache',str(enabled)]
            for name in ('framework','runtime','rom','route'):command+=['--'+name,str(getattr(a,name).resolve())]
            if a.color_package:command+=['--color-package',str(a.color_package.resolve())]
            with (a.out/f'{enabled}.log').open('wb') as output:
                result=subprocess.run(command,stdout=output,stderr=subprocess.STDOUT)
            assert result.returncode==0,json.loads((dest/'report.json').read_text())
        for backend in ('native','interpreter'):
            left=a.out/'0'/backend;right=a.out/'1'/backend
            names=['after.state','replayed.state','audio-after.bin','eye-0.png','eye-1.png']
            names += [f'game-seat{seat}-eye{eye}.png' for eye in range(2) for seat in range(-1,a.players)]
            for name in names:
                x=(left/name).read_bytes();y=(right/name).read_bytes()
                report['audits'].append({'backend':backend,'artifact':name,'equal':x==y,
                    'sha256':[hashlib.sha256(v).hexdigest() for v in (x,y)]})
                assert x==y,(backend,name)
        report['passed']=True
    except Exception as error:report['error']=repr(error)
    (a.out/'report.json').write_text(json.dumps(report,indent=2))
    print(json.dumps({k:report.get(k) for k in ('passed','players','error')},indent=2))
    return 0 if report['passed'] else 1

if __name__=='__main__':raise SystemExit(main())
