#!/usr/bin/env python3
"""Exercise the real app's UTC grouping and PDF export with synthetic QSO."""
import os
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile
import uuid

with tempfile.TemporaryDirectory(prefix="decolog-utc-pdf-") as temp:
    root = Path(os.environ.get("DECOLOG_UTC_PDF_KEEP", temp))
    root.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_QUICK_BACKEND="software", DECODXLOG_GRAB_MS="5000", DECODXLOG_DATA_ROOT=str(root / "data"))
    args = [sys.argv[1], "--software-renderer", "--settings", str(root / "settings"), "--db", str(root / "log.sqlite"), "--port", "0"]
    subprocess.run(args + ["--show", "stats", "--grab", str(root / "init.png")], env=env, capture_output=True, text=True, timeout=30, check=True)
    with sqlite3.connect(root / "log.sqlite") as db:
        for call, when in [("K1AAA", "2027-01-01T01:10:00+02:00"), ("K1BBB", "2026-12-31T23:20:00Z"), ("K1CCC", "2026-12-31T10:30:00Z")]:
            db.execute("INSERT INTO qso(uuid,call,qso_datetime_on,band,mode,created_at,updated_at) VALUES(?,?,?,?,?,?,?)", (str(uuid.uuid4()), call, when, "20m", "CW", when, when))
    report = root / "report.pdf"
    run = subprocess.run(args + ["--show", "activityutcprobe:" + str(report), "--grab", str(root / "result.png")], env=env, capture_output=True, text=True, timeout=30, check=True)
    output = run.stdout + run.stderr
    assert 'PROBE UTC grouping restored ["day","hour"]' in output, output
    assert "PROBE activityreport pdf success=true" in output, output
    pdf = subprocess.check_output([sys.argv[2], "-layout", str(report), "-"], text=True)
    lines = [line.split() for line in pdf.splitlines()]
    assert "UTC day" in pdf and "UTC hour" in pdf, pdf
    assert ["2026-12-31", "23", "2"] in lines, pdf
    assert ["2026-12-31", "10", "1"] in lines, pdf
    assert "TypeError" not in output and "RangeError" not in output, output
    print("PASS UTC PDF: offset and UTC timestamps grouped together; second hour separate")
    print(pdf)
