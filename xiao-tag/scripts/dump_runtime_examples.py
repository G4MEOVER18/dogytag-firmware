#!/usr/bin/env python3
import json

example = {
  "preflight": {
    "radio_ready": 1,
    "keys": {"dev": 1, "join": 1, "app": 1},
    "backend": "RadioLib-adapter",
    "backend_state": "adapter-ready-lib-detected",
    "initialized": 1,
    "join_supported": 1,
    "requested": 0,
    "attempts": 0,
  },
  "runtime_config": {
    "beacon": "hello-433",
    "beacon_len": 9,
    "loaded": 1,
    "dirty": 0,
    "join_autostart": 1,
    "state": "saved",
  }
}
print(json.dumps(example, indent=2))
