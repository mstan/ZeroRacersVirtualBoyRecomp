"""Qualify per-view damage warning against original native leaf boundaries.

This is interrupt-isolated calculation coverage, not whole-frame timing.
"""
import argparse
import itertools
import json
from pathlib import Path
import struct
import subprocess
import sys
import time
import zlib

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('framework','runtime','rom','state','test','out'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--port',type=int,default=15941)
    a=p.parse_args();a.out=a.out.resolve();a.out.mkdir(parents=True,exist_ok=False)
    sys.path.insert(0,str(a.framework.resolve()/'tools'));from cosim import Runner
    layout=json.loads(subprocess.check_output([str(a.test),'--layout'],text=True))['cpu_prefix_bytes']
    report={'passed':False,'scope':__doc__,'cases':[]}
    try:
        with Runner(a.runtime,a.rom,'hybrid',a.port,a.out/'profile') as r:
            c=r.client;temporary=a.out/'temporary.state'
            def word(address,value):
                for j in range(4):c.command('write_ram',addr=address+j,val=(value>>(8*j))&255)
            def capture(name):
                c.command('save_state',path=temporary.as_posix());blob=temporary.read_bytes()
                assert blob[:8]==b'VBSTATE1' and zlib.crc32(blob[112:])==struct.unpack_from('<I',blob,80)[0]
                prefix=blob[112:112+layout]
                (folder/(name+'.cpu')).write_bytes(b'ZRROUT01'+struct.pack('<II',layout,zlib.crc32(prefix))+prefix)
                (folder/(name+'.bin')).write_bytes(r.memory(0x05000000))
                (folder/(name+'-video.bin')).write_bytes(r.memory(0,0x40000))
            for i,(damage,frame) in enumerate(itertools.product((600,601,700,701,750,751,799,800),range(16))):
                folder=a.out/f'case-{i:03d}';folder.mkdir();c.command('load_state',path=a.state.resolve().as_posix())
                registers=c.command('get_registers');assert int(registers['pc'],0)==0xfff18c18
                c.command('psw_set',value=int(registers['psw'],0)|0x1000)
                word((int(registers['gpr'][6],0)&0x07ffffff)+0x38,damage)
                previous=(0,damage,800)[i%3];bias=(0,80,-80)[(i//3)%3]
                word(0x05001168,previous);word(0x0500114c,bias);word(0x05001048,frame)
                capture('before');c.command('breakpoint',pc=0xfff0fb8e);c.command('continue')
                deadline=time.monotonic()+5
                while not c.command('execution_stats')['stopped']:
                    if time.monotonic()>deadline:raise TimeoutError('Original warning return')
                    time.sleep(.002)
                assert int(c.command('get_registers')['pc'],0)==0xfff0fb8e;capture('after')
                args=[a.test,a.rom,folder/'before.cpu',folder/'before.bin',folder/'after.cpu',folder/'after.bin',
                      folder/'before-video.bin',folder/'after-video.bin']
                result=subprocess.run([str(x) for x in args],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,
                    creationflags=subprocess.CREATE_NO_WINDOW if sys.platform=='win32' else 0)
                (folder/'comparison.txt').write_text(result.stdout)
                report['cases'].append(dict(damage=damage,frame=frame,previous=previous,bias=bias,passed=result.returncode==0))
                if result.returncode:raise AssertionError(f'case {i}: '+result.stdout)
            temporary.unlink()
        report['passed']=True
    except Exception as error:report['error']=repr(error)
    (a.out/'report.json').write_text(json.dumps(report,indent=2))
    print(json.dumps({'passed':report['passed'],'cases':len(report['cases']),'error':report.get('error')},indent=2))
    return 0 if report['passed'] else 1

if __name__=='__main__':raise SystemExit(main())
