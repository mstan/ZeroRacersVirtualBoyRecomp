"""Actual independent Zero Racers processes over the shared LAN hub.

Compare complete state and each eye at an agreed parked tick. Optional state
operations exercise the real F7 UI when windowed. This harness does not certify
Internet traversal or visual lobby interaction.
"""
import argparse
import hashlib
import json
import os
import re
import socket
import subprocess
from pathlib import Path
import sys
import time
import traceback

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('framework','runtime','rom','out'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--players',type=int,choices=(2,3,4),default=2)
    p.add_argument('--ticks',type=int,default=96)
    p.add_argument('--port',type=int,default=16031)
    p.add_argument('--wire-port',type=int,default=19100)
    p.add_argument('--route',type=Path)
    p.add_argument('--mixed',action='store_true')
    p.add_argument('--latency-ms',type=int,default=0)
    p.add_argument('--jitter-ms',type=int,default=0)
    p.add_argument('--loss-pct',type=int,default=0)
    p.add_argument('--force-mispredict',type=int,default=0)
    p.add_argument('--native-setup-map',help='Debug native setup fixture: session-slot to dense racer permutation')
    p.add_argument('--shared-room',action='store_true',help='Use the real shared launcher LAN/Direct-IP callbacks before game startup')
    p.add_argument('--room-capacity',type=int,choices=(2,3,4))
    p.add_argument('--host-seat',type=int,choices=(0,1,2,3))
    p.add_argument('--window',action='store_true',help='Dummy SDL windows; check the actual one-eye game texture on every peer')
    p.add_argument('--state-operations',action='store_true',help='Qualify host-synchronized between-race slots and repeated input epoch resets')
    p.add_argument('--room-rematches',type=int,choices=(0,1,2),default=0,help='Return to the shared room and relaunch in the same processes')
    p.add_argument('--color-package',type=Path,help='Install and enable the same optional full-color package on every peer')
    a=p.parse_args();a.out=a.out.resolve();a.out.mkdir(parents=True,exist_ok=False)
    if a.window and not a.shared_room:p.error('--window requires --shared-room')
    if a.room_rematches and not a.shared_room:p.error('--room-rematches requires --shared-room')
    sys.path.insert(0,str(a.framework.resolve()/'tools'));from cosim import Runner
    from debug_client import DebugClient
    class RoomRunner(Runner):
        # Room negotiation precedes debug-server startup. Spawn every peer
        # first, then attach; waiting for the first child would deadlock it.
        def __init__(self,exe,rom,mode,port,directory,extra):
            self.directory=directory.resolve();self.directory.mkdir(parents=True,exist_ok=True)
            self.exe=exe.resolve();self.rom=rom.resolve();self.client=None;self.port=port
            with socket.socket() as probe:
                if os.name=='nt':probe.setsockopt(socket.SOL_SOCKET,socket.SO_EXCLUSIVEADDRUSE,1)
                probe.bind(('127.0.0.1',port))
            self.stdout_log=(self.directory/'stdout.log').open('wb');self.stderr_log=(self.directory/'stderr.log').open('wb')
            args=[str(self.exe),'--rom',str(self.rom),'--no-launcher','--paused','--port',str(port),
                  '--execution',mode,'--config',str(self.directory/'settings.cfg'),'--mods-dir',str(self.directory/'mods'),*extra]
            if not a.window:args+=['--headless']
            environment={k:v for k,v in os.environ.items() if not k.startswith('VBRECOMP_')}
            if a.window:
                (self.directory/'settings.cfg').write_text('fullscreen 0\naudio 1\nvolume 0\nscale 1\n')
                environment.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',SDL_RENDER_DRIVER='software',
                    SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT='0xffff/0xffff')
            self.process=subprocess.Popen(args,cwd=self.directory,stdout=self.stdout_log,stderr=self.stderr_log,env=environment,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
        def attach(self):
            deadline=time.monotonic()+20
            while self.client is None:
                if self.process.poll() is not None:raise RuntimeError(f'Room peer exited: {self.directory}')
                try:self.client=DebugClient(self.port,timeout=15)
                except OSError:
                    if time.monotonic()>deadline:raise TimeoutError(f'Room peer startup: {self.directory}')
                    time.sleep(.01)
            assert self.client.command('frame')['frame']==0,'Shared launch ran before continuation'
    report={'passed':False,'scope':__doc__,'players':a.players,'checkpoints':[]};runners=[];saves=[]
    try:
        for slot in range(a.players):
            extra=['--net-players',str(a.players),'--net-slot',str(slot),'--net-session','329128105',
                   '--net-bind',f'127.0.0.1:{a.wire_port+slot}','--versus-course','11']
            if slot or a.players==2:extra+=['--net-peer',f'127.0.0.1:{a.wire_port+(0 if slot else 1)}']
            if a.native_setup_map:extra+=['--net-test-lobby-map',a.native_setup_map]
            if a.shared_room:
                extra=['--net-players',str(a.players),'--net-bind',f'127.0.0.1:{a.wire_port+slot}',
                       '--net-room-host' if slot==0 else '--net-room-join',f'127.0.0.1:{a.wire_port}',
                       '--net-name','Same name','--versus-course','11']
                if slot==0 and a.room_capacity:extra+=['--net-test-room-capacity',str(a.room_capacity)]
                if slot==0 and a.host_seat is not None:extra+=['--net-test-host-seat',str(a.host_seat)]
            folder=a.out/f'peer-{slot}';folder.mkdir()
            save=folder/'protected-native.sav';native=bytes((i*29+7+slot)&255 for i in range(65536));save.write_bytes(native)
            saves.append((save,native));extra+=['--save',save.as_posix()]
            extra+=['--state-file',(folder/'quick.state').as_posix()]
            if a.color_package:extra+=['--install-mod',a.color_package.resolve().as_posix(),'--enable-mod','zero-racers.full-color:full-color']
            settings={'RNET_SIM_LATENCY_MS':str(a.latency_ms),'RNET_SIM_JITTER_MS':str(a.jitter_ms),
                      'RNET_SIM_LOSS_PCT':str(a.loss_pct),'RNET_SIM_SEED':str(91231+slot),
                      'VB_RB_FORCE_MISPREDICT':str(a.force_mispredict if slot==0 else 0)}
            saved={key:os.environ.get(key) for key in settings}
            try:
                os.environ.update(settings)
                runners.append((RoomRunner if a.shared_room else Runner)(a.runtime,a.rom,'interpreter' if a.mixed and slot%2 else 'native',
                    a.port+slot,a.out/f'peer-{slot}',extra=extra))
            finally:
                for key,value in saved.items():
                    if value is None:os.environ.pop(key,None)
                    else:os.environ[key]=value
        if a.shared_room:
            for r in runners:r.attach()
        for r in runners:assert r.memory(0x06000000)==bytes(65536),'Online startup loaded native persistent progress'
        for r in runners:r.client.command('continue')
        deadline=time.monotonic()+25
        while True:
            statuses=[r.client.command('net_status') for r in runners]
            if all(s['settled'] for s in statuses):break
            if time.monotonic()>deadline:raise TimeoutError(('Profile settlement',statuses))
            time.sleep(.005)
        def park(tick):
            for r in runners:r.client.command('net_hold',tick=tick)
            deadline=time.monotonic()+60
            while True:
                states=[r.client.command('net_status') for r in runners]
                if all(s['tick']==tick and s['held'] for s in states):return states
                if any(s['desync'] for s in states):raise AssertionError(('Desync',states))
                if time.monotonic()>deadline:raise TimeoutError(('Park',tick,states))
                time.sleep(.005)
        if a.route:
            end=0
            for segment in json.loads(a.route.read_text()):
                end+=segment['frames']
                for slot,r in enumerate(runners):
                    phase=r.client.command('game_status')['phase']
                    r.client.command('set_input',pad=16 if slot or phase>=1 else segment.get('pad',0))
                for r in runners:r.client.command('net_release')
                states=park(end)
                if (a.native_setup_map or a.shared_room) and all(r.client.command('game_status')['menu']['screen']==2 for r in runners):
                    for buttons in (4,0,4096,16):
                        for slot,r in enumerate(runners):r.client.command('set_input',pad=buttons if buttons!=4096 or slot==0 else 0)
                        end+=8
                        for r in runners:r.client.command('net_release')
                        park(end)
                    for attempt in range(30):
                        if all(r.client.command('game_status')['phase']==3 for r in runners):break
                        end+=80
                        for r in runners:r.client.command('net_release')
                        park(end)
                    else:raise AssertionError('Ready native online setup did not reach a race')
                    break
                if all(r.client.command('game_status')['phase']==3 for r in runners):break
            else:raise AssertionError('Route did not reach the shared race')
            for slot,r in enumerate(runners):r.client.command('set_input',pad=16)
            target=end+a.ticks
            for r in runners:r.client.command('net_release')
        else:target=max(s['tick'] for s in statuses)+a.ticks
        if a.force_mispredict:
            # A scheduled hold deliberately forbids new prediction. Let the
            # real race run freely first, then schedule a settled endpoint.
            deadline=time.monotonic()+60
            while True:
                live=[r.client.command('net_status') for r in runners]
                if min(s['tick'] for s in live)>=target:break
                if time.monotonic()>deadline:raise TimeoutError(('Free replay window',live))
                time.sleep(.005)
            target=max(s['tick'] for s in live)+64
        states=park(target)
        if a.state_operations:
            from qualify_online_states import qualify
            target,states,report['online_states']=qualify(runners,a,park,target)
        if a.room_rematches:
            from qualify_room_return import qualify
            target,states,report['room_return']=qualify(runners,a,park,target)
        for r in runners:r.client.command('pause')
        snapshots=[];games=[]
        for slot,r in enumerate(runners):
            path=a.out/f'peer-{slot}'/'parked.state';r.client.command('save_state',path=path.as_posix())
            snapshots.append(path.read_bytes());games.append(r.client.command('game_status'))
        assert all(s==snapshots[0] for s in snapshots),'Full parked states differ'
        assert all(g=={**games[0],'id':g['id']} for g in games),'Authoritative game status differs'
        audio=[]
        for slot,r in enumerate(runners):
            pcm=r.snapshot(r.client.command('frame')['frame'],['audio'])['audio']
            (a.out/f'peer-{slot}'/'audio.bin').write_bytes(pcm);audio.append(pcm)
        assert all(pcm==audio[0] for pcm in audio),'Emulated PCM differs across peers'
        eyes=[]
        for seat in range(a.players if games[0]['phase'] in (2,3,4) else 0):
            for eye in range(2):
                pictures=[]
                for slot,r in enumerate(runners):
                    path=a.out/f'peer-{slot}'/f'p{seat}-eye{eye}.png'
                    r.client.command('screenshot',path=path.as_posix(),game=1,seat=seat,eye=eye)
                    pictures.append(path.read_bytes())
                assert all(x==pictures[0] for x in pictures),('Eye differs',seat,eye)
                eyes.append({'seat':seat,'eye':eye,'sha256':hashlib.sha256(pictures[0]).hexdigest()})
        if a.window and not a.state_operations:
            from _imgio import load_png
            for slot,r in enumerate(runners):
                for pressed in (1,0):r.client.command('host_input',device=0,value=64,pressed=pressed)
                for pressed in (1,0):r.client.command('host_input',device=0,value=65,pressed=pressed)
                assert r.client.command('state_ui')['mode']==0,'An online peer opened a local state browser'
                time.sleep(.08)
                path=a.out/f'peer-{slot}'/'window.png';r.client.command('screenshot',host=1,path=path.as_posix())
                width,height,pixels=load_png(path);assert (width,height)==(384,224),'Online window is not one native eye'
                # Find the one racer this peer displays. Every independent view
                # has already been byte-audited across all peers above.
                matches=[seat for seat in range(a.players)
                         if load_png(a.out/f'peer-{slot}'/f'p{seat}-eye0.png')==(width,height,pixels)]
                assert len(matches)==1,('Window does not display one distinct owned racer',slot,matches)
                launch=re.search(r'shared room launch players=\d+ slot=\d+ racer=(\d+)',(a.out/f'peer-{slot}'/'stderr.log').read_text())
                assert launch and matches==[int(launch[1])],('Window displays another racer',slot,matches)
                eyes.append({'peer':slot,'window_racer':matches[0]})
        report['checkpoints'].append({'tick':target,'peers':states,'games':games,'state_bytes':len(snapshots[0]),
            'state_sha256':hashlib.sha256(snapshots[0]).hexdigest(),'eyes':eyes,
            'pcm_bytes':len(audio[0]),'pcm_sha256':hashlib.sha256(audio[0]).hexdigest()})
        if a.route:assert all(g['phase'] in (3,4) and g['race_fields']>0 for g in games),'No shared race executed'
        if a.force_mispredict and not a.state_operations:assert sum(s['resim'] for s in states)>0,'No actual replay executed'
        report['conditions']={k:getattr(a,k) for k in ('mixed','shared_room','native_setup_map','room_capacity','host_seat','window','latency_ms','jitter_ms','loss_pct','force_mispredict')}
        report['passed']=True
    except Exception as error:report['error']=repr(error);report['traceback']=traceback.format_exc()
    finally:
        # Park all debug loops before requesting shutdown so an early BYE
        # cannot race another peer's quit into its automatic room rejoin.
        for r in runners:
            try:r.client.command('pause')
            except Exception:pass
        for r in runners:
            try:r.client.command('quit')
            except Exception:pass
        for r in runners:r.close()
    try:
        for save,native in saves:
            assert save.read_bytes()==native,'Native save changed on network exit'
            assert not list(save.parent.glob('protected-native.sav.*')),'Native save backup/temp side effect'
        report['native_saves_protected']=True
    except Exception as error:report['passed']=False;report['error']=repr(error)
    (a.out/'report.json').write_text(json.dumps(report,indent=2))
    print(json.dumps({'passed':report['passed'],'players':a.players,'error':report.get('error')},indent=2))
    return 0 if report['passed'] else 1

if __name__=='__main__':raise SystemExit(main())
