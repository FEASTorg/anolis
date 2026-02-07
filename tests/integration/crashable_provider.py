#!/usr/bin/env python3
"""
Crashable Test Provider

A minimal ADPP provider that crashes on demand for testing provider supervision.

Crash triggers:
1. --crash-after N: Crash after N seconds
2. --crash-on-request: Crash when receiving {"crash": true} in Hello args
3. Responds to SIGTERM for graceful testing

Usage:
    python crashable_provider.py [--crash-after SECONDS] [--crash-on-request]
"""

import argparse
import json
import sys
import time
import threading
from typing import Any, Dict, Optional

# Set stdin/stdout to binary mode for framing protocol
if hasattr(sys.stdin, 'buffer'):
    sys.stdin = sys.stdin.buffer
if hasattr(sys.stdout, 'buffer'):
    sys.stdout = sys.stdout.buffer


class CrashableProvider:
    """Minimal ADPP provider that crashes on demand."""

    def __init__(self, crash_after: Optional[float] = None, crash_on_request: bool = False):
        self.crash_after = crash_after
        self.crash_on_request = crash_on_request
        self.start_time = time.time()
        self.crash_timer: Optional[threading.Timer] = None

    def log(self, message: str):
        """Log to stderr."""
        sys.stderr.write(f"[crashable_provider] {message}\n")
        sys.stderr.flush()

    def send_response(self, response: Dict[str, Any]):
        """Send a JSON response with binary length prefix (uint32_le)."""
        json_str = json.dumps(response)
        json_bytes = json_str.encode('utf-8')
        
        # Write uint32_le length prefix + payload
        length = len(json_bytes)
        length_bytes = length.to_bytes(4, byteorder='little')
        sys.stdout.write(length_bytes + json_bytes)
        sys.stdout.flush()

    def send_error(self, request_id: int, code: str, message: str):
        """Send error response."""
        self.send_response({
            "id": request_id,
            "error": {"code": code, "message": message}
        })

    def handle_hello(self, request_id: int, params: Dict[str, Any]):
        """Handle Hello request."""
        self.log("Received Hello request")

        # Check for crash trigger in params
        if self.crash_on_request and params.get("args", {}).get("crash"):
            self.log("Crash triggered via Hello args")
            self.schedule_immediate_crash()

        self.send_response({
            "id": request_id,
            "result": {
                "protocol_version": "v0",
                "provider_version": "test-crashable-1.0"
            }
        })

    def handle_list_devices(self, request_id: int):
        """Handle ListDevices request."""
        self.log("Received ListDevices request")
        self.send_response({
            "id": request_id,
            "result": {
                "devices": [
                    {
                        "device_id": "crash_device",
                        "device_type": "test_crashable",
                        "label": "Crashable Test Device"
                    }
                ]
            }
        })

    def handle_describe_device(self, request_id: int, params: Dict[str, Any]):
        """Handle DescribeDevice request."""
        device_id = params.get("device_id")
        self.log(f"Received DescribeDevice request for {device_id}")

        if device_id != "crash_device":
            self.send_error(request_id, "DEVICE_NOT_FOUND", f"Device {device_id} not found")
            return

        self.send_response({
            "id": request_id,
            "result": {
                "device": {
                    "device_id": "crash_device",
                    "device_type": "test_crashable",
                    "label": "Crashable Test Device"
                },
                "capabilities": {
                    "signals": [
                        {
                            "signal_id": "uptime",
                            "name": "Uptime",
                            "value_type": "DOUBLE",
                            "poll_hint_hz": 1.0
                        }
                    ],
                    "functions": []
                }
            }
        })

    def handle_read_signal(self, request_id: int, params: Dict[str, Any]):
        """Handle ReadSignal request."""
        device_id = params.get("device_id")
        signal_id = params.get("signal_id")
        
        if device_id != "crash_device" or signal_id != "uptime":
            self.send_error(request_id, "INVALID_SIGNAL", "Signal not found")
            return

        uptime = time.time() - self.start_time
        self.send_response({
            "id": request_id,
            "result": {
                "value": {"double_value": uptime},
                "timestamp_us": int(time.time() * 1_000_000),
                "quality": "GOOD"
            }
        })

    def schedule_immediate_crash(self):
        """Schedule crash in 100ms to allow response to send."""
        def crash():
            self.log("CRASHING NOW (as requested)")
            sys.exit(42)  # Exit with distinctive code
        
        self.crash_timer = threading.Timer(0.1, crash)
        self.crash_timer.start()

    def check_timed_crash(self):
        """Check if it's time to crash based on --crash-after."""
        if self.crash_after is not None:
            elapsed = time.time() - self.start_time
            if elapsed >= self.crash_after:
                self.log(f"CRASHING NOW (after {elapsed:.1f}s)")
                sys.exit(42)

    def handle_request(self, request: Dict[str, Any]):
        """Handle incoming ADPP request."""
        request_id = request.get("id", 0)
        method = request.get("method", "")
        params = request.get("params", {})

        if method == "Hello":
            self.handle_hello(request_id, params)
        elif method == "ListDevices":
            self.handle_list_devices(request_id)
        elif method == "DescribeDevice":
            self.handle_describe_device(request_id, params)
        elif method == "ReadSignal":
            self.handle_read_signal(request_id, params)
        else:
            self.send_error(request_id, "METHOD_NOT_FOUND", f"Unknown method: {method}")

    def read_frame(self) -> Optional[str]:
        """Read a length-prefixed frame (uint32_le + JSON bytes)."""
        # Read 4-byte little-endian uint32 length prefix
        length_bytes = sys.stdin.read(4)
        if len(length_bytes) < 4:
            self.log(f"DEBUG: read_frame() got {len(length_bytes)} length bytes (expected 4)")
            return None
        
        length = int.from_bytes(length_bytes, byteorder='little')
        self.log(f"DEBUG: Frame length = {length}")
        
        # Validate length
        if length == 0 or length > 1024 * 1024:  # Max 1 MiB
            self.log(f"ERROR: Invalid frame length: {length}")
            return None
        
        # Read exact payload bytes
        json_bytes = sys.stdin.read(length)
        if len(json_bytes) != length:
            self.log(f"ERROR: Expected {length} bytes, got {len(json_bytes)}")
            return None
        
        self.log(f"DEBUG: Read {len(json_bytes)} bytes: {json_bytes[:100]}")
        return json_bytes.decode('utf-8')

        return json_str

    def run(self):
        """Main provider loop."""
        self.log("Starting crashable provider")
        
        if self.crash_after:
            self.log(f"Will crash after {self.crash_after} seconds")
        if self.crash_on_request:
            self.log("Will crash on request via Hello args")

        try:
            while True:
                # Check for timed crash
                self.check_timed_crash()

                # Read next request
                json_str = self.read_frame()
                if json_str is None:
                    self.log("EOF on stdin, exiting")
                    break

                try:
                    request = json.loads(json_str)
                    self.handle_request(request)
                except json.JSONDecodeError as e:
                    self.log(f"ERROR: Invalid JSON: {e}")
                except Exception as e:
                    self.log(f"ERROR: Exception handling request: {e}")

        except KeyboardInterrupt:
            self.log("Interrupted, exiting gracefully")
        except Exception as e:
            self.log(f"FATAL ERROR: {e}")
            sys.exit(1)

        self.log("Shutting down gracefully")
        sys.exit(0)


def main():
    parser = argparse.ArgumentParser(description="Crashable test provider for supervision testing")
    parser.add_argument("--crash-after", type=float, help="Crash after N seconds")
    parser.add_argument("--crash-on-request", action="store_true", help="Crash when Hello includes crash=true")
    args = parser.parse_args()

    provider = CrashableProvider(
        crash_after=args.crash_after,
        crash_on_request=args.crash_on_request
    )
    provider.run()


if __name__ == "__main__":
    main()
