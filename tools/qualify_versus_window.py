"""Exercise the real SDL canvas and between-race whole-session F7/F8 UI.

Uses dummy video/audio and injected SDL events. This does not qualify a
physical controller driver, real graphics driver, audible output or netplay.
"""
import argparse
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import time
from versus_test_mod import versus_mod_args

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('framework','runtime','rom','route','out'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--players',type=int,choices=(2,3,4),default=4)
    p.add_argument('--port',type=int,default=15931)
    a=p.parse_args();a.out=a.out.resolve();a.out.mkdir(parents=True,exist_ok=False)
    sys.path.insert(0,str(a.framework.resolve()/'tools'));from debug_client import DebugClient
    from _imgio import load_png
    report={'passed':False,'players':a.players,'scope':__doc__,'checks':[]};process=client=None
    def check(label,value):
        report['checks'].append({'name':label,'passed':bool(value)})
        if not value:raise AssertionError(label)
    try:
        with socket.socket() as probe:
            if os.name=='nt':probe.setsockopt(socket.SOL_SOCKET,socket.SO_EXCLUSIVEADDRUSE,1)
            probe.bind(('127.0.0.1',a.port))
        profile=a.out/'profile';profile.mkdir()
        settings=profile/'settings.cfg';settings.write_text('fullscreen 0\naudio 1\nvolume 0\nscale 1\n')
        command=[str(a.runtime.resolve()),'--rom',str(a.rom.resolve()),'--no-launcher','--no-save','--paused',
                 '--config',str(settings),'--mods-dir',str(profile/'mods'),'--port',str(a.port),
                 '--state-file',str(profile/'quick.state'),
                 '--versus',str(a.players),'--versus-course','11','--rewind-frames','8','--rewind-interval','1']
        command+=versus_mod_args(a.runtime)
        env={k:v for k,v in os.environ.items() if not k.startswith('VBRECOMP_')}
        env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',SDL_RENDER_DRIVER='software',
                   SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT='0xffff/0xffff')
        with (a.out/'stdout.log').open('wb') as out,(a.out/'stderr.log').open('wb') as err:
            process=subprocess.Popen(command,cwd=a.out,env=env,stdout=out,stderr=err,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
            deadline=time.monotonic()+15
            while client is None:
                if process.poll() is not None:raise RuntimeError('Window runner exited')
                try:client=DebugClient(a.port)
                except OSError:
                    if time.monotonic()>deadline:raise
                    time.sleep(.02)
            c=client
            def key(scancode):
                for pressed in (1,0):c.command('host_input',device=0,value=scancode,pressed=pressed)
                time.sleep(.08)
            def picture(name,game=False):
                target=a.out/(name+'.png');c.command('screenshot',path=target.as_posix(),**({'game':1} if game else {'host':1}))
                return load_png(target)
            def snapshot(name):
                target=a.out/(name+'.state');c.command('save_state',path=target.as_posix());return target.read_bytes()
            remaining=2600
            for segment in json.loads(a.route.read_text()):
                n=min(remaining,segment['frames']);c.command('set_input',pad=segment.get('pad',0))
                while n:
                    k=min(n,32);c.command('replay_frames',frames=k);n-=k;remaining-=k
                if not remaining:break
            c.command('game_inputs',**{f'pad{i}':16 for i in range(a.players)});c.command('replay_frames',frames=30)
            time.sleep(.1)
            canvas=picture('race-canvas');check('SDL texture resized to the full local canvas',canvas[:2]==(384 if a.players==2 else 768,448))
            check('Window output equals the game-owned frozen canvas',canvas==picture('race-derived',True))
            key(64);key(65);check('F7 and F8 stay closed during a live race',c.command('state_ui')['mode']==0)
            c.command('game_inputs',**{f'command{i}':1 for i in range(a.players-1)});c.command('replay_frames',frames=1)
            check('Last-survivor results permit between-race browsers',c.command('game_status')['phase']==4)
            c.command('game_inputs');c.run_frames(4);before=snapshot('before-browser');frame=c.command('frame')['frame']
            width,height,pixels=picture('results-derived',True)
            key(64);check('F7 opens between races',c.command('state_ui')['mode']==1)
            key(22);check('F7 saves the whole current session',c.command('state_ui')['result']==1)
            slot=next((profile/'states').glob('*/slot-01.state'));check('Saved machine and game root are exact',slot.read_bytes()==before)
            thumb=Path(str(slot)+'.thumb').read_bytes();actual=[n[0]&0xffffff for n in struct.iter_unpack('<I',thumb[120:])]
            scale=min(128/width,75/height);w,h=max(1,int(width*scale)),max(1,int(height*scale));expected=[0]*(128*75)
            for y in range(h):
                for x in range(w):expected[(y+(75-h)//2)*128+x+(128-w)//2]=pixels[(y*height//h)*width+x*width//w]&0xffffff
            check('Thumbnail contains the full canvas with native aspect',actual==expected)
            picture('slots');key(41);c.run_frames(3);key(64);key(40)
            check('GUI load restores every racer and the complete machine',c.command('frame')['frame']==frame and snapshot('after-load')==before)
            c.run_frames(6);key(65);ui=c.command('state_ui')
            check('F8 presents whole-session history between races',ui['mode']==2 and ui['count']>=2)
            picture('rewind');key(80);target=c.command('state_ui')['selected_frame'];key(40)
            check('GUI rewind selects the exact whole-session frame',c.command('state_ui')['mode']==0 and c.command('frame')['frame']==target)
            report['passed']=True
    except Exception as error:report['error']=repr(error)
    finally:
        if client:
            try:client.command('quit')
            except Exception:pass
            client.close()
        if process:
            try:process.wait(timeout=5)
            except subprocess.TimeoutExpired:process.terminate();process.wait(timeout=5)
            if process.returncode:report['passed']=False;report['exit']=process.returncode
        (a.out/'report.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
    return 0 if report['passed'] else 1

if __name__=='__main__':raise SystemExit(main())
