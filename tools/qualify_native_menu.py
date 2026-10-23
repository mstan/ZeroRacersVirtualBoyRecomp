"""Enter local VS through the actual title menu, ready seats, and launch a race.

No networking is exercised. Every mode cold-boots the same build and ROM;
artifacts include the title addition, setup, split views and canonical replay.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import time
import traceback
from PIL import Image
from versus_test_mod import versus_mod_args

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('framework','runtime','rom','route','out'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--players',type=int,choices=(2,3,4),default=2)
    p.add_argument('--port',type=int,default=15931)
    p.add_argument('--color-package',type=Path)
    p.add_argument('--window',action='store_true',help='Also audit the real muted SDL texture (dummy devices)')
    a=p.parse_args();a.out=a.out.resolve();a.out.mkdir(parents=True,exist_ok=False)
    sys.path.insert(0,str(a.framework.resolve()/'tools'));from cosim import Runner
    report={'passed':False,'players':a.players,'backends':{}}
    try:
        for mode in ('native','interpreter'):
            folder=a.out/mode;folder.mkdir()
            extra=['--no-save',*versus_mod_args(a.runtime)]
            if a.color_package:extra+=['--install-mod',str(a.color_package.resolve()),'--enable-mod','zero-racers.full-color:full-color']
            with Runner(a.runtime,a.rom,mode,a.port,folder/'profile',extra=tuple(extra),window=a.window) as r:
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
                title_checks=[];reference_glyphs={};record_reference={};vs_blinks={}
                def title_capture(name,selected=False):
                    before=c.command('state_digest')['digest']
                    status=c.command('game_status');phase=status['menu']['title_phase']
                    for eye in range(2):
                        image=folder/f'{name}-eye{eye}.png';native=folder/f'{name}-native-eye{eye}.png'
                        assert c.command('screenshot',game=1,eye=eye,path=image.as_posix())['ok']
                        assert c.command('screenshot',presented=1,eye=eye,path=native.as_posix())['ok']
                        with Image.open(image) as shown,Image.open(native) as original:
                            assert shown.size==original.size==(384,224)
                            for box in ((0,0,384,88),(0,204,384,224)):
                                assert shown.crop(box).tobytes()==original.crop(box).tobytes(),'Logo/copyright changed'
                            for row in range(4):
                                # Ignore cursor flanks; all four original labels
                                # must retain their exact pixels and colors.
                                source=original.crop((160,96+24*row,224,112+24*row)).tobytes()
                                if row==3:
                                    if eye not in record_reference:record_reference[eye]=source
                                    if selected:source=record_reference[eye]
                                assert shown.crop((160,92+22*row,224,108+22*row)).tobytes()==source,('Native label changed',name,row)
                            body=shown.crop((161,180,223,196)).convert('RGB')
                            bbox=body.getbbox()
                            if not selected or phase>=8:assert bbox and bbox[3]-bbox[1]>=14,'VS still uses small/missing lettering'
                            vs_blinks.setdefault((status['menu']['highlight'],eye),set()).add(body.tobytes())
                            for dx,sx,sy in ((188,167,144),(197,192,168),(206,210,168),(215,174,168)):
                                key=(eye,dx)
                                # Native selection overlays alter the selected
                                # word's art. Compare with its unselected form.
                                if key not in reference_glyphs:reference_glyphs[key]=original.crop((sx,sy,sx+8,sy+16)).tobytes()
                                if not selected or phase>=8:
                                    assert shown.crop((dx,180,dx+8,196)).tobytes()==reference_glyphs[key],('VS native-letter shade/shape mismatch',name,eye,dx)
                            if a.color_package and eye==0:
                                assert any(r!=g or g!=b for r,g,b in shown.crop((70,24,316,72)).convert('RGB').get_flattened_data()),'Color title absent'
                    if a.window:
                        time.sleep(.05)
                        target=folder/f'{name}-window.png'
                        assert c.command('screenshot',host=1,path=target.as_posix())['ok']
                        with Image.open(target) as window,Image.open(folder/f'{name}-eye0.png') as left:
                            assert window.size==left.size==(384,224),'Single-eye window size differs'
                            assert window.convert('RGB').tobytes()==left.convert('RGB').tobytes(),'Window differs from the reviewed menu'
                    assert c.command('state_digest')['digest']==before,'Presentation mutated canonical state'
                    title_checks.append({'capture':name,'selected':selected,'phase':phase,'both_eyes':True,'native_art_and_spacing':True})
                for row in range(5):
                    assert c.command('game_status')['menu']['highlight']==(1 if row==4 else 0)
                    title_capture(f'title-row{row}',row==4)
                    phases=set()
                    for elapsed in range(256):
                        fields(1);phase=c.command('game_status')['menu']['title_phase']
                        if phase not in phases:
                            title_capture(f'title-row{row}-phase{phase:02}',row==4);phases.add(phase)
                        if len(phases)==16:break
                    assert len(phases)==16,('Full native blink cycle not exercised',row,phases)
                    if row<4:pulse(1024)
                for eye in range(2):
                    assert len(vs_blinks[0,eye])==1,'Unselected VS follows another native word blink'
                    assert len(vs_blinks[1,eye])==2,'Selected VS does not own its blink'
                status=c.command('game_status');assert status['menu']['highlight']==1,status
                c.command('save_state',path=(folder/'title.state').as_posix())
                for _ in range(4):pulse(2048)
                assert c.command('game_status')['menu']['highlight']==0
                c.command('load_state',path=(folder/'title.state').as_posix())
                assert c.command('game_status')['menu']['highlight']==1
                title_capture('title-restored',True)
                # Each stock choice still reaches its native screen. Restore
                # the same title snapshot rather than editing guest mode RAM.
                for choice in range(4):
                    c.command('load_state',path=(folder/'title.state').as_posix())
                    for _ in range(4-choice):pulse(2048)
                    pulse(4);fields(64)
                    assert c.command('game_status')['menu']['screen']==0,('Stock choice intercepted',choice)
                    assert c.command('screenshot',presented=1,path=(folder/f'stock-choice{choice}.png').as_posix())['ok']
                c.command('load_state',path=(folder/'title.state').as_posix())
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
                    'rematch':rematch,'changed_course':changed,'changed_digest':changed_digest,'title_checks':title_checks,'stock_choices':4}
        report['audits']=[]
        assert report['backends']['native']['changed_digest']==report['backends']['interpreter']['changed_digest']
        names=['title-vs.png','setup.png','after.state','after-rematch.state','after-change.state','canvas-eye0.png','canvas-eye1.png']
        names += [f'title-row{row}-eye{eye}.png' for row in range(5) for eye in range(2)]
        names += [f'title-row{row}-phase{phase:02}-eye{eye}.png' for row in range(5) for phase in range(16) for eye in range(2)]
        names += [f'title-restored-eye{eye}.png' for eye in range(2)]
        for name in names:
            left=(a.out/'native'/name).read_bytes();right=(a.out/'interpreter'/name).read_bytes()
            report['audits'].append({'name':name,'equal':left==right,'sha256':hashlib.sha256(left).hexdigest()})
            assert left==right,name+' native/portable mismatch'
        report['passed']=True
    except Exception as error:report['error']=str(error);report['traceback']=traceback.format_exc()
    (a.out/'report.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
    return 0 if report['passed'] else 1

if __name__=='__main__':raise SystemExit(main())
