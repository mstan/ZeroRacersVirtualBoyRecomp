"""Enter local VS through the actual title menu, ready seats, and launch a race.

No networking is exercised. Every mode cold-boots the same build and ROM;
artifacts include the title addition, setup, split views and canonical replay.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys
from PIL import Image

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('framework','runtime','rom','route','out'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--players',type=int,choices=(2,3,4),default=2)
    p.add_argument('--port',type=int,default=15931)
    p.add_argument('--color-package',type=Path)
    a=p.parse_args();a.out=a.out.resolve();a.out.mkdir(parents=True,exist_ok=False)
    sys.path.insert(0,str(a.framework.resolve()/'tools'));from cosim import Runner
    report={'passed':False,'players':a.players,'backends':{}}
    try:
        for mode in ('native','interpreter'):
            folder=a.out/mode;folder.mkdir()
            extra=['--no-save']
            if a.color_package:extra+=['--install-mod',str(a.color_package.resolve()),'--enable-mod','zero-racers.full-color:full-color']
            with Runner(a.runtime,a.rom,mode,a.port,folder/'profile',extra=tuple(extra)) as r:
                c=r.client
                def fields(n):
                    while n:
                        k=min(n,32);c.command('replay_frames',frames=k);n-=k
                remaining=800
                for segment in json.loads(a.route.read_text()):
                    n=min(remaining,segment['frames']);c.command('set_input',pad=segment.get('pad',0));fields(n);remaining-=n
                    if not remaining:break
                c.command('set_input',pad=0);fields(8)
                assert c.command('game_status')['menu']['screen']==1,c.command('game_status')
                def pulse(pad,seat=0):
                    c.command('set_input',pad=pad if seat==0 else 0)
                    c.command('game_inputs',**{f'pad{seat}':pad});fields(4)
                    c.command('set_input',pad=0);c.command('game_inputs');fields(4)
                for _ in range(4):pulse(1024)
                status=c.command('game_status');assert status['menu']['highlight']==1,status
                assert c.command('screenshot',game=1,path=(folder/'title-vs.png').as_posix())['ok']
                pulse(4)
                assert c.command('game_status')['menu']['screen']==2,c.command('game_status')
                for _ in range(a.players-2):pulse(256)
                for i in range(1,a.players):pulse(4,i)
                status=c.command('game_status')
                assert status['menu']['players']==a.players and status['menu']['ready_mask']==((1<<a.players)-2),status
                assert c.command('screenshot',game=1,path=(folder/'setup.png').as_posix())['ok']
                c.command('save_state',path=(folder/'before-launch.state').as_posix())
                pulse(4096);fields(900)
                status=c.command('game_status');assert status['players']==a.players and status['phase']==3,status
                c.command('game_inputs',**{f'pad{i}':16 for i in range(a.players)})
                before=status;fields(90);after=c.command('game_status')
                for left,right in zip(before['actors'],after['actors']):
                    assert (left['x'],left['y'],left['z'])!=(right['x'],right['y'],right['z']),'Seat failed to accelerate'
                c.command('save_state',path=(folder/'after.state').as_posix());digest=c.command('state_digest')['digest']
                for eye in range(2):
                    assert c.command('screenshot',game=1,eye=eye,path=(folder/f'canvas-eye{eye}.png').as_posix())['ok']
                    with Image.open(folder/f'canvas-eye{eye}.png') as image:
                        assert image.size==(384 if a.players==2 else 768,448)
                        assert len(image.convert('RGB').getcolors(1000000))>1
                c.command('load_state',path=(folder/'before-launch.state').as_posix())
                pulse(4096);fields(900)
                c.command('game_inputs',**{f'pad{i}':16 for i in range(a.players)});fields(90)
                assert c.command('state_digest')['digest']==digest,'Setup restore/launch replay differs'
                # Finish by explicit sealed retirement, then exercise the real
                # native GP stack's reinitialization for rematch and new course.
                def finish():
                    c.command('game_inputs',**{f'command{i}':1 for i in range(a.players-1)});fields(1)
                    c.command('game_inputs');fields(4)
                    result=c.command('game_status');assert result['phase']==4,result
                    assert result['actors'][-1]['status']==5 and result['actors'][-1]['finish_field']==0,result
                finish();pulse(4096);fields(900)
                rematch=c.command('game_status');assert rematch['phase']==3 and rematch['players']==a.players,rematch
                c.command('save_state',path=(folder/'after-rematch.state').as_posix())
                finish();pulse(8)
                assert c.command('game_status')['menu']['screen']==2,c.command('game_status')
                pulse(1024);pulse(256) # course row, next native course table
                for i in range(1,a.players):pulse(4,i)
                c.command('save_state',path=(folder/'change-race.state').as_posix())
                assert c.command('game_status')['menu']['course']==2
                pulse(4096);fields(900)
                changed=c.command('game_status');assert changed['phase']==3 and changed['course']==2,changed
                changed_digest=c.command('state_digest')['digest']
                c.command('save_state',path=(folder/'after-change.state').as_posix())
                c.command('load_state',path=(folder/'change-race.state').as_posix());pulse(4096);fields(900)
                assert c.command('state_digest')['digest']==changed_digest,'Change-race setup restore/reinitialize differs'
                report['backends'][mode]={'status':after,'digest':digest,'setup_restore_replay':True,
                    'rematch':rematch,'changed_course':changed,'changed_digest':changed_digest}
        report['audits']=[]
        assert report['backends']['native']['changed_digest']==report['backends']['interpreter']['changed_digest']
        for name in ('title-vs.png','setup.png','after.state','after-rematch.state','after-change.state','canvas-eye0.png','canvas-eye1.png'):
            left=(a.out/'native'/name).read_bytes();right=(a.out/'interpreter'/name).read_bytes()
            report['audits'].append({'name':name,'equal':left==right,'sha256':hashlib.sha256(left).hexdigest()})
            assert left==right,name+' native/portable mismatch'
        report['passed']=True
    except Exception as error:report['error']=str(error)
    (a.out/'report.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
    return 0 if report['passed'] else 1

if __name__=='__main__':raise SystemExit(main())
