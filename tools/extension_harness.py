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


class ExtensionEventGap(ValueError):
    pass
ALLOWED_PERMISSIONS = frozenset({"qso.read", "status.read", "worked.read", "tracker.read", "qso.events"})
REQUIRED = frozenset({"schemaVersion", "apiVersion", "id", "version", "entrypoint", "permissions"})


def validate_manifest(value):
    if not isinstance(value, dict) or not REQUIRED.issubset(value) or set(value) - REQUIRED - {"dependencies"}:
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
    dependencies = value.get("dependencies", [])
    if (not isinstance(dependencies, list) or len(dependencies) > 16 or
            any(not isinstance(name, str) or not re.fullmatch(r"[A-Za-z0-9_-]+\.py", name) for name in dependencies) or
            len(set(dependencies)) != len(dependencies) or entry in dependencies):
        raise ValueError("invalid extension dependencies")
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
    for name in value.get("dependencies", []):
        dependency = (directory / name).resolve(strict=True)
        if dependency.parent != directory or not dependency.is_file():
            raise ValueError("dependency must be a file in the manifest directory")
    return value


def contains_secret(value, secret):
    """Check decoded JSON keys and values, including escaped or nested strings."""
    pending = [value]
    while pending:
        item = pending.pop()
        if isinstance(item, str):
            if secret in item:
                return True
        elif isinstance(item, list):
            pending.extend(item)
        elif isinstance(item, dict):
            pending.extend(item.keys())
            pending.extend(item.values())
    return False


def supported_scope(manifest):
    permissions = set(manifest["permissions"])
    if permissions == {"qso.events"}:
        return "event_counts"
    if "qso.read" in permissions and "tracker.read" not in permissions:
        return "recent_sample"
    if "tracker.read" in permissions and "qso.read" not in permissions:
        return "tracker_snapshot"
    raise ValueError("the harness requires one supported example capability")


def run_extension(manifest_path, token, port, timeout, state_path=None, accept_gap=False):
    manifest_path = pathlib.Path(manifest_path).resolve(strict=True)
    manifest = load_manifest(manifest_path)
    expected_scope = supported_scope(manifest)
    if accept_gap and expected_scope != "event_counts":
        raise ValueError("gap recovery requires an event extension")
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
            elif expected_scope == "event_counts":
                state = pathlib.Path(state_path) if state_path is not None else pathlib.Path(directory) / "temporary-state.sqlite"
                if state.is_symlink() or (state.exists() and not state.is_file()):
                    raise ValueError("invalid extension state path")
                command += ["--state", str(state)]
                if accept_gap: command += ["--accept-gap"]
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
                group_denied = False
                if os.name == "posix":
                    try:
                        os.killpg(process.pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
                    except PermissionError:
                        # Reap the leader if possible, but do not report success:
                        # surviving descendants cannot be ruled out.
                        group_denied = True
                        if process.poll() is None:
                            try:
                                process.kill()
                            except (PermissionError, ProcessLookupError):
                                pass
                elif process.poll() is None:
                    process.kill()
                try:
                    process.wait(timeout=1 if group_denied else None)
                except subprocess.TimeoutExpired:
                    raise ValueError("extension process group cleanup denied; leader still running")
                if group_denied:
                    raise ValueError("extension process group cleanup denied; descendants may remain")
            if stdout.stat().st_size > MAX_OUTPUT or stderr.stat().st_size > MAX_OUTPUT:
                raise ValueError("extension exceeded output limit")
            if process.returncode != 0:
                err.seek(0)
                if expected_scope == "event_counts" and err.read(MAX_OUTPUT + 1).replace(b"\r\n", b"\n") == b"Event counter unavailable: EventGap\n":
                    raise ExtensionEventGap("event history was pruned")
                raise ValueError("extension failed")
            out.seek(0)
            raw_output = out.read(MAX_OUTPUT + 1)
            if token.encode("utf-8") in raw_output:
                raise ValueError("extension output contains a credential")
            output = json.loads(raw_output)
            if contains_secret(output, token):
                raise ValueError("extension output contains a credential")
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
    except (OSError, ValueError, TypeError, RecursionError, json.JSONDecodeError) as exc:
        print(f"Extension check failed: {type(exc).__name__}", file=sys.stderr)
        return 1
    print(json.dumps(result, ensure_ascii=False, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
