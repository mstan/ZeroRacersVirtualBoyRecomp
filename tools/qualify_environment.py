"""Boundary comparisons against the stock native executable.

Positions and flags are parameterized at captured subroutine entry, including
real repair rectangles, surface responses and two-racer swept contacts. This
qualifies these kernels only, not complete races, camera timing or netplay.
Private binary fixtures must be written to an explicit non-repository folder.
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
    for name in ('framework','runtime','rom','fixtures','test','out'):
        p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--port',type=int,default=15931)
    p.add_argument('--kind',choices=('repair','surface','wall','contact','initialize'),action='append')
    a=p.parse_args();a.out=a.out.resolve();a.out.mkdir(parents=True,exist_ok=False)
    sys.path.insert(0,str(a.framework.resolve()/'tools'))
    from cosim import Runner
    layout=json.loads(subprocess.check_output([str(a.test),'--layout'],text=True))['cpu_prefix_bytes']
    rom=a.rom.read_bytes();cases=[]
    for course,selected in itertools.product(range(1,17),range(4)):
        cases.append(dict(kind='initialize',return_pc=0xfff1037c,course=course,selected=selected))
    # All sixteen authored rectangles, all four native handling profiles,
    # both inside/outside transitions and damage clamping at zero.
    for course,profile,inside in itertools.product(range(1,17),range(4),(False,True)):
        lo_x,hi_x,lo_z,hi_z=struct.unpack_from('<4i',rom,0xe35c4+course*16)
        cases.append(dict(kind='repair',return_pc=0xfff0f650,course=course,profile=profile,
            inside=inside,x=(lo_x+hi_x)//2 if inside else lo_x,z=(lo_z+hi_z)//2))
    for profile,damage in itertools.product(range(4),(0,1,799,800)):
        x0,x1,z0,z1=struct.unpack_from('<4i',rom,0xe35c4+11*16)
        cases.append(dict(kind='repair',return_pc=0xfff0f650,course=11,profile=profile,
            inside=True,x=(x0+x1)//2,z=(z0+z1)//2,damage=damage))
    for surface,inventory in itertools.product(range(14),(0,3)):
        cases.append(dict(kind='surface',return_pc=0xfff0f612,surface=surface,inventory=inventory))
    for delta in ((0,0,0),(500,0,0),(-500,0,0),(0,500,0),(0,-500,0),(0,0,500),(0,0,-500)):
        for profile in range(4):cases.append(dict(kind='wall',return_pc=0xfff0f62a,delta=delta,profile=profile))
    for seat_pair,delta,speed in itertools.product(((0,1),(0,3),(2,1),(3,0)),
            ((0,0,0),(8,0,8),(25,0,0),(0,30,0),(0,0,100)),((4000,1000),(1000,4000))):
        cases.append(dict(kind='contact',return_pc=0xfff0f5e0,seats=seat_pair,delta=delta,speed=speed))
    if a.kind:cases=[case for case in cases if case['kind'] in a.kind]
    report={'passed':False,'scope':__doc__,'cases':[]}
    try:
        with Runner(a.runtime,a.rom,'hybrid',a.port,a.out/'profile') as runner:
            c=runner.client;temporary=a.out/'temporary.state'
            def word(address,value):
                for i in range(4):c.command('write_ram',addr=address+i,val=(value>>(8*i))&255)
            def capture(prefix):
                c.command('save_state',path=temporary.as_posix())
                blob=temporary.read_bytes();assert blob[:8]==b'VBSTATE1'
                assert zlib.crc32(blob[112:])==struct.unpack_from('<I',blob,80)[0]
                cpu=blob[112:112+layout]
                (folder/(prefix+'.cpu')).write_bytes(b'ZRROUT01'+struct.pack('<II',layout,zlib.crc32(cpu))+cpu)
                (folder/(prefix+'.bin')).write_bytes(runner.memory(0x05000000))
            for i,case in enumerate(cases):
                kind=case['kind'];folder=a.out/f'{kind}-{i:03d}';folder.mkdir()
                fixture='human-routine' if kind=='initialize' else kind
                c.command('load_state',path=(a.fixtures/fixture/'before.state').resolve().as_posix())
                registers=c.command('get_registers');base=int(registers['gpr'][6],0)
                source=(a.fixtures/fixture/'before-wram.bin').read_bytes()
                if kind=='initialize':
                    c.command('pc_set',pc=0xfff100b6)
                    # FFF10090 initializes this loop counter before the
                    # selected pure setup block. Preserve that precondition.
                    c.command('set_gpr',index=6,val=0)
                    word(0x05001058,case['course']);word(0x05001100,case['selected'])
                    word(0x050010fc,struct.unpack_from('<I',rom,0xddd94+case['selected']*20)[0])
                elif kind=='repair':
                    word(0x05001058,case['course']);word(base,case['x']);word(base+8,case['z'])
                    word(base+0x60,case['profile']);word(base+0x38,case.get('damage',500))
                    word(0x05001148,int(not case['inside']))
                elif kind=='surface':
                    obj=0xd270+19*32
                    x,y,z=struct.unpack_from('<3i',source,obj);sector=struct.unpack_from('<I',source,obj+0x1c)[0]
                    for off,val in enumerate((x,y,z,x,y,z)):word(base+off*4,val)
                    word(base+0x3c,sector);word(base+0x70,case['inventory'])
                    # Isolate a single real model instance, with each native
                    # interaction class (model id 140 + class*8).
                    for j in range(19,39):word(0x0500d270+j*32+0x18,0)
                    word(0x05000000+obj+0x18,0x140+case['surface']*8)
                elif kind=='wall':
                    x,y,z=struct.unpack_from('<3i',source,base&65535)
                    for j,(v,d) in enumerate(zip((x,y,z),case['delta'])):
                        word(base+j*4,v+d);word(base+12+j*4,v+d)
                    word(base+0x60,case['profile'])
                else:
                    first,second=case['seats'];template=source[0xdcb0:0xdd50]
                    xyz=struct.unpack_from('<3i',template)
                    for seat in range(6):word(0x0500db70+seat*0xa0+0x30,0)
                    for seat,delta,speed in zip((first,second),((0,0,0),case['delta']),case['speed']):
                        actor=0x0500db70+seat*0xa0
                        for off in range(0,0xa0,4):word(actor+off,struct.unpack_from('<I',template,off)[0])
                        for j,(v,d) in enumerate(zip(xyz,delta)):
                            word(actor+j*4,v+d);word(actor+12+j*4,v+d)
                        word(actor+0x34,speed);word(actor+0x38,0);word(actor+0x68,0)
                    word(0x05001100,first)
                # Pure cartridge kernels do not own device interrupts. Whole
                # machine timing remains a separate qualification plane.
                c.command('psw_set',value=int(registers['psw'],0)|0x1000)
                capture('before');c.command('breakpoint',pc=case['return_pc']);c.command('continue')
                deadline=time.monotonic()+10
                while not c.command('execution_stats')['stopped']:
                    if time.monotonic()>deadline:raise TimeoutError(case)
                    time.sleep(.002)
                assert int(c.command('get_registers')['pc'],0)==case['return_pc']
                capture('after')
                result=subprocess.run([str(a.test),str(a.rom),str(folder/'before.cpu'),str(folder/'before.bin'),
                    str(folder/'after.cpu'),str(folder/'after.bin')],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,
                    text=True,creationflags=subprocess.CREATE_NO_WINDOW if sys.platform=='win32' else 0)
                (folder/'comparison.txt').write_text(result.stdout)
                report['cases'].append({**case,'passed':result.returncode==0})
                if result.returncode:raise AssertionError(f'{folder.name}: '+result.stdout[:2000])
            temporary.unlink()
        report['passed']=True
    except Exception as error:report['error']=str(error)
    (a.out/'report.json').write_text(json.dumps(report,indent=2))
    print(json.dumps({'passed':report['passed'],'cases':len(report['cases']),'error':report.get('error')},indent=2))
    return 0 if report['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
