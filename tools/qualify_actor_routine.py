"""Compare classified actor mechanics against real stock CPU invocations.

The fixtures vary machine identity, buttons, boost inventory and settings at a
captured routine boundary. This qualifies that adapter boundary, not selection,
course setup, collisions, repairs, a full race or multiplayer.
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
    for name in ('framework','runtime','rom','state','test','out'):
        p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--port',type=int,default=15931)
    a=p.parse_args();a.out=a.out.resolve();a.out.mkdir(parents=True,exist_ok=False)
    sys.path.insert(0,str(a.framework.resolve()/'tools'))
    from cosim import Runner
    layout=json.loads(subprocess.check_output([str(a.test),'--layout'],text=True))['cpu_prefix_bytes']
    report={'passed':False,'scope':__doc__,'cases':[]}
    try:
        with Runner(a.runtime,a.rom,'hybrid',a.port,a.out/'profile') as r:
            c=r.client
            source=a.state.resolve().as_posix();temporary=a.out/'temporary.state'
            def word(address,value):
                for i in range(4):c.command('write_ram',addr=address+i,val=(value>>(8*i))&255)
            def capture(name):
                c.command('save_state',path=temporary.as_posix())
                blob=temporary.read_bytes();assert blob[:8]==b'VBSTATE1' and zlib.crc32(blob[112:])==struct.unpack_from('<I',blob,80)[0]
                prefix=blob[112:112+layout]
                (folder/(name+'.cpu')).write_bytes(b'ZRROUT01'+struct.pack('<II',layout,zlib.crc32(prefix))+prefix)
                (folder/(name+'.bin')).write_bytes(r.memory(0x05000000))
            pads=[0,4,8,16,32,256,512,1024,2048,64,128,16384,32768,4|256,4|512,4|16,4|8]
            cases=[{'machine':machine,'pad':pad,'pressed':pad,'inventory':3,'vertical':0,'horizontal':0}
                   for machine,pad in itertools.product(range(4),pads)]
            cases += [{'machine':machine,'pad':pad,'pressed':0,'inventory':0,'vertical':vertical,'horizontal':horizontal}
                      for machine,pad,vertical,horizontal in itertools.product(range(4),(256|1024,512|2048),(0,1),(0,1))]
            for i,case in enumerate(cases):
                folder=a.out/f'case-{i:03d}';folder.mkdir()
                c.command('load_state',path=source)
                before=c.command('get_registers');base=int(before['gpr'][6],0)
                word(base+0x60,case['machine']);word(base+0x70,case['inventory'])
                word(0x05001024,(case['pad']|2)|(case['pressed']<<16))
                word(0x0500106c,case['vertical']);word(0x05001070,case['horizontal'])
                capture('before')
                c.command('breakpoint',pc=0xfff0f53c);c.command('continue')
                end=time.monotonic()+5
                while not c.command('execution_stats')['stopped']:
                    if time.monotonic()>end:raise TimeoutError('native actor return')
                    time.sleep(.002)
                assert int(c.command('get_registers')['pc'],0)==0xfff0f53c
                capture('after')
                args=[str(a.test),str(a.rom),str(folder/'before.cpu'),str(folder/'before.bin'),str(folder/'after.cpu'),str(folder/'after.bin')]
                result=subprocess.run(args,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,
                    creationflags=subprocess.CREATE_NO_WINDOW)
                (folder/'comparison.txt').write_text(result.stdout)
                report['cases'].append({**case,'passed':result.returncode==0})
                if result.returncode:raise AssertionError(f'case {i}: '+result.stdout)
            temporary.unlink()
        report['passed']=True
    except Exception as error:report['error']=str(error)
    (a.out/'report.json').write_text(json.dumps(report,indent=2))
    print(json.dumps({'passed':report['passed'],'cases':len(report['cases']),'error':report.get('error')},indent=2))
    return 0 if report['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
