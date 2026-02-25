"""
Scenario: Slow SSE Client Behavior

Validates that slow SSE (Server-Sent Events) consumers don't block the runtime.

Tests:
1. Start runtime with telemetry enabled
2. Verify normal operations work
3. Simulate slow SSE client behavior (slow event consumption)
4. Verify runtime continues processing
5. Verify other clients can still access API

This scenario validates runtime responsiveness while a real SSE client is connected
and intentionally drains events slowly.
"""

import threading
import time
from typing import Any

import requests

from .base import ScenarioBase


class SlowSseClientBehavior(ScenarioBase):
    """Slow SSE consumer doesn't block runtime."""

    def run(self) -> None:
        """Execute slow SSE client scenario."""
        stop_event = threading.Event()
        events_lock = threading.Lock()
        sse_events: list[dict[str, Any]] = []
        stream_errors: list[str] = []

        def slow_sse_consumer() -> None:
            url = f"{self.base_url}/v0/events?provider_id=sim0"
            try:
                with requests.get(
                    url,
                    stream=True,
                    timeout=20.0,
                    headers={"Accept": "text/event-stream"},
                ) as response:
                    response.raise_for_status()
                    for line in response.iter_lines(decode_unicode=True):
                        if stop_event.is_set():
                            break
                        if not line or not line.startswith("data: "):
                            continue
                        # Slow-drain the stream on purpose.
                        time.sleep(0.2)
                        with events_lock:
                            sse_events.append({"raw": line[6:]})
            except requests.RequestException as exc:
                if not stop_event.is_set():
                    stream_errors.append(str(exc))

        consumer_thread = threading.Thread(target=slow_sse_consumer, daemon=True)
        consumer_thread.start()

        try:
            self.sleep(1.0)  # Give the SSE subscription time to establish.

            # Step 1: Verify baseline responsiveness
            start = time.time()
            self.get_runtime_status()
            baseline_latency = time.time() - start

            assert baseline_latency < 3.0, f"Baseline latency too high: {baseline_latency}s"

            # Step 2: Perform operations while monitoring responsiveness.
            operation_count = 10
            latencies = []

            for i in range(operation_count):
                start = time.time()

                # Mix of requests while SSE consumer is intentionally slow.
                if i % 3 == 0:
                    self.get_devices()
                elif i % 3 == 1:
                    self.get_state("sim0", "tempctl0")
                else:
                    self.get_state("sim0", "motorctl0")

                latency = time.time() - start
                latencies.append(latency)

                # Small delay to avoid overwhelming
                time.sleep(0.05)

            # Step 3: Verify latencies remain reasonable
            avg_latency = sum(latencies) / len(latencies)
            max_latency = max(latencies)

            assert avg_latency < 3.0, f"Average latency too high under load: {avg_latency}s"
            assert max_latency < 5.0, f"Max latency too high: {max_latency}s"

            # Step 4: Trigger a state change and verify runtime remains responsive.
            start = time.time()
            result = self.call_function("sim0", "relayio0", "set_relay_ch1", {"enabled": True})
            call_latency = time.time() - start

            assert result["status"] == "OK", "Function call should succeed under load"
            assert call_latency < 5.0, f"Function call latency too high: {call_latency}s"

            # Confirm the slow SSE consumer actually received events.
            def any_event_seen() -> bool:
                with events_lock:
                    return len(sse_events) > 0

            events_seen = self.poll_until(any_event_seen, timeout=3.0, interval=0.1)
            assert events_seen, "Expected SSE events while stream client was connected"
            assert not stream_errors, f"SSE stream error: {stream_errors[-1]}"

            # Step 5: Test concurrent access from multiple API clients.
            results = []
            errors = []

            def make_request(device_id, request_id):
                try:
                    state = self.get_state("sim0", device_id)
                    results.append((request_id, "success", state))
                except Exception as e:
                    errors.append((request_id, str(e)))

            # Launch multiple concurrent requests
            threads = []
            devices = ["tempctl0", "motorctl0", "relayio0", "analogsensor0"]
            for i, device in enumerate(devices):
                t = threading.Thread(target=make_request, args=(device, i))
                threads.append(t)
                t.start()

            # Wait for all to complete
            for t in threads:
                t.join(timeout=5.0)

            # Step 6: Verify all concurrent requests succeeded
            assert len(errors) == 0, f"Concurrent requests failed: {errors}"
            assert len(results) == len(devices), f"Not all concurrent requests completed: {len(results)}/{len(devices)}"

            # Step 7: Verify runtime is still responsive after load
            start = time.time()
            self.get_runtime_status()
            final_latency = time.time() - start

            assert final_latency < 3.0, f"Post-load latency too high: {final_latency}s"
        finally:
            stop_event.set()
            consumer_thread.join(timeout=2.0)
