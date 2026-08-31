#!/usr/bin/env python3
import json
import subprocess
import sys

if len(sys.argv) != 2:
    raise SystemExit("usage: test_worker_ipc.py <amt_worker>")

proc = subprocess.Popen(
    [sys.argv[1], "--stdio"],
    stdin=subprocess.PIPE,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
    text=True,
    bufsize=1,
)
assert proc.stdin is not None and proc.stdout is not None

def exchange(message):
    proc.stdin.write(json.dumps(message, separators=(",", ":")) + "\n")
    proc.stdin.flush()
    line = proc.stdout.readline()
    if not line:
        raise RuntimeError("worker closed stdout")
    return json.loads(line)

health = exchange({"protocol": 1, "requestId": "phase0-health", "type": "health", "payload": {}})
assert health["protocol"] == 1
assert health["requestId"] == "phase0-health"
assert health["ok"] is True
assert health["payload"]["status"] == "ok"
assert health["payload"]["version"]

unsupported = exchange({"protocol": 1, "requestId": "phase0-unknown", "type": "unknown", "payload": {}})
assert unsupported["ok"] is False
assert unsupported["error"] == "unsupported_type"

shutdown = exchange({"protocol": 1, "requestId": "phase0-shutdown", "type": "shutdown", "payload": {}})
assert shutdown["ok"] is True
assert shutdown["payload"]["status"] == "shutting_down"
proc.stdin.close()
return_code = proc.wait(timeout=5)
assert return_code == 0, proc.stderr.read() if proc.stderr else ""
print("worker IPC smoke: ok")
