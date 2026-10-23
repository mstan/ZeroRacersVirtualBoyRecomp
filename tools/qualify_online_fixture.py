"""Own an isolated official lobby server for a real host UDP/ICE campaign.

All game peers remain separate processes. This verifies control-plane handoff
and gameplay transport on loopback; it does not certify public authentication
or cross-network NAT traversal. The server, database and logs live under --out.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import time
import traceback
import urllib.request

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--server',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    p.add_argument('--path',choices=('ice',),required=True);p.add_argument('qualify_args',nargs=argparse.REMAINDER)
    a=p.parse_args();a.out=a.out.resolve();a.out.mkdir(parents=True,exist_ok=False)
    rest=a.qualify_args[1:] if a.qualify_args[:1]==['--'] else a.qualify_args
    if any(flag in rest for flag in ('--out','--online-path','--online-url')):p.error('Fixture owns output, transport and server URL')
    profile=a.out/'server';profile.mkdir();(profile/'.env').write_text('')
    with socket.socket() as probe:probe.bind(('127.0.0.1',0));port=probe.getsockname()[1]
    env={k:v for k,v in os.environ.items() if not k.startswith(('JWT_','DISCORD_','COTURN_','INPUT_RELAY_'))}
    env.update(BIND_ADDR=f'127.0.0.1:{port}',DATABASE_URL=f'sqlite:{(profile/"lobby.db").as_posix()}?mode=rwc',
        REQUIRE_AUTH='false',GUEST_CAN_HOST='true',GUEST_CAN_CHAT='false',DISCORD_REQUIRED='false',
        PUBLIC_HOST='127.0.0.1',LOBBY_PUBLIC_HOST='127.0.0.1',CHAT_REPORT_DUMP_DIR=(profile/'reports').as_posix(),RUST_LOG='warn')
    stdout=(profile/'stdout.log').open('wb');stderr=(profile/'stderr.log').open('wb')
    server=subprocess.Popen([str(a.server.resolve())],cwd=profile,stdout=stdout,stderr=stderr,env=env,
        creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
    report={'passed':False,'scope':__doc__,'transport':a.path,'server_sha256':hashlib.sha256(a.server.read_bytes()).hexdigest()}
    try:
        deadline=time.monotonic()+15
        while True:
            if server.poll() is not None:raise RuntimeError(('Official fixture server exited',server.returncode))
            try:
                with urllib.request.urlopen(f'http://127.0.0.1:{port}/health',timeout=1) as reply:report['health']=json.load(reply)
                break
            except OSError:
                if time.monotonic()>deadline:raise TimeoutError('Official fixture startup')
                time.sleep(.025)
        command=[sys.executable,str(Path(__file__).with_name('qualify_netplay.py').resolve()),
            '--out',str(a.out/'match'),'--online-path',a.path,'--online-url',f'ws://127.0.0.1:{port}',*rest]
        with (a.out/'campaign.stdout.log').open('wb') as output,(a.out/'campaign.stderr.log').open('wb') as error:
            result=subprocess.run(command,stdout=output,stderr=error)
        report['campaign_exit']=result.returncode
        leaf=a.out/'match'/'report.json';report['campaign']=json.loads(leaf.read_text()) if leaf.exists() else None
        assert result.returncode==0 and report['campaign'] and report['campaign']['passed'],'Game campaign failed'
        report['passed']=True
    except Exception as error:report['error']=repr(error);report['traceback']=traceback.format_exc()
    finally:
        if server.poll() is None:server.terminate();server.wait(timeout=5)
        stdout.close();stderr.close()
    (a.out/'report.json').write_text(json.dumps(report,indent=2));print(json.dumps({k:report.get(k) for k in ('passed','transport','error')},indent=2))
    return 0 if report['passed'] else 1

if __name__=='__main__':raise SystemExit(main())
