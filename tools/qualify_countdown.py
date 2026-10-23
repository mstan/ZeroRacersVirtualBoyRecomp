"""Check the actual VS countdown, command stores and PCM with live IRQ timing.

No IRQ mask or success-counter injection is used. Both execution backends
cold-boot through the same original route and stop at the native fly-in exit.
"""
import argparse
import json
from pathlib import Path
import sys
import time

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('framework','runtime','rom','route','out'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--players',type=int,choices=(2,3,4),default=4)
    p.add_argument('--port',type=int,default=15931)
    a=p.parse_args();a.out=a.out.resolve();a.out.mkdir(parents=True,exist_ok=False)
    sys.path.insert(0,str(a.framework.resolve()/'tools'));from cosim import Runner
    report={'passed':False,'players':a.players,'scope':__doc__,'backends':{}}
    try:
        for mode in ('native','interpreter'):
            folder=a.out/mode;folder.mkdir()
            with Runner(a.runtime,a.rom,mode,a.port,folder/'profile',extra=('--versus',str(a.players))) as r:
                c=r.client;ready=False
                for segment in json.loads(a.route.read_text()):
                    c.command('set_input',pad=segment.get('pad',0));left=segment['frames']
                    while left:
                        n=min(16,left);c.command('replay_frames',frames=n);left-=n
                        if c.command('game_status')['phase']==1:ready=True;break
                    if ready:break
                assert ready,'Native race initialization did not arrive'
                c.command('set_input',pad=0);c.command('breakpoint',pc=0xfff10c70);c.command('continue')
                end=time.monotonic()+15
                while not c.command('execution_stats')['stopped']:
                    if time.monotonic()>end:raise TimeoutError('Native fly-in exit')
                    time.sleep(.01)
                assert int(c.command('get_registers')['pc'],0)==0xfff10c70
                first_frame=c.command('frame')['frame'];cursor=c.command('wtrace_stats')['total']
                c.command('breakpoint',pc=-1);sounds=[];timeline=[]
                for elapsed in range(1,151):
                    c.command('replay_frames',frames=1);status=c.command('game_status')
                    assert status['countdown']==150-elapsed and status['race_fields']==0,status
                    assert status['phase']==(2 if elapsed<150 else 3),status
                    stores=c.command('wtrace_dump',from_seq=cursor,limit=4096,addr_min=0x050000f3,addr_max=0x050000f4)
                    cursor=c.command('wtrace_stats')['total']
                    for store in stores['entries']:
                        value=store.get('val',store.get('value'))
                        if isinstance(value,str):value=int(value,0)
                        if value:
                            cycle=store.get('cycle',store.get('cycles'))
                            if isinstance(cycle,str):cycle=int(cycle,0)
                            sounds.append({'value':value,'field':cycle//397824-first_frame})
                    timeline.append((status['phase'],status['countdown'],status['tick']))
                assert sounds==[{'value':5,'field':0},{'value':5,'field':50},{'value':5,'field':100},{'value':6,'field':150}],sounds
                c.command('replay_frames',frames=8)
                (folder/'pcm.bin').write_bytes(r.snapshot(c.command('frame')['frame'],['audio'])['audio'])
                c.command('save_state',path=(folder/'after.state').as_posix())
                report['backends'][mode]={'first_frame':first_frame,'sounds':sounds,'timeline':timeline}
        assert report['backends']['native']==report['backends']['interpreter'],'Countdown timing differs across backends'
        for name in ('after.state','pcm.bin'):
            assert (a.out/'native'/name).read_bytes()==(a.out/'interpreter'/name).read_bytes(),name+' differs'
        report['passed']=True
    except Exception as error:report['error']=repr(error)
    (a.out/'report.json').write_text(json.dumps(report,indent=2))
    print(json.dumps({'passed':report['passed'],'error':report.get('error')},indent=2))
    return 0 if report['passed'] else 1

if __name__=='__main__':raise SystemExit(main())
