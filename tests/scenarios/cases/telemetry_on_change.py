"""
Scenario: Telemetry On Change

Validates that telemetry only fires when signal values actually change.

Tests:
1. Monitor telemetry endpoint (SSE stream)
2. Poll device signals repeatedly without changes
3. Verify no redundant telemetry events
4. Change device state via function call
5. Verify telemetry event fires for the change
6. Poll again without changes
7. Verify no further telemetry events

This validates the "telemetry on change" optimization - telemetry events should only
be emitted when signal values actually change, reducing network traffic.
"""

import json
import threading
from typing import Any

import requests

from .base import ScenarioBase


class TelemetryOnChange(ScenarioBase):
    """Verify telemetry only fires on signal change (not every poll)."""

    def run(self) -> None:
        """Execute telemetry on change scenario."""
        stop_event = threading.Event()
        events_lock = threading.Lock()
        telemetry_events: list[dict[str, Any]] = []
        stream_errors: list[str] = []

        def consume_sse() -> None:
            url = f"{self.base_url}/v0/events?provider_id=sim0&device_id=relayio0&signal_id=relay_ch1_state"
            try:
                with requests.get(
                    url,
                    stream=True,
                    timeout=15.0,
                    headers={"Accept": "text/event-stream"},
                ) as response:
                    response.raise_for_status()
                    for line in response.iter_lines(decode_unicode=True):
                        if stop_event.is_set():
                            break
                        if not line or not line.startswith("data: "):
                            continue
                        payload = line[6:]
                        try:
                            event = json.loads(payload)
                        except json.JSONDecodeError:
                            continue
                        with events_lock:
                            telemetry_events.append(event)
            except requests.RequestException as exc:
                if not stop_event.is_set():
                    stream_errors.append(str(exc))

        sse_thread = threading.Thread(target=consume_sse, daemon=True)
        sse_thread.start()

        try:
            # Allow stream setup and clear any initial backlog before assertions.
            self.sleep(1.0)
            with events_lock:
                telemetry_events.clear()

            # Step 1: Get initial state.
            initial_state = self.get_state("sim0", "relayio0")
            initial_ch1 = None
            for sig in initial_state["signals"]:
                if sig.get("signal_id") == "relay_ch1_state":
                    initial_ch1 = sig.get("value")
                    break

            assert initial_ch1 is not None, "relay_ch1_state not found"

            # Step 2: Poll state repeatedly without changing values.
            poll_count = 5
            for i in range(poll_count):
                state = self.get_state("sim0", "relayio0")
                ch1_value = None
                for sig in state["signals"]:
                    if sig.get("signal_id") == "relay_ch1_state":
                        ch1_value = sig.get("value")
                        break

                assert ch1_value == initial_ch1, f"Signal value changed unexpectedly on poll {i + 1}"

                self.sleep(0.1)

            # No change period should produce no relay_ch1_state telemetry events.
            self.sleep(0.5)
            with events_lock:
                unchanged_events = len(telemetry_events)
            assert unchanged_events == 0, (
                f"Expected no telemetry events during unchanged polling, saw {unchanged_events}"
            )

            # Step 3: Make an actual change - toggle relay.
            new_ch1_state = not initial_ch1
            result = self.call_function("sim0", "relayio0", "set_relay_ch1", {"enabled": new_ch1_state})
            assert result["status"] == "OK", "Failed to change relay state"

            # Step 4: Verify state change is reflected in state API.
            self.sleep(0.2)
            changed_state = self.get_state("sim0", "relayio0")
            changed_ch1 = None
            for sig in changed_state["signals"]:
                if sig.get("signal_id") == "relay_ch1_state":
                    changed_ch1 = sig.get("value")
                    break

            assert changed_ch1 == new_ch1_state, (
                f"State change not reflected: expected {new_ch1_state}, got {changed_ch1}"
            )

            # Step 5: Telemetry event should be emitted for the actual change.
            def relay_change_event_seen() -> bool:
                with events_lock:
                    for event in telemetry_events:
                        value = event.get("value", {})
                        if event.get("signal_id") != "relay_ch1_state":
                            continue
                        if value.get("type") == "bool" and value.get("bool") == new_ch1_state:
                            return True
                return False

            change_event_seen = self.poll_until(relay_change_event_seen, timeout=3.0, interval=0.1)
            assert change_event_seen, "Expected relay change telemetry event after state change"
            assert not stream_errors, f"SSE stream error: {stream_errors[-1]}"

            # Step 6: Poll again without changes; no additional relay_ch1 events expected.
            with events_lock:
                telemetry_events.clear()
            for i in range(3):
                state = self.get_state("sim0", "relayio0")
                ch1_value = None
                for sig in state["signals"]:
                    if sig.get("signal_id") == "relay_ch1_state":
                        ch1_value = sig.get("value")
                        break

                assert ch1_value == new_ch1_state, f"Signal value inconsistent on post-change poll {i + 1}"
                self.sleep(0.1)

            self.sleep(0.5)
            with events_lock:
                post_change_events = len(telemetry_events)
            assert post_change_events == 0, (
                f"Expected no new telemetry events without further changes, saw {post_change_events}"
            )
        finally:
            stop_event.set()
            sse_thread.join(timeout=2.0)
