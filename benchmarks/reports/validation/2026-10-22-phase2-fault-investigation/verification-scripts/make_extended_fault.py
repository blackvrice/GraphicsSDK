import pathlib
root=pathlib.Path.cwd();text=(root/'out/investigate_faults.py').read_text()
text=text.replace("root/'benchmarks/reports/validation/2026-10-22-phase2-fault-investigation'","root/'benchmarks/reports/validation/2026-10-22-phase2-fault-investigation/extended-intel'")
text=text.replace("[('intel-normal-1',normal),('intel-normal-2',normal),('intel-symbols',symbols)]","[('intel-normal-extended',normal)]")
text=text.replace("'420000'","'720000'").replace("'--seconds','300'","'--seconds','600'").replace("'--max-private-growth-mib','512'","'--max-private-growth-mib','1024'").replace('timeout=480','timeout=780')
prefix="""import time,pathlib
spaced_done=pathlib.Path.cwd()/'benchmarks/reports/validation/2026-10-22-phase2-fault-investigation/spaced-controls/inputs-after.json'
spaced_deadline=time.monotonic()+300
while not spaced_done.exists():
    assert time.monotonic()<spaced_deadline,'Serial native predecessor did not finish'
    time.sleep(2)
"""
assert 'intel-normal-extended' in text and "'--seconds','600'" in text
(root/'out/investigate_extended_fault.py').write_text(prefix+text)
