"""Create/join/Play through actual LAN launcher widgets, without helper readiness.

Separate muted hidden SDL/OpenGL windows and native-save canaries. This covers
the human launcher path; it does not certify Internet/NAT traversal.
"""
import argparse, hashlib, json, os, socket, subprocess, sys, time, traceback, zipfile
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('framework', 'runtime', 'rom', 'color-package', 'multiplayer-package', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--port', type=int, default=17861)
    parser.add_argument('--wire-port', type=int, default=19471)
    a = parser.parse_args()
    a.out = a.out.resolve(); a.out.mkdir(parents=True, exist_ok=False)
    sys.path.insert(0, str(a.framework.resolve() / 'tools'))
    from debug_client import DebugClient
    children, clients, logs, saves = [], [], [], []
    report = dict(passed=False, scope=__doc__, runtime_sha256=hashlib.sha256(a.runtime.read_bytes()).hexdigest())
    try:
        for port, kind in ((a.port, socket.SOCK_STREAM), (a.port+1, socket.SOCK_STREAM), (a.wire_port, socket.SOCK_DGRAM)):
            with socket.socket(type=kind) as probe:
                if os.name == 'nt': probe.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
                probe.bind(('127.0.0.1', port))
        for seat in range(2):
            folder = a.out / f'peer-{seat}'; folder.mkdir()
            (folder / 'settings.cfg').write_text('fullscreen 0\naudio 1\nvolume 0\nscale 1\n')
            (folder / 'netplay-name.txt').write_text(f'Launcher QA {seat+1}\n')
            network = folder / 'saves/netplay'; network.mkdir(parents=True)
            (network / 'network settings').write_text(f'preferred_port={a.wire_port}\n')
            native = folder / 'native.sav'; data = bytes((i*17+seat)&255 for i in range(65536))
            native.write_bytes(data); saves.append((native, data))
            for package in (a.multiplayer_package, a.color_package):
                import tomllib
                with zipfile.ZipFile(package) as z:
                    info = tomllib.loads(z.read('manifest.toml').decode())
                    target = folder / 'mods/packages' / info['id'] / info['version']
                    target.mkdir(parents=True)
                    for entry in z.namelist(): assert (target/entry).resolve().is_relative_to(target.resolve())
                    z.extractall(target)
            script = ['size:1100x880', 'wait:8', 'view:netplay_mode', 'wait:8', 'click:200,260', 'wait:8']
            if seat == 0:
                script += ['click:110,820', 'wait:8', 'click:550,610', 'wait:500',
                           f'shot:{(folder/"before-play.png").as_posix()}', 'click:980,820', 'wait:500',
                           f'shot:{(folder/"after-play.png").as_posix()}', 'quit']
            else:
                # Match the advertised interface. A guest bound to a LAN IP
                # must not expect loopback as the source of the host's reply.
                endpoint=(a.out/'peer-0/netplay-lan.txt').read_text().splitlines()[1]
                host_ip=endpoint.rsplit(':',1)[0]
                script += ['click:980,820', 'wait:8', 'click:450,220', 'key:Home']
                script += ['key:Delete'] * 15
                script += [f'text:{host_ip}', 'click:400,262', 'key:Home']
                script += ['key:Delete'] * 5
                script += [f'text:{a.wire_port}', 'wait:8', f'shot:{(folder/"direct-config.png").as_posix()}',
                           'click:540,354', 'wait:300',
                           f'shot:{(folder/"joined.png").as_posix()}', 'wait:900', 'quit']
            env = {k:v for k,v in os.environ.items() if not k.startswith(('LNG_', 'VBRECOMP_', 'RNET_SIM_', 'VB_RB_'))}
            temp = folder/'tmp'; temp.mkdir()
            env.update(TEMP=str(temp), TMP=str(temp), SDL_AUDIODRIVER='dummy', SDL_VIDEO_WINDOW_POS='-20000,-20000',
                       LNG_TEST_HIDDEN='1', LNG_SCRIPT=';'.join(script), SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT='0xffff/0xffff')
            for key in ('MESA_SHADER_CACHE_DIR', '__GL_SHADER_DISK_CACHE_PATH', 'DXVK_STATE_CACHE_PATH'):
                cache = folder/'cache'/key; cache.mkdir(parents=True); env[key]=str(cache)
            command = [str(a.runtime.resolve()), '--rom', str(a.rom.resolve()), '--launcher', '--paused',
                       '--config', str(folder/'settings.cfg'), '--mods-dir', str(folder/'mods'),
                       '--save', str(native), '--state-file', str(folder/'quick.state'), '--port', str(a.port+seat),
                       '--enable-mod', 'zero-racers.multiplayer:versus', '--enable-mod', 'zero-racers.full-color:full-color']
            out=(folder/'stdout.log').open('wb'); err=(folder/'stderr.log').open('wb'); logs += [out,err]
            children.append(subprocess.Popen(command,cwd=folder,env=env,stdout=out,stderr=err,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0))
            if seat == 0:
                deadline=time.monotonic()+25
                index=folder/'netplay-lan.txt'
                while not index.is_file():
                    assert children[0].poll() is None, 'Host launcher exited before creating its room'
                    if time.monotonic()>deadline: raise TimeoutError('Real Host Lobby click did not create room')
                    time.sleep(.05)
        deadline=time.monotonic()+45
        for seat, child in enumerate(children):
            while True:
                if child.poll() is not None: raise RuntimeError(f'Launcher peer {seat} exited {child.returncode}')
                try: clients.append(DebugClient(a.port+seat,timeout=10)); break
                except OSError:
                    if time.monotonic()>deadline: raise TimeoutError('Actual launcher Play never launched both peers')
                    time.sleep(.05)
        for client in clients:
            assert client.command('frame')['frame']==0
            assert client.command('game_status')['multiplayer_enabled']
            client.command('continue')
        deadline=time.monotonic()+30
        while True:
            states=[client.command('net_status') for client in clients]
            if all(s['settled'] for s in states): break
            if time.monotonic()>deadline: raise TimeoutError(('Profile settlement',states))
            time.sleep(.02)
        assert all(s['players']==2 for s in states),states
        assert states[0]['session']==states[1]['session'],states
        report.update(passed=True, actual_launcher_create_join_play=True, no_helper_ready=True, peers=states)
    except Exception as error:
        report.update(error=repr(error),traceback=traceback.format_exc())
    finally:
        for client in clients:
            try: client.command('quit')
            except Exception: pass
            client.close()
        for child in children:
            try: child.wait(timeout=5)
            except subprocess.TimeoutExpired: child.terminate(); child.wait(timeout=5)
        for log in logs: log.close()
        report['native_canaries_preserved']=all(path.read_bytes()==data for path,data in saves)
        report['passed']=report['passed'] and report['native_canaries_preserved']
        (a.out/'report.json').write_text(json.dumps(report,indent=2))
    print(json.dumps(report,indent=2))
    return 0 if report['passed'] else 1


if __name__=='__main__': raise SystemExit(main())
