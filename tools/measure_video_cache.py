"""Serial balanced A/B and A/A process-CPU measurements of the real SDL host.

Uses dummy speakers/video, normal frame pacing and identical enabled features.
Measures CPU consumed by the owned runtime, not wall time or sampler output.
Each timed block restores the same current-build moving-race fixture first.
The shared mutex, compiler check and load gate reject competing workloads.
"""
import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
import os
from pathlib import Path
import statistics
import sys
import time

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('framework','runtime','rom','route','out'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--players',type=int,choices=(2,3,4),required=True)
    p.add_argument('--color-package',type=Path)
    p.add_argument('--block-fields',type=int,default=90)
    p.add_argument('--blocks',type=int,default=5)
    p.add_argument('--max-background',type=float,default=.30,
        help='Maximum background CPU fraction; at least two idle cores are always required (ceiling .75)')
    a=p.parse_args()
    if os.name!='nt':p.error('Windows process-CPU clock required')
    if not 30<=a.block_fields<=90 or not 3<=a.blocks<=20:p.error('Use 30..90 fields and 3..20 blocks')
    if not .10<=a.max_background<=.75:p.error('Background CPU limit must be 0.10..0.75')
    a.out=a.out.resolve();a.out.mkdir(parents=True,exist_ok=False)
    for path in (a.out,Path(os.environ.get('TEMP','C:/')),Path(os.environ.get('TMP','C:/'))):
        if path.drive.upper() not in ('F:','G:'):p.error('Output and TEMP/TMP must resolve to F: or G:')
    sys.path.insert(0,str(a.framework.resolve()/'tools'));from cosim import Runner
    k=c.WinDLL('kernel32',use_last_error=True)
    k.GetProcessTimes.argtypes=[w.HANDLE,*([c.POINTER(w.FILETIME)]*4)]
    k.GetSystemTimes.argtypes=[c.POINTER(w.FILETIME)]*3
    k.CreateMutexW.argtypes=[c.c_void_p,w.BOOL,w.LPCWSTR];k.CreateMutexW.restype=w.HANDLE
    k.CloseHandle.argtypes=[w.HANDLE]
    k.ReleaseMutex.argtypes=[w.HANDLE]
    k.CreateToolhelp32Snapshot.argtypes=[w.DWORD,w.DWORD];k.CreateToolhelp32Snapshot.restype=w.HANDLE
    class Process(c.Structure):
        _fields_=[('size',w.DWORD),('usage',w.DWORD),('pid',w.DWORD),('heap',c.c_size_t),
            ('module',w.DWORD),('threads',w.DWORD),('parent',w.DWORD),('priority',w.LONG),
            ('flags',w.DWORD),('name',w.WCHAR*260)]
    k.Process32FirstW.argtypes=[w.HANDLE,c.POINTER(Process)]
    k.Process32NextW.argtypes=[w.HANDLE,c.POINTER(Process)]
    def no_compiler():
        handle=k.CreateToolhelp32Snapshot(2,0)
        if handle==c.c_void_p(-1).value:raise c.WinError(c.get_last_error())
        compilers=[]
        try:
            entry=Process();entry.size=c.sizeof(entry);valid=k.Process32FirstW(handle,c.byref(entry))
            while valid:
                if entry.name.lower() in ('cl.exe','cc1.exe','cc1plus.exe','clang.exe','clang++.exe','rustc.exe','link.exe','lld-link.exe'):
                    compilers.append({'pid':entry.pid,'name':entry.name})
                valid=k.Process32NextW(handle,c.byref(entry))
        finally:k.CloseHandle(handle)
        assert not compilers,('Competing build workload',compilers)
    def value(t):return (t.dwHighDateTime<<32)|t.dwLowDateTime
    def cpu(process):
        times=[w.FILETIME() for _ in range(4)]
        if not k.GetProcessTimes(w.HANDLE(int(process._handle)),*(c.byref(t) for t in times)):raise c.WinError(c.get_last_error())
        return (value(times[2])+value(times[3]))/10000000
    def system():
        times=[w.FILETIME() for _ in range(3)]
        if not k.GetSystemTimes(*(c.byref(t) for t in times)):raise c.WinError(c.get_last_error())
        return value(times[0]),value(times[1])+value(times[2])
    def quiet():
        no_compiler()
        samples=[]
        for _ in range(3):
            idle,total=system();time.sleep(1);end_idle,end_total=system()
            samples.append(1-(end_idle-idle)/max(1,end_total-total))
        limit=min(a.max_background,1-2/max(2,os.cpu_count()))
        assert max(samples)<limit,('Insufficient CPU headroom for measurement',samples,limit)
        return samples
    mutex=k.CreateMutexW(None,True,'Global\\RecompBenchmark')
    if not mutex or c.get_last_error()==183:
        if mutex:k.CloseHandle(mutex)
        raise RuntimeError('Another serialized recomp benchmark owns the measurement window')
    report={'passed':False,'scope':__doc__,'players':a.players,'runs':[],'background_limit':a.max_background,
        'logical_cpus':os.cpu_count(),
        'runtime_sha256':hashlib.sha256(a.runtime.read_bytes()).hexdigest()}
    baseline=None;fixture=a.out/'moving.state'
    try:
        # A = exact frozen-view cache; B = always rebuild the view. Balanced
        # adjacent AB/BA pairs are bracketed by two independent A/A controls.
        order=[1,1,1,0,0,1,0,1,1,0,1,1]
        for index,enabled in enumerate(order):
            load=quiet();folder=a.out/f'{index:02d}-cache{enabled}'
            extra=['--versus',str(a.players),'--versus-course','11','--versus-video-cache',str(enabled)]
            if a.color_package:extra+=['--install-mod',str(a.color_package.resolve()),'--enable-mod','zero-racers.full-color:full-color']
            with Runner(a.runtime,a.rom,'native',16131,folder,extra=extra,window=True) as r:
                q=r.client;q.command('rewind_enable',value=0)
                if not fixture.exists():
                    remaining=2600
                    for segment in json.loads(a.route.read_text()):
                        count=min(remaining,segment['frames']);q.command('set_input',pad=segment.get('pad',0))
                        while count:
                            n=min(32,count);q.command('replay_frames',frames=n);count-=n;remaining-=n
                            if q.command('game_status')['phase']==3:count=remaining=0;break
                        if not remaining:break
                    assert q.command('game_status')['phase']==3
                    q.command('save_state',path=fixture.as_posix())
                elapsed=used=0
                for block in range(a.blocks):
                    q.command('load_state',path=fixture.as_posix())
                    q.command('game_inputs',**{f'pad{i}':16 for i in range(a.players)})
                    start=time.perf_counter();begin=cpu(r.process)
                    q.run_frames(a.block_fields)
                    used+=cpu(r.process)-begin;elapsed+=time.perf_counter()-start
                status=q.command('game_status');assert status['phase']==3,('Workload ended before measurement',status)
                no_compiler()
                q.command('save_state',path=(folder/'after.state').as_posix())
                artifacts=[(folder/'after.state').read_bytes(),r.snapshot(q.command('frame')['frame'],['audio'])['audio']]
                for eye in range(2):
                    for seat in range(a.players):
                        shot=folder/f'p{seat}-eye{eye}.png';q.command('screenshot',game=1,seat=seat,eye=eye,path=shot.as_posix())
                        artifacts.append(shot.read_bytes())
                if baseline is None:baseline=artifacts
                assert artifacts==baseline,'A/B authoritative state, PCM or independent eye mismatch'
                row={'index':index,'cache':enabled,'cpu_seconds':used,'wall_seconds':elapsed,
                     'fields':a.blocks*a.block_fields,'background_busy':load,'state':status}
                report['runs'].append(row);print(json.dumps({k:row[k] for k in ('index','cache','cpu_seconds')}),flush=True)
        runs=report['runs'];cpu_at=lambda i:runs[i]['cpu_seconds']
        controls=[abs(cpu_at(x)-cpu_at(y))/statistics.mean([cpu_at(x),cpu_at(y)]) for x,y in ((0,1),(10,11))]
        gains=[(cpu_at(b)-cpu_at(a_index))/cpu_at(b) for a_index,b in ((2,3),(5,4),(7,6),(8,9))]
        gain=statistics.median(gains);noise=max(controls)
        report.update(aa_relative_noise=controls,paired_relative_cpu_reduction=gains,
            median_cpu_reduction=gain,accepted=all(v>0 for v in gains) and gain>noise+.02,passed=True)
    except Exception as error:report['error']=repr(error)
    finally:k.ReleaseMutex(mutex);k.CloseHandle(mutex)
    (a.out/'report.json').write_text(json.dumps(report,indent=2))
    print(json.dumps({k:report.get(k) for k in ('passed','accepted','median_cpu_reduction','aa_relative_noise','error')},indent=2))
    return 0 if report['passed'] else 1

if __name__=='__main__':raise SystemExit(main())
