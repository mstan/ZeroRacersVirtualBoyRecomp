"""Real native-title VS transitions preserve SP native saves and state bytes.

Independent native/interpreter processes, dummy SDL audio threads, current
build snapshots only. Canaries test persistence isolation, not game save data.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
from versus_test_mod import versus_mod_args
import zlib

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('framework','runtime','rom','route','out'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--port',type=int,default=15931)
    a=p.parse_args();a.out=a.out.resolve();a.out.mkdir(parents=True,exist_ok=False)
    sys.path.insert(0,str(a.framework.resolve()/'tools'));from cosim import Runner
    report={'passed':False,'scope':__doc__,'backends':{}}
    try:
        for mode in ('native','interpreter'):
            folder=a.out/mode;folder.mkdir();save=folder/'native-progress.sav';save.write_bytes(bytes(65536))
            with Runner(a.runtime,a.rom,mode,a.port,folder/'profile',extra=('--save',save.as_posix(),*versus_mod_args(a.runtime)),window=True) as r:
                c=r.client
                def fields(n):
                    while n:
                        k=min(n,32);c.command('replay_frames',frames=k);n-=k
                def pulse(pad,seat=0):
                    c.command('set_input',pad=pad if seat==0 else 0);c.command('game_inputs',**{f'pad{seat}':pad});fields(4)
                    c.command('set_input',pad=0);c.command('game_inputs');fields(4)
                def snapshot(name):
                    path=folder/(name+'.state');c.command('save_state',path=path.as_posix());return path
                remaining=800
                for segment in json.loads(a.route.read_text()):
                    n=min(remaining,segment['frames']);c.command('set_input',pad=segment.get('pad',0));fields(n);remaining-=n
                    if not remaining:break
                c.command('set_input',pad=0);fields(8)
                assert c.command('game_status')['menu']['screen']==1
                c.command('write_ram',addr=0x0600ffff,val=42);sp_bytes=r.memory(0x06000000);sp=snapshot('single-player')
                for _ in range(4):pulse(1024)
                pulse(4);assert c.command('game_status')['menu']['screen']==2
                pulse(4,1);pulse(4096);fields(900)
                assert c.command('game_status')['phase']==3
                assert save.read_bytes()==sp_bytes,'VS entry did not preserve live SP progress'
                assert r.memory(0x06000000)[-1]!=42,'VS retained native persistent progress'
                c.command('write_ram',addr=0x0600ffff,val=94);vs=snapshot('versus')
                assert save.read_bytes()==sp_bytes,'VS wrote its SRAM to the native save'
                # A valid outer envelope with an invalid CPU must not invoke
                # any mode callback or touch native files/backups.
                invalid=bytearray(vs.read_bytes());struct.pack_into('<I',invalid,112,1)
                struct.pack_into('<I',invalid,80,zlib.crc32(invalid[112:]));bad=folder/'invalid.state';bad.write_bytes(invalid)
                backups=set(folder.glob('native-progress.sav.*'))
                try:c.command('load_state',path=bad.as_posix())
                except RuntimeError:pass
                else:raise AssertionError('Invalid CPU state was accepted')
                assert snapshot('after-invalid').read_bytes()==vs.read_bytes()
                assert save.read_bytes()==sp_bytes and set(folder.glob('native-progress.sav.*'))==backups
                c.command('load_state',path=sp.as_posix());assert r.memory(0x06000000)==sp_bytes
                c.command('write_ram',addr=0x0600ffff,val=79);latest=r.memory(0x06000000)
                c.command('load_state',path=vs.as_posix())
                assert save.read_bytes()==latest,'VS restore published candidate SRAM instead of pre-restore SP progress'
                assert snapshot('versus-restored').read_bytes()==vs.read_bytes(),'Mode hook changed restored execution bytes'
                c.command('game_inputs',command0=1);fields(1);c.command('game_inputs');fields(4)
                assert c.command('game_status')['phase']==4
                c.command('game_inputs',pad0=8192);fields(1)
                assert c.command('frame')['frame']==0,'Results return did not cold reset at the field boundary'
                assert c.command('game_status')['players']==0
                assert r.memory(0x06000000)==latest,'Cold SP return retained VS SRAM'
                c.command('game_inputs');c.command('set_input',pad=0)
                snapshot('returned-title');report['backends'][mode]={'native_sha256':hashlib.sha256(latest).hexdigest(),
                    'entry_preserved':True,'invalid_restore_no_side_effects':True,'sp_vs_restore_source':True,
                    'exact_vs_restore':True,'field_boundary_return':True,'nested_audio_replay':True}
            assert save.read_bytes()==latest,'Native save changed after returning to SP'
            backup_data=[path.read_bytes() for path in folder.glob('native-progress.sav.pre-state-*.bak')]
            # The first protected write also backs up the original empty
            # native file. Every backup must be one of the three SP versions.
            assert backup_data and all(data in (bytes(65536),sp_bytes,latest) for data in backup_data),'A native backup contains VS data'
            assert not list(folder.glob('native-progress.sav.tmp'))
        for name in ('single-player.state','versus.state','versus-restored.state','returned-title.state'):
            assert (a.out/'native'/name).read_bytes()==(a.out/'interpreter'/name).read_bytes(),name+' backend mismatch'
        report['passed']=True
    except Exception as error:report['error']=repr(error)
    (a.out/'report.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
    return 0 if report['passed'] else 1

if __name__=='__main__':raise SystemExit(main())
