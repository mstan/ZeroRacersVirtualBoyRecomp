"""Remap every VB input through the real launcher and verify host persistence.

Uses the launcher's SDL capture path, then clicks Play and attaches only to
the owned paused runtime. P1 and P4 are edited in separate launches of the
same profile; P2 also captures controller buttons, axes and unbinding. This
proves both 14-button edges and four-seat settings round trips.
Dummy audio and hidden OpenGL launcher; physical input drivers remain untested.
"""
import argparse
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import time

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('framework','runtime','rom','out'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();a.out=a.out.resolve();a.out.mkdir(parents=True,exist_ok=False)
    sys.path.insert(0,str(a.framework.resolve()/'tools'));from debug_client import DebugClient
    config=a.out/'settings.cfg';config.write_text('fullscreen 0\naudio 1\nvolume 0\nscale 1\np4.source 1\n')
    report={'passed':False,'scope':__doc__,'launches':[]}
    expected=[[82,81,80,79,26,22,4,7,27,29,20,8,40,229] for _ in range(4)]
    expected_pads=[[12,13,14,15,106,107,104,105,1,2,10,11,7,5] for _ in range(4)]
    try:
        for player,keys,codes in ((0,[f'F{i}' for i in range(1,15)],list(range(58,70))+[104,105]),
                                  (3,list('abcdefghijklmn'),list(range(4,18))), (1,None,None)):
            with socket.socket() as probe:
                if os.name=='nt':probe.setsockopt(socket.SOL_SOCKET,socket.SO_EXCLUSIVEADDRUSE,1)
                probe.bind(('127.0.0.1',17531))
            shot=a.out/f'player{player+1}-before.png';after=a.out/f'player{player+1}-after.png'
            script=['size:1100x880','view:controller',f'player:{player}','wait:10',f'shot:{shot.as_posix()}']
            if keys:
                for button,key in enumerate(keys):script += [f'capbtn:{button}',f'key:{key}','wait:2']
                script += ['capbtn:13','key:Backspace','wait:2','capbtn:13',f'key:{keys[-1]}','wait:2']
            else:
                codes=list(range(1,15))
                for button in range(14):
                    command=f'padbutton:{button}'
                    if 4<=button<=7:
                        axis=3 if button<6 else 2;value=-30000 if button%2==0 else 30000
                        command=f'padaxis:{axis},{value}';codes[button]=100+axis*2+(value>0)
                    script += [f'cappad:{button}',command,'wait:2']
                # Back/Select must bind as a normal player button, independent
                # of the assist shortcut path's Select-combo handling.
                script += ['cappad:13','key:Backspace','wait:2','cappad:13','padbutton:4','wait:2'];codes[13]=5
            script += [f'shot:{after.as_posix()}','view:dashboard','wait:4','click:960,820','wait:30']
            assert len(script)<=64 and len(';'.join(script))<2048
            env={k:v for k,v in os.environ.items() if not k.startswith(('VBRECOMP_','RNET_SIM_','VB_RB_'))}
            env.update(SDL_AUDIODRIVER='dummy',SDL_VIDEO_WINDOW_POS='-20000,-20000',LNG_TEST_HIDDEN='1',
                LNG_SCRIPT=';'.join(script),SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT='0xffff/0xffff')
            command=[str(a.runtime.resolve()),'--rom',str(a.rom.resolve()),'--launcher','--paused','--no-save',
                '--config',str(config),'--mods-dir',str(a.out/'mods'),'--state-file',str(a.out/'quick.state'),'--port','17531']
            client=process=None
            with (a.out/f'p{player+1}.stdout.log').open('wb') as out,(a.out/f'p{player+1}.stderr.log').open('wb') as error:
                try:
                    process=subprocess.Popen(command,cwd=a.out,env=env,stdout=out,stderr=error,
                        creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
                    deadline=time.monotonic()+30
                    while client is None:
                        if process.poll() is not None:raise RuntimeError(('Launcher did not Play',process.returncode))
                        try:client=DebugClient(17531,timeout=10)
                        except OSError:
                            if time.monotonic()>deadline:raise TimeoutError('Launcher Play did not initialize runtime')
                            time.sleep(.02)
                    assert client.command('frame')['frame']==0
                    if keys:expected[player]=codes
                    else:expected_pads[player]=codes
                    values=dict(line.split(' ',1) for line in config.read_text().splitlines())
                    for seat in range(4):
                        prefix=f'p{seat+1}.' if seat else ''
                        actual=[int(values[f'{prefix}key{i}']) for i in range(14)]
                        assert actual==expected[seat],('Persisted host bindings',seat,actual,expected[seat])
                        pads=[int(values[f'{prefix}pad{i}']) for i in range(14)]
                        assert pads==expected_pads[seat],('Persisted controller bindings',seat,pads,expected_pads[seat])
                    assert shot.is_file() and after.is_file()
                    report['launches'].append({'player':player+1,'all_four_maps':expected.copy(),
                        'all_four_pad_maps':expected_pads.copy(),'paused_after_real_play':True})
                finally:
                    if client:
                        try:client.command('quit')
                        except (OSError,RuntimeError):pass
                        client.close()
                    if process:
                        try:process.wait(timeout=5)
                        except subprocess.TimeoutExpired:process.terminate();process.wait(timeout=5)
            assert process.returncode==0,('Runtime exit',process.returncode)
        report['passed']=True
    except Exception as error:report['error']=repr(error)
    (a.out/'report.json').write_text(json.dumps(report,indent=2))
    print(json.dumps({k:report.get(k) for k in ('passed','error')},indent=2))
    return 0 if report['passed'] else 1

if __name__=='__main__':raise SystemExit(main())
