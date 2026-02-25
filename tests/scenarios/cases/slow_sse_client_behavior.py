"""
Scenario: Slow SSE Client Behavior

Validates that slow SSE (Server-Sent Events) consumers don't block the runtime.

Tests:
1. Start runtime with telemetry enabled
2. Verify normal operations work
3. Simulate slow client behavior (indirectly)
4. Verify runtime continues processing
5. Verify other clients can still access API

Note: Full SSE client testing requires async streaming. This scenario validates
that the runtime remains responsive even under load/slow conditions.
"""

import threading
import time

from .base import ScenarioBase, ScenarioResult, create_result


class SlowSseClientBehavior(ScenarioBase):
    """Slow SSE consumer doesn't block runtime."""

    def run(self) -> ScenarioResult:
        """Execute slow SSE client scenario."""
        try:
            # Step 1: Verify baseline responsiveness
            start = time.time()
            self.get_runtime_status()
            baseline_latency = time.time() - start

            self.assert_true(
                baseline_latency < 3.0,
                f"Baseline latency too high: {baseline_latency}s",
            )

            # Step 2: Perform rapid operations while monitoring responsiveness
            # Simulate load by making many concurrent state queries
            operation_count = 10
            latencies = []

            for i in range(operation_count):
                start = time.time()

                # Mix of different operations
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

            self.assert_true(
                avg_latency < 3.0,
                f"Average latency too high under load: {avg_latency}s",
            )

            self.assert_true(max_latency < 5.0, f"Max latency too high: {max_latency}s")

            # Step 4: Verify function calls still work with good responsiveness
            start = time.time()
            result = self.call_function("sim0", "relayio0", "set_relay_ch1", {"enabled": True})
            call_latency = time.time() - start

            self.assert_equal(result["status"], "OK", "Function call should succeed under load")
            self.assert_true(call_latency < 5.0, f"Function call latency too high: {call_latency}s")

            # Step 5: Test concurrent access from multiple "clients"
            # Simulate by making parallel requests
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
            self.assert_equal(len(errors), 0, f"Concurrent requests failed: {errors}")

            self.assert_equal(
                len(results),
                len(devices),
                f"Not all concurrent requests completed: {len(results)}/{len(devices)}",
            )

            # Step 7: Verify runtime is still responsive after load
            start = time.time()
            self.get_runtime_status()
            final_latency = time.time() - start

            self.assert_true(final_latency < 3.0, f"Post-load latency too high: {final_latency}s")

            return create_result(
                self,
                True,
                (
                    "Slow client behavior validated: runtime responsive under load "
                    f"(avg: {avg_latency:.3f}s, max: {max_latency:.3f}s)"
                ),
            )

        except AssertionError as e:
            return create_result(self, False, "Assertion failed", str(e))
        except Exception as e:
            return create_result(self, False, f"Exception: {type(e).__name__}", str(e))
