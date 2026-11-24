import os,subprocess,tempfile,sqlite3,uuid,sys
from pathlib import Path
p=Path(tempfile.mkdtemp(prefix='decolog-power-pdf-')); app=sys.argv[1]; env=dict(os.environ,QT_QPA_PLATFORM='offscreen'); args=[app,'--software-renderer','--settings',str(p/'settings'),'--db',str(p/'log.sqlite'),'--port','0']
subprocess.run(args+['--show','stats','--grab',str(p/'first.png')],env=env,capture_output=True,text=True,timeout=30,check=True)
c=sqlite3.connect(p/'log.sqlite')
for i,(band,value) in enumerate((('20m',5),('20m',10),('20m',None),('40m',None),('80m',0))): c.execute('insert into qso(uuid,call,qso_datetime_on,band,mode,tx_pwr,created_at,updated_at) values(?,?,?,?,?,?,?,?)',(str(uuid.uuid4()),'K1AA'+str(i),'2026-01-01T12:00:00Z',band,'CW',value,'2026-01-01T12:00:00Z','2026-01-01T12:00:00Z'))
c.commit();c.close()
a=subprocess.run(args+['--show','activitypowerprobe:'+str(p/'report.pdf'),'--grab',str(p/'second.png')],env=env,capture_output=True,text=True,timeout=30,check=True); print('DIR',p); print('\n'.join(x for x in (a.stdout+a.stderr).splitlines() if 'PROBE' in x)); pdf=subprocess.check_output([sys.argv[2],'-layout',str(p/'report.pdf'),'-'],text=True); print(pdf); assert '7.5' in pdf and '(not recorded)' in pdf and 'Excluded' in pdf

assert "PROBE activityreport rendered 20m · 7.50 · 2 · 1" in a.stderr
assert "PROBE activityreport rendered second 40m · — · 0 · 1" in a.stderr
assert "PROBE activityreport rendered third 80m · 0.00 · 1 · 0" in a.stderr
assert any(line.split()==['80m','0','1','0'] for line in pdf.splitlines())
assert "PROBE activityreport pdf success=true" in a.stderr
