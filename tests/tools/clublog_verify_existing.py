#!/usr/bin/env python3
"""Read-only check of staged Club Log QSO IDs; token is read from stdin.

This records presence, not each contact's upload outcome. It never uploads,
deletes, or infers which records were accepted versus duplicates.
"""
import configparser
import datetime
import hashlib
import json
import os
import pathlib
import re
import sys
import tempfile
import urllib.error
import urllib.request

if len(sys.argv) != 2:
    raise SystemExit("usage: clublog_verify_existing.py STAGING_DIR")
root = pathlib.Path(sys.argv[1])
manifest = json.loads((root / "manifest-private.json").read_text())
journal = configparser.ConfigParser()
journal.read(root / "live-journal.ini")
checkpoint = journal["clubLogRestUpload"]
history = json.loads((root / "web-history-evidence.json").read_text())
if (checkpoint.get("phase") != "queued" or history.get("upload_id") != checkpoint.get("uploadid")
        or manifest.get("sha256") != checkpoint.get("payloadsha256")
        or manifest.get("count") != 50 or history.get("accepted", 0) + history.get("duplicates", 0) != 50):
    raise SystemExit("Checkpoint or aggregate evidence does not match")
station = manifest.get("callsign", "")
if not re.fullmatch(r"[A-Z0-9]+(?:/[A-Z0-9]+)*", station):
    raise SystemExit("Invalid station callsign")
payload = (root / "selected-private.adi").read_bytes()
if hashlib.sha256(payload).hexdigest() != manifest.get("sha256"):
    raise SystemExit("Staged ADIF does not match its fingerprint")
raw = payload.decode("utf-8")
records = []
for record in re.split(r"(?i)<eor\s*>", raw):
    def field(name):
        match = re.search(r"(?i)<" + name + r":(\d+)(?::[^>]*)?>", record)
        return record[match.end():match.end() + int(match.group(1))] if match else ""
    call, date, qsoid = field("CALL").upper(), field("QSO_DATE"), field("APP_CLUBLOG_QSOID")
    if not call and not date and not qsoid:
        continue
    if not re.fullmatch(r"[A-Z0-9]+(?:/[A-Z0-9]+)*", call) or not re.fullmatch(r"\d{8}", date) or not qsoid.isdigit():
        raise SystemExit("Invalid staged record")
    day = date[:4] + "-" + date[4:6] + "-" + date[6:]
    records.append((qsoid, {"dxcall": call, "from": day + "T00:00:00Z", "to": day + "T23:59:59Z"}))
if len(records) != 50 or len({row[0] for row in records}) != 50:
    raise SystemExit("Expected 50 unique staged identifiers")
if {row[0] for row in records} != set(manifest.get("existingQsoIds", [])):
    raise SystemExit("Staged identifiers do not match the manifest")
token = sys.stdin.readline().strip()
if not token.startswith("cl_"):
    raise SystemExit("Personal token required on stdin")
body = json.dumps({"filters": [row[1] for row in records]}).encode()
url = "https://clublog.org/api/v1/logs/" + station + "/qsos/query"
request = urllib.request.Request(url, data=body, method="POST",
                                 headers={"Authorization": "Bearer " + token, "Content-Type": "application/json"})
try:
    with urllib.request.urlopen(request, timeout=30) as response:
        result = json.load(response)
except urllib.error.HTTPError as error:
    raise SystemExit("Read-only QSO query failed: HTTP " + str(error.code))
except (OSError, ValueError):
    raise SystemExit("Read-only QSO query failed")
rows = result.get("results") if isinstance(result, dict) else None
if not isinstance(rows, list) or len(rows) != 50:
    raise SystemExit("Incomplete query result")
def key(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"))
matches = {}
for row in rows:
    if not isinstance(row, dict) or not isinstance(row.get("filter"), dict) or not isinstance(row.get("qsos"), list):
        raise SystemExit("Invalid query result")
    if len(row["qsos"]) >= 100:
        raise SystemExit("A query may have been truncated")
    ids = {str(qso.get("qsoid")) for qso in row["qsos"] if isinstance(qso, dict) and qso.get("qsoid") is not None}
    filter_key = key(row["filter"])
    if filter_key in matches and matches[filter_key] != ids:
        raise SystemExit("Conflicting duplicate query results")
    matches[filter_key] = ids
if any(qsoid not in matches.get(key(filter_), set()) for qsoid, filter_ in records):
    raise SystemExit("At least one original identifier was not verified")
evidence = {"source": "Club Log REST QSO query", "upload_id": checkpoint.get("uploadid"),
            "sha256": manifest["sha256"], "checked_at_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
            "count": len(records), "original_qsoids_present": [qsoid for qsoid, _ in records],
            "per_qso_upload_outcome": "not returned by this query"}
fd, temp = tempfile.mkstemp(prefix=".presence-", dir=root)
try:
    os.fchmod(fd, 0o600)
    with os.fdopen(fd, "w") as output:
        json.dump(evidence, output, indent=2)
        output.write("\n")
        output.flush()
        os.fsync(output.fileno())
    os.replace(temp, root / "presence-evidence-private.json")
finally:
    if os.path.exists(temp):
        os.unlink(temp)
print("Verified 50/50 original QSO IDs in their matching read-only queries; no individual upload outcomes inferred")
