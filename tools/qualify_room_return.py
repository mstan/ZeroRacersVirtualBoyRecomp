"""Actual room rejoin + rematch in the same independent game processes.

First return uses the authority's sealed Results Select, confirmed by every
driver. An optional second return uses a guest's actual SDL F10 leave during a
live race. No simulation snapshot is carried across the next match.
"""
import json
import time

def qualify(runners,args,park,target):
    identities=[r.process.pid for r in runners];records=[]
    def release():
        for r in runners:r.client.command('net_release')
    def advance(pad,fields=8):
        nonlocal target
        for r in runners:r.client.command('set_input',pad=pad)
        target+=fields;release();return park(target)
    sessions=[[r.client.command('net_status')['session']] for r in runners]
    for attempt in range(args.room_rematches):
        old_sessions=[list(ids) for ids in sessions]
        live_leave=args.window and attempt==1
        if live_leave:
            assert all(r.client.command('game_status')['phase']==3 for r in runners)
            guest=runners[-1]
            release()
            guest.client.command('host_input',device=0,value=67,pressed=1)
        else:
            if runners[0].client.command('game_status')['phase']!=4:
                for pad in (4096,0,8192,0):advance(pad)
            assert all(r.client.command('game_status')['phase']==4 for r in runners)
            for seat,r in enumerate(runners):r.client.command('set_input',pad=8192 if seat==0 else 0)
            release()
        disconnected=set();deadline=time.monotonic()+25
        while len(disconnected)!=len(runners):
            for seat,r in enumerate(runners):
                if seat in disconnected:continue
                try:r.client.command('ping')
                except (OSError,RuntimeError):
                    r.client.close();r.client=None;disconnected.add(seat)
            if time.monotonic()>deadline:raise TimeoutError(('Old match did not return',disconnected))
            time.sleep(.01)
        for r in runners:r.attach()
        assert identities==[r.process.pid for r in runners],'Room return replaced a game process'
        assert all(r.client.command('frame')['frame']==0 for r in runners),'Rematch did not cold reset'
        assert all(r.client.command('game_status')['phase']==0 for r in runners),'Rematch carried an old session'
        assert all(r.memory(0x06000000)==bytes(65536) for r in runners),'Rematch carried native progress'
        for ids,r in zip(sessions,runners):ids.append(r.client.command('net_status')['session'])
        new_sessions=sessions
        assert all(len(ids)==attempt+2 and len(set(ids))==len(ids) for ids in new_sessions),('Missing or reused room session IDs',new_sessions)
        assert len({ids[-1] for ids in new_sessions})==1,'Peers relaunched different room sessions'
        for r in runners:r.client.command('continue')
        deadline=time.monotonic()+25
        while True:
            statuses=[r.client.command('net_status') for r in runners]
            if all(s['settled'] for s in statuses):break
            if time.monotonic()>deadline:raise TimeoutError(('Rematch profile',statuses))
            time.sleep(.01)
        target=0
        for segment in json.loads(args.route.read_text()):
            target+=segment['frames']
            for seat,r in enumerate(runners):r.client.command('set_input',pad=segment.get('pad',0) if seat==0 else 0)
            release();park(target)
            if all(r.client.command('game_status')['menu']['screen']==2 for r in runners):
                for pad in (4,0):advance(pad)
                for seat,r in enumerate(runners):r.client.command('set_input',pad=4096 if seat==0 else 0)
                target+=8;release();park(target)
                for r in runners:r.client.command('set_input',pad=16)
                for _ in range(30):
                    if all(r.client.command('game_status')['phase']==3 for r in runners):break
                    target+=80;release();park(target)
                else:raise AssertionError('Rematch did not execute a live native VS race')
                break
        else:raise AssertionError('Rematch route did not reach VS setup')
        target+=48;release();states=park(target)
        assert all(s['desync']==0 for s in states),states
        records.append({'attempt':attempt+1,'return':'guest-f10-live-leave' if live_leave else 'confirmed-results-select',
            'old_session_ids':[ids[-1] for ids in old_sessions],'new_session_id':new_sessions[0][-1],
            'same_process_ids':identities,'states':states})
    return target,states,{'passed':True,'rematches':records}
