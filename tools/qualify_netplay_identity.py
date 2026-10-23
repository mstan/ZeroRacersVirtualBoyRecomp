"""Actual two-process compatibility refusal before the first game field.

Same source build and renderer plugin, different installed palette bytes must
be refused too. Assertions use driver exit tick, process result and preserved
native files; this is not compilation-based compatibility coverage.
"""
import argparse
from pathlib import Path
import json
import re
import sys
import time
import traceback
import zipfile

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('framework','runtime','rom','package','out'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--port',type=int,default=16131);p.add_argument('--wire-port',type=int,default=19300)
    a=p.parse_args();a.out=a.out.resolve();a.out.mkdir(parents=True,exist_ok=False)
    sys.path.insert(0,str(a.framework.resolve()/'tools'));from cosim import Runner
    changed=a.out/'changed-palette.vbmod'
    with zipfile.ZipFile(a.package) as source,zipfile.ZipFile(changed,'w',compression=zipfile.ZIP_DEFLATED) as dest:
        for name in source.namelist():
            data=source.read(name)
            if name=='palette.txt':data+=b'\n# qualification: different active package bytes\n'
            dest.writestr(name,data)
    report={'passed':False,'scope':__doc__,'cases':[]}
    try:
        for case in ('different-enabled-feature','same-plugin-different-assets'):
            folder=a.out/case;folder.mkdir();runners=[];saves=[]
            try:
                for slot in range(2):
                    profile=folder/f'peer-{slot}';profile.mkdir();save=profile/'protected.sav'
                    contents=bytes((i*17+slot*3)&255 for i in range(65536));save.write_bytes(contents);saves.append((save,contents))
                    extra=['--net-players','2','--net-slot',str(slot),'--net-session','102718105',
                        '--net-bind',f'127.0.0.1:{a.wire_port+slot}','--net-peer',f'127.0.0.1:{a.wire_port+1-slot}',
                        '--save',save.as_posix()]
                    if slot==0 or case=='same-plugin-different-assets':
                        package=a.package if slot==0 else changed
                        extra+=['--install-mod',package.resolve().as_posix(),'--enable-mod','zero-racers.full-color:full-color']
                    runners.append(Runner(a.runtime,a.rom,'native',a.port+slot,profile,extra=extra))
                for r in runners:r.client.command('continue')
                deadline=time.monotonic()+35
                while any(r.process.poll() is None for r in runners):
                    if time.monotonic()>deadline:raise TimeoutError('Mismatched peers did not refuse')
                    time.sleep(.01)
                for r in runners:
                    assert r.process.returncode==8,('Unexpected refusal result',r.process.returncode)
                    log=(r.directory/'stderr.log').read_text()
                    ticks=re.findall(r'vb_net: final tick=(\d+)',log)
                    assert ticks==['0'],('Mismatch admitted gameplay',ticks)
                    assert any(word in log for word in ('mod_set_mismatch','mod set differs','build, ROM, active features')),log[-1600:]
                report['cases'].append({'case':case,'passed':True,'final_ticks':[0,0]})
            finally:
                for r in runners:r.close()
                for save,contents in saves:
                    assert save.read_bytes()==contents and not list(save.parent.glob('protected.sav.*')),'Refusal touched native progress'
        report['passed']=True
    except Exception as error:report['error']=repr(error);report['traceback']=traceback.format_exc()
    (a.out/'report.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
    return 0 if report['passed'] else 1

if __name__=='__main__':raise SystemExit(main())
