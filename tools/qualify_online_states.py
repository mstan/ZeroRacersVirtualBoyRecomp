"""Shared-STATE slot qualification used by the independent-process LAN harness.

Exercises real sealed controller retirement, the actual F7 SDL UI when windowed,
host/guest authorization, strict envelope/device/racing-state refusal, repeated
loads and post-load agreement. The caller audits all eyes, PCM and native saves.
"""
import hashlib
from pathlib import Path
import struct
import time
import zlib

def qualify(runners,args,park,target):
    def release():
        for r in runners:r.client.command('net_release')
    def advance(pad,fields=8):
        nonlocal target
        for r in runners:r.client.command('set_input',pad=pad)
        target+=fields;release();return park(target)
    def key(r,scan):
        for pressed in (1,0):r.client.command('host_input',device=0,value=scan,pressed=pressed)
        time.sleep(.04)
    def wait_operation():
        deadline=time.monotonic()+45
        while True:
            statuses=[r.client.command('net_status') for r in runners]
            if not any(s['state_busy'] for s in statuses) and statuses[0]['state_result']:
                assert all(s['state_result']==1 for s in statuses),statuses
                return statuses
            if time.monotonic()>deadline:raise TimeoutError(('Online operation',statuses))
            time.sleep(.01)
    def expect_refused(r,path,saving):
        try:r.client.command('net_state',path=path.as_posix(),save=int(saving),slot=0)
        except RuntimeError:pass
        else:raise AssertionError('An unauthorized or invalid online operation was accepted')
    def raw_state(r,name):
        path=r.directory/(name+'.state');r.client.command('save_state',path=path.as_posix());return path.read_bytes()
    def frozen_refusal(path):
        for r in runners:r.client.command('pause')
        before=[raw_state(r,'before-refusal') for r in runners]
        expect_refused(runners[0],path,False)
        after=[raw_state(r,'after-refusal') for r in runners]
        assert before==after,'Rejected online load changed execution bytes'
        for r in runners:r.client.command('continue')
    host=runners[0];save=host.directory/'online-slot.state'
    for r in runners:expect_refused(r,save,True)
    for r in runners:r.client.command('pause')
    racing=raw_state(host,'racing-candidate')
    for r in runners:r.client.command('continue')
    for pad in (4096,0,8192,0):advance(pad)
    assert all(r.client.command('game_status')['phase']==4 for r in runners),'Controllers did not retire the race'
    for r in runners[1:]:expect_refused(r,save,True)
    if args.window:
        for r in runners[1:]:
            key(r,64);assert r.client.command('state_ui')['mode']==0,'Guest opened host-only F7'
        for r in runners:
            key(r,65);assert r.client.command('state_ui')['mode']==0,'Online rewind opened'
        key(host,64);assert host.client.command('state_ui')['mode']==1,'Host could not open between-race F7'
        save=host.directory/'states'/hashlib.sha256(args.rom.read_bytes()).hexdigest()/'online'/'slot-01.state'
        key(host,22) # S: actual browser save, no debug operation substitute.
    else:host.client.command('net_state',path=save.as_posix(),save=1,slot=0)
    statuses=wait_operation();target=max(s['tick'] for s in statuses)+64;states=park(target)
    assert save.is_file() and save.read_bytes()[:8]==b'VBVSNET1'
    assert all(r.client.command('game_status')['phase']==4 for r in runners)
    envelope=save.read_bytes();text_size,blob_size=struct.unpack_from('<II',envelope,8);offset=20+text_size
    assert offset+blob_size==len(envelope)
    # Current-build candidate with valid outer/inner CRCs, invalid CPU state.
    invalid=bytearray(envelope);struct.pack_into('<I',invalid,offset+112,1)
    struct.pack_into('<I',invalid,offset+80,zlib.crc32(invalid[offset+112:]))
    struct.pack_into('<I',invalid,16,zlib.crc32(invalid[offset:]))
    bad=host.directory/'invalid-device.state';bad.write_bytes(invalid);frozen_refusal(bad)
    # Correct build/ROM/roster envelope containing a racing execution snapshot.
    bad_race=bytearray(envelope[:offset]);struct.pack_into('<I',bad_race,12,len(racing))
    struct.pack_into('<I',bad_race,16,zlib.crc32(racing));bad_race+=racing
    bad=host.directory/'racing-slot.state';bad.write_bytes(bad_race);frozen_refusal(bad)
    bad=host.directory/'different-session.state';mismatch=bytearray(envelope);mismatch[20]^=1;bad.write_bytes(mismatch);frozen_refusal(bad)
    load_runs=[]
    for attempt in range(2):
        # B opens the native between-race setup; loading must restore the saved
        # results, including the native guest, menu and every actor together.
        if args.window and host.client.command('state_ui')['mode']:key(host,64)
        advance(8);advance(0,80)
        assert all(r.client.command('game_status')['menu']['screen']==2 for r in runners),'Native course setup did not open'
        before_frame=host.client.command('frame')['frame']
        if args.window:
            key(host,64);assert host.client.command('state_ui')['mode']==1
            key(host,40) # Return: actual slot load.
        else:host.client.command('net_state',path=save.as_posix(),save=0,slot=0)
        statuses=wait_operation()
        target=max(s['tick'] for s in statuses)+64;states=park(target)
        assert all(s['desync']==0 for s in states)
        assert all(r.client.command('game_status')['phase']==4 and r.client.command('game_status')['menu']['screen']==0 for r in runners)
        after_frame=host.client.command('frame')['frame'];assert after_frame<before_frame,'Load did not rewind the execution snapshot'
        if args.window:assert host.client.command('state_ui')['mode']==0,'Successful online load did not close F7'
        load_runs.append({'attempt':attempt+1,'before_frame':before_frame,'after_frame':after_frame,'states':states})
    assert save.read_bytes()==envelope,'Loading mutated the host slot'
    return target,states,{'passed':True,'actual_f7_ui':args.window,'guest_and_racing_requests_refused':True,
        'invalid_device_racing_identity_candidates_refused_without_mutation':True,'repeated_epoch_loads':load_runs,
        'file_sha256':hashlib.sha256(envelope).hexdigest()}
