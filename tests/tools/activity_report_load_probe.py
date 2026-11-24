import os,subprocess,tempfile,sqlite3,re,json,sys
from pathlib import Path
temporary=tempfile.TemporaryDirectory(prefix='decolog-report-load-'); p=Path(temporary.name); env=dict(os.environ,QT_QPA_PLATFORM='offscreen',QT_QUICK_BACKEND='software'); app=sys.argv[1]; args=[app,'--software-renderer','--settings',str(p/'settings'),'--db',str(p/'log.sqlite'),'--port','0']
def run(show):
 a=subprocess.run(args+['--show',show,'--grab',str(p/'grab.png')],env=env,capture_output=True,text=True,timeout=50,check=True); m=re.search(r'PROBE reportload (\{.*\})',a.stderr); return json.loads(m.group(1)) if m else None
run('stats'); c=sqlite3.connect(p/'log.sqlite')
def seed(start,count):
 c.execute("WITH RECURSIVE n(i) AS (VALUES(?) UNION ALL SELECT i+1 FROM n WHERE i<?) INSERT INTO qso(uuid,call,qso_datetime_on,band,mode,created_at,updated_at,tx_pwr) SELECT printf('fixture-%d',i),printf('K%d',i),'2026-01-01T12:00:00Z','20m','CW','2026-01-01','2026-01-01',10 FROM n",(start,start+count-1));c.commit()
seed(1,100); a=run('activitypowerloadprobe'); seed(101,499900); b=run('activitypowerloadprobe'); print('small',a,'large',b,'path',p); assert a and b and not a['error'] and not b['error']; assert a['rows'][0]['samples']==100 and b['rows'][0]['samples']==500000 and b['rows'][0]['value']==10; assert b['elapsed']<100 or b['ticks']>0
c.close(); temporary.cleanup()
