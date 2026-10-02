from pathlib import Path
import datetime,json,os,platform,subprocess,time
root=Path(__file__).resolve().parent

def compilers():
    result=[]
    for entry in Path('/proc').iterdir():
        if not entry.name.isdigit():continue
        try:
            if (entry/'comm').read_text().strip() in ('cc1plus','clang','clang++','g++'):
                status=(entry/'stat').read_text().split()
                if status[2]!='Z':result.append(int(entry.name))
        except (OSError,ProcessLookupError):pass
    return result

with (root/'abba.log').open('w') as log:
    def record(text):log.write(text+'\n');log.flush();print(text,flush=True)
    record('UTC '+datetime.datetime.now(datetime.timezone.utc).isoformat())
    record('Platform '+platform.platform())
    record('Host affinity '+str(sorted(os.sched_getaffinity(0))))
    record('cgroup cpu.max '+Path('/sys/fs/cgroup/cpu.max').read_text().strip())
    record('cgroup cpu.stat before '+json.dumps(Path('/sys/fs/cgroup/cpu.stat').read_text()))
    assert not compilers(),'Competing compiler process is active; defer the measurement.'
    for label,filename in [('A1','baseline'),('B1','candidate-frozen'),('B2','candidate-frozen'),('A2','baseline')]:
        active=compilers();record(label+' competing_compilers_before='+str(active))
        assert not active,'Competing compilation began; this run is not an idle comparison.'
        started=time.monotonic()
        output=subprocess.check_output(['taskset','-c','4',str(root/filename)],text=True)
        (root/(label+'.log')).write_text(output)
        record(label+' elapsed_seconds='+str(time.monotonic()-started))
        record(output.rstrip())
        active=compilers();record(label+' competing_compilers_after='+str(active))
        assert not active,'Competing compilation began; this run is not an idle comparison.'
    record('cgroup cpu.stat after '+json.dumps(Path('/sys/fs/cgroup/cpu.stat').read_text()))
