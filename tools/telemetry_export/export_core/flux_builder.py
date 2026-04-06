"""Flux query construction utilities."""

from __future__ import annotations

from .models import AUX_FIELDS_LAST_ONLY, NON_NUMERIC_VALUE_FIELDS, NUMERIC_VALUE_FIELDS, SignalsQuery
from .query_plan import QueryPlan, build_query_plan


def flux_quote(value: str) -> str:
    return value.replace("\\", "\\\\").replace('"', '\\"')


def build_selector_filter(field_name: str, values: tuple[str, ...]) -> str | None:
    if not values:
        return None
    clauses = [f'r.{field_name} == "{flux_quote(v)}"' for v in values]
    return " or ".join(clauses)


def build_field_filter(field_names: tuple[str, ...]) -> str:
    clauses = [f'r._field == "{flux_quote(field)}"' for field in field_names]
    return " or ".join(clauses)


def build_base_flux_lines_from_plan(plan: QueryPlan) -> list[str]:
    lines = [
        f'from(bucket:"{flux_quote(plan.bucket)}")',
        f'  |> range(start: time(v: "{plan.start_iso}"), stop: time(v: "{plan.end_iso}"))',
        '  |> filter(fn:(r) => r._measurement == "anolis_signal")',
    ]

    for field_name, values in (
        ("runtime_name", plan.runtime_names),
        ("provider_id", plan.provider_ids),
        ("device_id", plan.device_ids),
        ("signal_id", plan.signal_ids),
    ):
        expr = build_selector_filter(field_name, values)
        if expr:
            lines.append(f"  |> filter(fn:(r) => {expr})")

    return lines


def build_base_flux_pipeline_from_plan(plan: QueryPlan) -> str:
    return "\n".join(build_base_flux_lines_from_plan(plan))


def build_downsample_query_from_plan(plan: QueryPlan) -> str:
    base = build_base_flux_pipeline_from_plan(plan)
    numeric_filter = build_field_filter(NUMERIC_VALUE_FIELDS)
    non_numeric_filter = build_field_filter(NON_NUMERIC_VALUE_FIELDS + AUX_FIELDS_LAST_ONLY)
    numeric_agg = plan.resolution.aggregation

    return "\n".join(
        [
            "numeric = (",
            base,
            f"  |> filter(fn:(r) => {numeric_filter})",
            f"  |> aggregateWindow(every: {plan.resolution.interval}, fn: {numeric_agg}, createEmpty: false)",
            ")",
            "",
            "non_numeric = (",
            base,
            f"  |> filter(fn:(r) => {non_numeric_filter})",
            f"  |> aggregateWindow(every: {plan.resolution.interval}, fn: last, createEmpty: false)",
            ")",
            "",
            "union(tables:[numeric, non_numeric])",
            (
                '  |> pivot(rowKey:["_time","runtime_name","provider_id","device_id","signal_id"], '
                'columnKey:["_field"], valueColumn:"_value")'
            ),
            (
                '  |> keep(columns:["_time","runtime_name","provider_id","device_id","signal_id","quality",'
                '"value_double","value_int","value_uint","value_bool","value_string"])'
            ),
            '  |> sort(columns:["_time","runtime_name","provider_id","device_id","signal_id"])',
        ]
    )


def build_raw_or_project_query_from_plan(plan: QueryPlan) -> str:
    lines = build_base_flux_lines_from_plan(plan)

    lines.extend(
        [
            (
                '  |> pivot(rowKey:["_time","runtime_name","provider_id","device_id","signal_id"], '
                'columnKey:["_field"], valueColumn:"_value")'
            ),
            (
                '  |> keep(columns:["_time","runtime_name","provider_id","device_id","signal_id","quality",'
                '"value_double","value_int","value_uint","value_bool","value_string"])'
            ),
            '  |> sort(columns:["_time","runtime_name","provider_id","device_id","signal_id"])',
        ]
    )

    return "\n".join(lines)


def emit_flux_from_plan(plan: QueryPlan) -> str:
    if plan.resolution.mode == "downsampled":
        return build_downsample_query_from_plan(plan)
    return build_raw_or_project_query_from_plan(plan)


def build_flux_query(request: SignalsQuery, bucket: str) -> str:
    plan = build_query_plan(request, bucket)
    return emit_flux_from_plan(plan)
