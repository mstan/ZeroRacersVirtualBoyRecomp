"""Compare the per-actor audio adapter with original stock ED56 invocations.

Vary all selected seats, distance/passing thresholds, speed boundaries and
camera quadrants. This qualifies native command/spatial parameter calculation;
it does not establish audible multi-player mixing or frame/IRQ timing.
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
    cases=[dict(slot=slot,speed=speed,x=distance,z=0,yaw=0,passing=True)
        for slot,speed,distance in itertools.product(range(4),(1599,1600,3199,3200),(0,100,300,600,1000,2399,2400,2401))]
    cases += [dict(slot=slot,speed=4000,x=x,z=z,yaw=yaw,passing=False)
        for slot,(x,z),yaw in itertools.product(range(4),((100,100),(100,-100),(-100,100),(-100,-100)),(0,900,1800,2700))]
    report={'passed':False,'scope':__doc__,'cases':[]}
    try:
        with Runner(a.runtime,a.rom,'hybrid',a.port,a.out/'profile') as r:
            c=r.client;temporary=a.out/'temporary.state'
            def word(address,value):
                for i in range(4):c.command('write_ram',addr=address+i,val=(value>>(8*i))&255)
            def capture(name):
                c.command('save_state',path=temporary.as_posix());blob=temporary.read_bytes()
                assert blob[:8]==b'VBSTATE1' and zlib.crc32(blob[112:])==struct.unpack_from('<I',blob,80)[0]
                prefix=blob[112:112+layout]
                (folder/(name+'.cpu')).write_bytes(b'ZRROUT01'+struct.pack('<II',layout,zlib.crc32(prefix))+prefix)
                (folder/(name+'.bin')).write_bytes(r.memory(0x05000000))
            for i,case in enumerate(cases):
                folder=a.out/f'case-{i:03d}';folder.mkdir();c.command('load_state',path=a.state.resolve().as_posix())
                registers=c.command('get_registers');c.command('psw_set',value=int(registers['psw'],0)|0x1000)
                selected=case['slot'];other=(selected+1)%4
                for seat in range(6):word(0x0500db70+seat*0xa0+0x30,257 if seat in (selected,other) else 0)
                word(0x05001000+0x100,selected);word(0x05001104,other if case['passing'] else 0xffffffff)
                word(0x050011f0,0);word(0x050011f8,0);word(0x05001200,case['yaw'])
                base=0x0500db70+other*0xa0
                word(base,case['x']);word(base+8,case['z']);word(base+0x34,case['speed'])
                capture('before');c.command('breakpoint',pc=0xfff0fa7e);c.command('continue')
                deadline=time.monotonic()+5
                while not c.command('execution_stats')['stopped']:
                    if time.monotonic()>deadline:raise TimeoutError('Original audio routine return')
                    time.sleep(.002)
                assert int(c.command('get_registers')['pc'],0)==0xfff0fa7e;capture('after')
                args=[a.test,a.rom,folder/'before.cpu',folder/'before.bin',folder/'after.cpu',folder/'after.bin']
                result=subprocess.run([str(x) for x in args],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,
                    creationflags=subprocess.CREATE_NO_WINDOW if sys.platform=='win32' else 0)
                (folder/'comparison.txt').write_text(result.stdout);report['cases'].append({**case,'passed':result.returncode==0})
                if result.returncode:raise AssertionError(f'case {i}: '+result.stdout)
            temporary.unlink()
        report['passed']=True
    except Exception as error:report['error']=repr(error)
    (a.out/'report.json').write_text(json.dumps(report,indent=2))
    print(json.dumps({'passed':report['passed'],'cases':len(report['cases']),'error':report.get('error')},indent=2))
    return 0 if report['passed'] else 1

if __name__=='__main__':raise SystemExit(main())
