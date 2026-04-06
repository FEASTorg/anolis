#!/usr/bin/env python3
"""Minimal programmatic client example for telemetry export MVP service."""

from __future__ import annotations

import argparse
import json
import sys

import requests


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Query Anolis telemetry export service")
    parser.add_argument("--base-url", default="http://127.0.0.1:8091", help="Export service base URL")
    parser.add_argument("--token", default="export-dev-token", help="Bearer token")
    parser.add_argument("--start", required=True, help="RFC3339 UTC start timestamp")
    parser.add_argument("--end", required=True, help="RFC3339 UTC end timestamp")
    parser.add_argument("--format", choices=["json", "csv", "ndjson"], default="json")
    parser.add_argument("--runtime", action="append", default=[], help="runtime_name filter (repeatable)")
    parser.add_argument("--provider", action="append", default=[], help="provider_id filter (repeatable)")
    parser.add_argument("--device", action="append", default=[], help="device_id filter (repeatable)")
    parser.add_argument("--signal", action="append", default=[], help="signal_id filter (repeatable)")
    parser.add_argument("--requester", default="example-client", help="Requester ID for audit metadata")
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    body: dict[str, object] = {
        "time_range": {
            "start": args.start,
            "end": args.end,
        },
        "resolution": {
            "mode": "raw_event",
        },
        "format": args.format,
    }

    selector: dict[str, list[str]] = {}
    if args.runtime:
        selector["runtime_names"] = args.runtime
    if args.provider:
        selector["provider_ids"] = args.provider
    if args.device:
        selector["device_ids"] = args.device
    if args.signal:
        selector["signal_ids"] = args.signal
    if selector:
        body["selector"] = selector

    response = requests.post(
        f"{args.base_url.rstrip('/')}/v1/exports/signals:query",
        headers={
            "Content-Type": "application/json",
            "Authorization": f"Bearer {args.token}",
            "X-Requester-Id": args.requester,
        },
        json=body,
        timeout=20,
    )

    request_id = response.headers.get("X-Request-Id", "")
    if request_id:
        print(f"request_id={request_id}", file=sys.stderr)
    export_id = response.headers.get("X-Export-Id", "")
    if export_id:
        print(f"export_id={export_id}", file=sys.stderr)
    manifest_hash = response.headers.get("X-Export-Manifest-Hash", "")
    if manifest_hash:
        print(f"manifest_hash={manifest_hash}", file=sys.stderr)

    if response.status_code != 200:
        print(response.text)
        return 1

    content_type = response.headers.get("Content-Type", "")
    if content_type.startswith("application/json"):
        payload = response.json()
        print(json.dumps(payload, indent=2))
        return 0

    if content_type.startswith("text/csv"):
        if export_id:
            manifest_response = requests.get(
                f"{args.base_url.rstrip('/')}/v1/exports/manifests/{export_id}",
                headers={"Authorization": f"Bearer {args.token}"},
                timeout=20,
            )
            if manifest_response.status_code == 200:
                print("manifest=" + manifest_response.text, file=sys.stderr)
        print(response.text)
        return 0

    if content_type.startswith("application/x-ndjson"):
        if export_id:
            manifest_response = requests.get(
                f"{args.base_url.rstrip('/')}/v1/exports/manifests/{export_id}",
                headers={"Authorization": f"Bearer {args.token}"},
                timeout=20,
            )
            if manifest_response.status_code == 200:
                print("manifest=" + manifest_response.text, file=sys.stderr)
        print(response.text)
        return 0

    print(response.text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
