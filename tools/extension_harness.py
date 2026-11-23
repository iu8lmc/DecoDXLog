#!/usr/bin/env python3
"""Developer check for the experimental DecoDXLog Python extension manifest."""
import argparse
import json
import os
import pathlib
import re
import signal
import subprocess
import sys
import tempfile
import time

MAX_MANIFEST = 16 * 1024
MAX_OUTPUT = 1024 * 1024
ALLOWED_PERMISSIONS = frozenset({"qso.read", "status.read", "worked.read", "tracker.read"})
REQUIRED = frozenset({"schemaVersion", "apiVersion", "id", "version", "entrypoint", "permissions"})


def validate_manifest(value):
    if not isinstance(value, dict) or set(value) != REQUIRED:
        raise ValueError("unsupported manifest fields")
    if type(value["schemaVersion"]) is not int or value["schemaVersion"] != 1:
        raise ValueError("unsupported manifest schema")
    if type(value["apiVersion"]) is not int or value["apiVersion"] != 1:
        raise ValueError("unsupported API version")
    if not isinstance(value["id"], str) or not re.fullmatch(r"[a-z0-9]+(?:[.-][a-z0-9]+)+", value["id"]) or len(value["id"]) > 128:
        raise ValueError("invalid extension identity")
    if not isinstance(value["version"], str) or not re.fullmatch(r"(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)", value["version"]):
        raise ValueError("invalid extension version")
    entry = value["entrypoint"]
    if not isinstance(entry, str) or not re.fullmatch(r"[A-Za-z0-9_-]+\.py", entry):
        raise ValueError("entrypoint must be a Python file in the manifest directory")
    permissions = value["permissions"]
    if (not isinstance(permissions, list) or not permissions or
            any(not isinstance(p, str) or p not in ALLOWED_PERMISSIONS for p in permissions) or
            len(set(permissions)) != len(permissions)):
        raise ValueError("unsupported or duplicate permission")
    return value


def load_manifest(path):
    path = pathlib.Path(path)
    with path.open("rb") as f:
        raw = f.read(MAX_MANIFEST + 1)
    if len(raw) > MAX_MANIFEST:
        raise ValueError("manifest is too large")
    def unique_object(pairs):
        result = {}
        for key, item in pairs:
            if key in result:
                raise ValueError("duplicate manifest field")
            result[key] = item
        return result
    value = validate_manifest(json.loads(raw, object_pairs_hook=unique_object))
    directory = path.resolve(strict=True).parent
    entry = (directory / value["entrypoint"]).resolve(strict=True)
    if entry.parent != directory or not entry.is_file():
        raise ValueError("entrypoint must be a file in the manifest directory")
    return value


def run_extension(manifest_path, token, port, timeout):
    manifest_path = pathlib.Path(manifest_path).resolve(strict=True)
    manifest = load_manifest(manifest_path)
    permissions = set(manifest["permissions"])
    if "qso.read" in permissions and "tracker.read" not in permissions:
        expected_scope = "recent_sample"
    elif "tracker.read" in permissions and "qso.read" not in permissions:
        expected_scope = "tracker_snapshot"
    else:
        raise ValueError("the harness requires one supported example capability")
    if not token or not 1 <= port <= 65535 or not 0.1 <= timeout <= 60:
        raise ValueError("invalid token, port or timeout")
    env = {k: os.environ[k] for k in ("PATH", "SYSTEMROOT", "WINDIR") if k in os.environ}
    env["DECODXLOG_READ_TOKEN"] = token
    entry = manifest_path.parent / manifest["entrypoint"]
    with tempfile.TemporaryDirectory(prefix="decodxlog-extension-check-") as directory:
        stdout = pathlib.Path(directory) / "stdout"
        stderr = pathlib.Path(directory) / "stderr"
        with stdout.open("w+b") as out, stderr.open("w+b") as err:
            command = [sys.executable, str(entry), "--port", str(port)]
            if expected_scope == "recent_sample":
                command += ["--limit", "100"]
            process = subprocess.Popen(command, cwd=directory, env=env, stdin=subprocess.DEVNULL, stdout=out, stderr=err,
                                       start_new_session=(os.name == "posix"))
            try:
                deadline = time.monotonic() + timeout
                while process.poll() is None:
                    if time.monotonic() >= deadline or stdout.stat().st_size > MAX_OUTPUT or stderr.stat().st_size > MAX_OUTPUT:
                        raise ValueError("extension timed out or exceeded output limit")
                    time.sleep(0.02)
            finally:
                # Kill the session's original process group even if its leader exited;
                # children can otherwise keep running after success, failure or timeout.
                if os.name == "posix":
                    try:
                        os.killpg(process.pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
                elif process.poll() is None:
                    process.kill()
                process.wait()
            if stdout.stat().st_size > MAX_OUTPUT or stderr.stat().st_size > MAX_OUTPUT:
                raise ValueError("extension exceeded output limit")
            if process.returncode != 0:
                raise ValueError("extension failed")
            out.seek(0)
            raw_output = out.read(MAX_OUTPUT + 1)
            if token.encode("utf-8") in raw_output:
                raise ValueError("extension output contains a credential")
            output = json.loads(raw_output)
    if not isinstance(output, dict) or output.get("scope") != expected_scope:
        raise ValueError("unsupported extension output")
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=pathlib.Path)
    parser.add_argument("--port", type=int)
    parser.add_argument("--timeout", type=float, default=10)
    args = parser.parse_args()
    try:
        if args.port is None:
            manifest = load_manifest(args.manifest)
            result = {"manifestValid": True, "id": manifest["id"], "apiVersion": manifest["apiVersion"]}
        else:
            result = run_extension(args.manifest, os.environ.get("DECODXLOG_READ_TOKEN", ""), args.port, args.timeout)
    except (OSError, ValueError, TypeError, json.JSONDecodeError) as exc:
        print(f"Extension check failed: {type(exc).__name__}", file=sys.stderr)
        return 1
    print(json.dumps(result, ensure_ascii=False, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
