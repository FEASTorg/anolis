"""Typed query plan model for deterministic Flux emission."""

from __future__ import annotations

from dataclasses import dataclass

from .models import SignalsQuery


@dataclass(frozen=True)
class ResolutionPlan:
    mode: str
    interval: str | None
    aggregation: str | None


@dataclass(frozen=True)
class QueryPlan:
    bucket: str
    start_iso: str
    end_iso: str
    runtime_names: tuple[str, ...]
    provider_ids: tuple[str, ...]
    device_ids: tuple[str, ...]
    signal_ids: tuple[str, ...]
    resolution: ResolutionPlan


def _sorted_unique(values: list[str]) -> tuple[str, ...]:
    return tuple(sorted(set(values)))


def build_query_plan(request: SignalsQuery, bucket: str) -> QueryPlan:
    return QueryPlan(
        bucket=bucket,
        start_iso=request.start.isoformat().replace("+00:00", "Z"),
        end_iso=request.end.isoformat().replace("+00:00", "Z"),
        runtime_names=_sorted_unique(request.runtime_names),
        provider_ids=_sorted_unique(request.provider_ids),
        device_ids=_sorted_unique(request.device_ids),
        signal_ids=_sorted_unique(request.signal_ids),
        resolution=ResolutionPlan(
            mode=request.resolution.mode,
            interval=request.resolution.interval,
            aggregation=request.resolution.aggregation,
        ),
    )
