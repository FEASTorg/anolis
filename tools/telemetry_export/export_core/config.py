"""Configuration parsing for telemetry export service."""

from __future__ import annotations

import os
from pathlib import Path
from typing import Any

import yaml

from .models import AppConfig, AuthorizationConfig, InfluxConfig, LimitConfig, ServerConfig


def parse_bool(value: Any, field_name: str) -> bool:
    if isinstance(value, bool):
        return value
    raise RuntimeError(f"Invalid config at {field_name}: must be boolean")


def parse_int(value: Any, field_name: str, *, min_value: int | None = None, max_value: int | None = None) -> int:
    try:
        parsed = int(value)
    except (TypeError, ValueError) as exc:
        raise RuntimeError(f"Invalid config at {field_name}: must be an integer") from exc

    if min_value is not None and parsed < min_value:
        raise RuntimeError(f"Invalid config at {field_name}: must be >= {min_value}")
    if max_value is not None and parsed > max_value:
        raise RuntimeError(f"Invalid config at {field_name}: must be <= {max_value}")
    return parsed


def parse_required_string(value: Any, field_name: str) -> str:
    parsed = str(value).strip()
    if not parsed:
        raise RuntimeError(f"Invalid config at {field_name}: must be a non-empty string")
    return parsed


def parse_allowed_list(value: Any, field_name: str) -> tuple[str, ...]:
    if value is None:
        return ()
    if not isinstance(value, list):
        raise RuntimeError(f"Invalid config at {field_name}: must be an array of strings")

    parsed: list[str] = []
    seen: set[str] = set()
    for item in value:
        if not isinstance(item, str) or not item.strip():
            raise RuntimeError(f"Invalid config at {field_name}: entries must be non-empty strings")
        normalized = item.strip()
        if normalized not in seen:
            parsed.append(normalized)
            seen.add(normalized)

    return tuple(parsed)


def resolve_secret(*, env_name: str, config_value: str, field_name: str) -> str:
    """Resolve secret using env override then config fallback."""
    env_value = os.getenv(env_name, "").strip()
    if env_value:
        return env_value

    if config_value:
        return config_value

    raise RuntimeError(f"Invalid config at {field_name}: must be set (or provide env override {env_name})")


def load_config(path: Path) -> AppConfig:
    if not path.exists() or not path.is_file():
        raise RuntimeError(f"Config file not found: {path}")

    raw = yaml.safe_load(path.read_text(encoding="utf-8"))
    if not isinstance(raw, dict):
        raise RuntimeError("Invalid config: expected top-level mapping")

    server_raw = raw.get("server")
    influx_raw = raw.get("influxdb")
    limits_raw = raw.get("limits")
    authorization_raw = raw.get("authorization", {})

    if not isinstance(server_raw, dict):
        raise RuntimeError("Invalid config at server: section is required")
    if not isinstance(influx_raw, dict):
        raise RuntimeError("Invalid config at influxdb: section is required")
    if not isinstance(limits_raw, dict):
        raise RuntimeError("Invalid config at limits: section is required")
    if authorization_raw is None:
        authorization_raw = {}
    if not isinstance(authorization_raw, dict):
        raise RuntimeError("Invalid config at authorization: section must be an object when set")

    server = ServerConfig(
        host=parse_required_string(server_raw.get("host", "127.0.0.1"), "server.host"),
        port=parse_int(server_raw.get("port", 8091), "server.port", min_value=1, max_value=65535),
        auth_token=resolve_secret(
            env_name="ANOLIS_EXPORT_AUTH_TOKEN",
            config_value=str(server_raw.get("auth_token", "")).strip(),
            field_name="server.auth_token",
        ),
    )

    influx = InfluxConfig(
        url=parse_required_string(influx_raw.get("url", ""), "influxdb.url").rstrip("/"),
        org=parse_required_string(influx_raw.get("org", ""), "influxdb.org"),
        bucket=parse_required_string(influx_raw.get("bucket", ""), "influxdb.bucket"),
        token=resolve_secret(
            env_name="ANOLIS_EXPORT_INFLUX_TOKEN",
            config_value=str(influx_raw.get("token", "")).strip(),
            field_name="influxdb.token",
        ),
    )

    limits = LimitConfig(
        max_span_seconds=parse_int(limits_raw.get("max_span_seconds", 86400), "limits.max_span_seconds", min_value=1),
        max_rows=parse_int(limits_raw.get("max_rows", 50000), "limits.max_rows", min_value=1),
        max_response_bytes=parse_int(
            limits_raw.get("max_response_bytes", 10_000_000),
            "limits.max_response_bytes",
            min_value=1,
        ),
        max_selector_items=parse_int(
            limits_raw.get("max_selector_items", 128),
            "limits.max_selector_items",
            min_value=1,
        ),
        request_timeout_seconds=parse_int(
            limits_raw.get("request_timeout_seconds", 15),
            "limits.request_timeout_seconds",
            min_value=1,
        ),
        max_request_bytes=parse_int(
            limits_raw.get("max_request_bytes", 200_000),
            "limits.max_request_bytes",
            min_value=1,
        ),
    )

    authorization = AuthorizationConfig(
        enforce_selector_scope=parse_bool(
            authorization_raw.get("enforce_selector_scope", False),
            "authorization.enforce_selector_scope",
        ),
        allowed_runtime_names=parse_allowed_list(
            authorization_raw.get("allowed_runtime_names"),
            "authorization.allowed_runtime_names",
        ),
        allowed_provider_ids=parse_allowed_list(
            authorization_raw.get("allowed_provider_ids"),
            "authorization.allowed_provider_ids",
        ),
        allowed_device_ids=parse_allowed_list(
            authorization_raw.get("allowed_device_ids"),
            "authorization.allowed_device_ids",
        ),
        allowed_signal_ids=parse_allowed_list(
            authorization_raw.get("allowed_signal_ids"),
            "authorization.allowed_signal_ids",
        ),
    )

    if limits.max_response_bytes < limits.max_request_bytes:
        raise RuntimeError(
            "Invalid config at limits.max_response_bytes: must be >= limits.max_request_bytes"
        )

    if authorization.enforce_selector_scope and not any(
        (
            authorization.allowed_runtime_names,
            authorization.allowed_provider_ids,
            authorization.allowed_device_ids,
            authorization.allowed_signal_ids,
        )
    ):
        raise RuntimeError(
            "Invalid config at authorization: enforce_selector_scope=true requires "
            "at least one non-empty allowed_* list"
        )

    return AppConfig(server=server, influx=influx, limits=limits, authorization=authorization)
