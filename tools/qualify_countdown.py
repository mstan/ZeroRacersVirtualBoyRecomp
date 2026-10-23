"""Check the actual VS countdown, command stores and PCM with live IRQ timing.

No IRQ mask or success-counter injection is used. Both execution backends
cold-boot through the same original route and stop at the native fly-in exit.
"""
import argparse
import json
from pathlib import Path
import sys
import time
from versus_test_mod import versus_mod_args

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('framework','runtime','rom','route','out'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--players',type=int,choices=(2,3,4),default=4)
    p.add_argument('--port',type=int,default=15931)
    p.add_argument('--color-package',type=Path)
    p.add_argument('--window',action='store_true',help='Also measure normal muted SDL pacing')
    a=p.parse_args();a.out=a.out.resolve();a.out.mkdir(parents=True,exist_ok=False)
    sys.path.insert(0,str(a.framework.resolve()/'tools'));from cosim import Runner
    report={'passed':False,'players':a.players,'scope':__doc__,'backends':{}}
    try:
        for mode in ('native','interpreter'):
            folder=a.out/mode;folder.mkdir()
            extra=['--versus',str(a.players),*versus_mod_args(a.runtime)]
            if a.color_package:extra+=['--install-mod',str(a.color_package.resolve()),'--enable-mod','zero-racers.full-color:full-color']
            with Runner(a.runtime,a.rom,mode,a.port,folder/'profile',extra=extra,window=a.window) as r:
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
                begin=folder/'before-countdown.state';c.command('save_state',path=begin.as_posix())
                checkpoint=None
                for elapsed in range(1,193):
                    c.command('replay_frames',frames=1);status=c.command('game_status')
                    assert status['countdown']==192-elapsed and status['race_fields']==0,status
                    assert status['phase']==(2 if elapsed<192 else 3),status
                    if elapsed==64:
                        checkpoint=folder/'during-countdown.state';c.command('save_state',path=checkpoint.as_posix())
                    if elapsed in (1,32,47,48,49,64,79,80,95,96,97,127,128,143,144,145,175,176,191,192):
                        for seat in range(a.players):
                            for eye in range(2):
                                target=folder/f'field{elapsed:03}-seat{seat}-eye{eye}.png'
                                assert c.command('screenshot',game=1,seat=seat,eye=eye,path=target.as_posix())['ok']
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
                # Measured original CPU stores, not an adapter-derived target:
                # 10CF4 at +48/+96/+144, 10D20 at +192 from 10C70.
                expected=[{'value':5,'field':48},{'value':5,'field':96},{'value':5,'field':144},{'value':6,'field':192}]
                assert sounds==expected,sounds
                c.command('replay_frames',frames=8)
                (folder/'pcm.bin').write_bytes(r.snapshot(c.command('frame')['frame'],['audio'])['audio'])
                c.command('save_state',path=(folder/'after.state').as_posix())
                digest=c.command('state_digest')['digest']
                c.command('load_state',path=checkpoint.as_posix())
                for n in (32,32,32,32,8):c.command('replay_frames',frames=n)
                assert c.command('state_digest')['digest']==digest,'Countdown restore replay differs'
                assert r.snapshot(c.command('frame')['frame'],['audio'])['audio']==(folder/'pcm.bin').read_bytes(),'Countdown PCM restore differs'
                if a.color_package:
                    from PIL import Image
                    # The selection hue is unique at this grid in the 2P
                    # fixture; assert the actual overlay's pulse gaps in both
                    # eyes, rather than only inspecting a countdown integer.
                    if a.players==2:
                        for elapsed in (32,64,79,80,95,97,127,128,143,145,175,176,191):
                            visible=48<=elapsed<192 and elapsed%48<32
                            for seat in range(a.players):
                                for eye in range(2):
                                    with Image.open(folder/f'field{elapsed:03}-seat{seat}-eye{eye}.png') as im:
                                        gold=sum(r>g>b and g>2*b for r,g,b in im.crop((184,72,192,80)).convert('RGB').get_flattened_data())
                                        assert (gold>0)==visible,('Countdown visual pulse',elapsed,seat,eye,gold,visible)
                report['backends'][mode]={'first_frame':first_frame,'sounds':sounds,'timeline':timeline}
                if a.window and mode=='native':
                    c.command('load_state',path=begin.as_posix());started=time.perf_counter();c.run_frames(200)
                    seconds=time.perf_counter()-started;status=c.command('game_status')
                    assert status['phase']==3 and status['race_fields']==8,status
                    assert 3.2<seconds<4.8,('Normal 2P SDL pacing is not near hardware rate',seconds)
                    report['wall_pacing']={'fields':200,'seconds':seconds,'hardware_seconds':200*397824/20000000,'dummy_audio':True}
        assert report['backends']['native']==report['backends']['interpreter'],'Countdown timing differs across backends'
        for name in ('after.state','pcm.bin'):
            assert (a.out/'native'/name).read_bytes()==(a.out/'interpreter'/name).read_bytes(),name+' differs'
        report['passed']=True
    except Exception as error:report['error']=repr(error)
    (a.out/'report.json').write_text(json.dumps(report,indent=2))
    print(json.dumps({'passed':report['passed'],'error':report.get('error')},indent=2))
    return 0 if report['passed'] else 1

if __name__=='__main__':raise SystemExit(main())
