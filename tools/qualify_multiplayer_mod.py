"""Toggle the bundled multiplayer mod through the real Mods page, then Play.

Uses isolated profiles, hidden OpenGL and dummy speakers. Checks disabled
stock entry, persistence, current feature-set restore refusal and CLI gates.
"""
import argparse,hashlib,json,os,socket,subprocess,sys,time,traceback,zipfile
from pathlib import Path
from versus_test_mod import versus_mod_args

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('framework','runtime','rom','route','out'):p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();a.out=a.out.resolve();a.out.mkdir(parents=True,exist_ok=False)
    sys.path.insert(0,str(a.framework.resolve()/'tools'));from debug_client import DebugClient
    package=Path(versus_mod_args(a.runtime)[1]);mods=a.out/'mods'
    catalog=mods/'packages/zero-racers.multiplayer/0.1.0';catalog.mkdir(parents=True)
    with zipfile.ZipFile(package) as z:
        for n in z.namelist():assert (catalog/n).resolve().is_relative_to(catalog.resolve())
        z.extractall(catalog)
    config=a.out/'settings.cfg';config.write_text('fullscreen 0\naudio 1\nvolume 0\nscale 1\n')
    report={'passed':False,'scope':__doc__,'runtime_sha256':hashlib.sha256(a.runtime.read_bytes()).hexdigest(),'launches':[]}
    enabled_state=a.out/'enabled-title.state'
    try:
        for index,(enabled,toggle) in enumerate(((False,False),(True,True),(True,False),(False,True))):
            folder=a.out/f'launch{index}';folder.mkdir();client=process=None
            script=['size:1100x880','wait:8','click:1020,47','wait:8',f'shot:{(folder/"mods-before.png").as_posix()}']
            if toggle:script+=['click:65,304','wait:8']
            script += [f'shot:{(folder/"mods-after.png").as_posix()}','click:960,820','wait:8']
            env={k:v for k,v in os.environ.items() if not k.startswith(('LNG_','VBRECOMP_','RNET_SIM_','VB_RB_'))}
            env.update(SDL_AUDIODRIVER='dummy',SDL_VIDEO_WINDOW_POS='-20000,-20000',LNG_TEST_HIDDEN='1',
                       LNG_SCRIPT=';'.join(script),SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT='0xffff/0xffff')
            args=[str(a.runtime.resolve()),'--rom',str(a.rom.resolve()),'--launcher','--paused','--no-save',
                  '--config',str(config),'--mods-dir',str(mods),'--port','17821']
            with (folder/'stdout.log').open('wb') as out,(folder/'stderr.log').open('wb') as err:
                try:
                    with socket.socket() as probe:
                        probe.setsockopt(socket.SOL_SOCKET,socket.SO_EXCLUSIVEADDRUSE,1);probe.bind(('127.0.0.1',17821))
                    process=subprocess.Popen(args,cwd=folder,env=env,stdout=out,stderr=err,creationflags=subprocess.CREATE_NO_WINDOW)
                    deadline=time.monotonic()+30
                    while client is None:
                        if process.poll() is not None:raise RuntimeError(('Launcher did not Play',process.returncode))
                        try:client=DebugClient(17821)
                        except OSError:
                            if time.monotonic()>deadline:raise TimeoutError('Mods Play')
                            time.sleep(.02)
                    c=client;assert c.command('frame')['frame']==0
                    assert c.command('game_status')['multiplayer_enabled']==enabled,'GUI selection did not activate the plugin'
                    remaining=800
                    for segment in json.loads(a.route.read_text()):
                        n=min(remaining,segment['frames']);c.command('set_input',pad=segment.get('pad',0))
                        while n:
                            k=min(32,n);c.command('replay_frames',frames=k);n-=k;remaining-=k
                        if not remaining:break
                    c.command('set_input',pad=0);c.command('replay_frames',frames=8)
                    status=c.command('game_status');assert status['menu']['screen']==(1 if enabled else 0),status
                    for eye in range(2):
                        c.command('screenshot',eye=eye,path=(folder/f'title-eye{eye}.png').as_posix(),**({'game':1} if enabled else {'presented':1}))
                    if enabled:c.command('save_state',path=enabled_state.as_posix())
                    elif enabled_state.exists():
                        before=c.command('state_digest')['digest'];refused=False
                        try:c.command('load_state',path=enabled_state.as_posix())
                        except RuntimeError:refused=True
                        assert refused and c.command('state_digest')['digest']==before,'Disabled feature accepted/mutated a VS snapshot'
                    report['launches'].append({'enabled':enabled,'toggled_in_real_gui':toggle,'title':status['menu'],'paused_after_play':True})
                finally:
                    if client:
                        try:client.command('quit')
                        except Exception:pass
                        client.close()
                    if process:
                        try:process.wait(timeout=5)
                        except subprocess.TimeoutExpired:process.terminate();process.wait(timeout=5)
                assert process.returncode==0,('Bad runtime exit',process.returncode)
        native=a.out/'protected.sav';data=bytes((i*7+3)&255 for i in range(65536));native.write_bytes(data)
        for case,extra in (('local',['--versus','2']),('network',['--net-players','2','--net-slot','0','--net-session','1234567','--net-bind','127.0.0.1:19451','--net-peer','127.0.0.1:19452'])):
            result=subprocess.run([str(a.runtime.resolve()),'--rom',str(a.rom.resolve()),'--headless','--no-launcher',
                '--config',str(config),'--mods-dir',str(mods),'--save',str(native),*extra],cwd=a.out,
                capture_output=True,timeout=15,creationflags=subprocess.CREATE_NO_WINDOW)
            (a.out/f'{case}-disabled.log').write_bytes(result.stdout+result.stderr)
            assert result.returncode==2 and b'Enable Multiplayer' in result.stdout+result.stderr,(case,result.returncode,result.stdout,result.stderr)
            assert native.read_bytes()==data and not list(a.out.glob('protected.sav.*')),'Disabled launch touched native progress'
        report['disabled_cli_and_network_refused_before_frame0']=True
        report['passed']=True
    except Exception as error:report.update(error=repr(error),traceback=traceback.format_exc())
    (a.out/'report.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
    return 0 if report['passed'] else 1

if __name__=='__main__':raise SystemExit(main())
