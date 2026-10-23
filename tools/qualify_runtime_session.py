"""Exercise the real game host, canonical restore and independent seat inputs.

This is development qualification; it does not assert completed multiplayer UI,
audible mixing, full-race course coverage, or any networking coverage.
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
    p.add_argument('--state-folder',type=Path,help='Same-build native/interpreter checkpoint folder, for focused lifecycle checks')
    a=p.parse_args();a.out=a.out.resolve();a.out.mkdir(parents=True,exist_ok=False)
    sys.path.insert(0,str(a.framework.resolve()/'tools'))
    from cosim import Runner
    report={'passed':False,'scope':__doc__,'players':a.players,'backends':{}}
    try:
        for mode in ('native','interpreter'):
            folder=a.out/mode;folder.mkdir()
            native_save=folder/'protected-native.sav';native_bytes=bytes((i*29+7)&255 for i in range(65536))
            native_save.write_bytes(native_bytes)
            extra=('--versus',str(a.players),'--versus-course','11','--save',native_save.as_posix(),
                   '--rewind-frames','16','--rewind-interval','1')
            with Runner(a.runtime,a.rom,mode,a.port,folder/'profile',extra=extra) as r:
                c=r.client;remaining=2600
                assert r.memory(0x06000000)==bytes(65536),'Versus must not load native persistent progress'
                if a.state_folder:
                    c.command('load_state',path=(a.state_folder/mode/'before.state').resolve().as_posix());remaining=0
                else:
                    for segment in json.loads(a.route.read_text()):
                        count=min(remaining,segment['frames']);c.command('set_input',pad=segment.get('pad',0))
                        while count:
                            n=min(32,count);c.command('replay_frames',frames=n);count-=n;remaining-=n
                            # Start the moving audit at the first published
                            # racing field. Standing idle for a fixed boot
                            # frame can destroy a racer on a wall beforehand.
                            if c.command('game_status')['phase']==3:
                                count=remaining=0;break
                        if not remaining:break
                assert remaining==0
                status=c.command('game_status');assert status['players']==a.players and status['phase'] in (3,4),status
                c.command('game_inputs',**{f'pad{i}':16|(256 if i&1 else 512) for i in range(a.players)})
                c.command('save_state',path=(folder/'before.state').as_posix())
                before=c.command('game_status')
                for _ in range(3):c.command('replay_frames',frames=30)
                c.command('save_state',path=(folder/'after.state').as_posix())
                after=c.command('game_status');digest=c.command('state_digest')['digest']
                audio=r.snapshot(c.command('frame')['frame'],['audio'])['audio']
                (folder/'audio-after.bin').write_bytes(audio)
                for start,end in zip(before['actors'],after['actors']):
                    assert (start['x'],start['y'],start['z'])!=(end['x'],end['y'],end['z']),'Seat did not move with accelerator'
                c.command('load_state',path=(folder/'before.state').as_posix())
                for _ in range(3):c.command('replay_frames',frames=30)
                c.command('save_state',path=(folder/'replayed.state').as_posix())
                assert (folder/'after.state').read_bytes()==(folder/'replayed.state').read_bytes(),'Current-build restore/replay mismatch'
                assert c.command('state_digest')['digest']==digest
                assert r.snapshot(c.command('frame')['frame'],['audio'])['audio']==audio,'Emulated PCM replay mismatch'
                assert c.command('game_status')['actors']==after['actors']
                assert before['tick']+90==after['tick']
                # Published Start input keeps advancing the race; no modal pause.
                c.command('game_inputs',pad0=4096,pad1=16)
                c.command('replay_frames',frames=3)
                menu=c.command('game_status')
                assert menu['tick']==after['tick']+3 and (menu['phase']==4 or menu['can_pause'] is False)
                for eye in range(2):c.command('screenshot',eye=eye,path=(folder/f'eye-{eye}.png').as_posix())
                presented_digest=c.command('state_digest')['digest']
                for eye in range(2):
                    for seat in range(-1,a.players):
                        name=f'game-seat{seat}-eye{eye}.png'
                        capture=c.command('screenshot',game=1,seat=seat,eye=eye,path=(folder/name).as_posix())
                        assert capture['ok'],capture
                        with Image.open(folder/name) as image:
                            assert len(set(image.convert('RGB').getdata()))>1,'View is entirely blank'
                        assert (capture['width'],capture['height'])==((384,224) if seat>=0 else (384 if a.players==2 else 768,448))
                        again=folder/('repeat-'+name)
                        assert c.command('screenshot',game=1,seat=seat,eye=eye,path=again.as_posix())['ok']
                        assert (folder/name).read_bytes()==again.read_bytes(),'Repeated view advanced presentation'
                assert c.command('state_digest')['digest']==presented_digest,'Rendering modified authoritative state'
                assert (folder/'game-seat0-eye0.png').read_bytes()!=(folder/'game-seat1-eye0.png').read_bytes(),'Independent views are identical'
                # Exercise the actual rbengine history through the normal
                # machine loop, rather than calling it a rewind from a memcpy.
                c.command('rewind_enable',value=8);start=c.command('frame')['frame']
                c.run_frames(3);rewind_end=c.command('game_status');rewind_digest=c.command('state_digest')['digest']
                c.command('rewind',value=start+1);c.run_frames(2)
                assert c.command('game_status')['actors']==rewind_end['actors']
                assert c.command('state_digest')['digest']==rewind_digest,'Whole-session rewind branch mismatch'
                report['backends'][mode]={'before':before,'after':after,'menu':menu,'digest':digest,
                    'rewind_branch':True,'native_save_sandbox':True,'pcm_replay':True,
                    'execution':c.command('execution_stats')}
            assert native_save.read_bytes()==native_bytes,'Native save changed on exit'
            assert not list(folder.glob('protected-native.sav.*')),'Native save backup/temp side effect'
        # Same executable/build/features, different qualified execution paths.
        audits=[]
        names=['after.state','replayed.state','eye-0.png','eye-1.png','audio-after.bin']
        names += [f'game-seat{seat}-eye{eye}.png' for eye in range(2) for seat in range(-1,a.players)]
        for name in names:
            left=(a.out/'native'/name).read_bytes();right=(a.out/'interpreter'/name).read_bytes()
            equal=left==right
            audits.append({'artifact':name,'bytes':[len(left),len(right)],'equal':equal,
                           'hashes':[hashlib.sha256(x).hexdigest() for x in (left,right)]})
        report['byte_audits']=audits
        if not all(x['equal'] for x in audits):raise AssertionError('Runtime backend byte audit mismatch')
        report['passed']=True
    except Exception as error:report['error']=str(error)
    (a.out/'report.json').write_text(json.dumps(report,indent=2))
    print(json.dumps({'passed':report['passed'],'backends':list(report['backends']),'error':report.get('error')},indent=2))
    return 0 if report['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
