"""Shared pytest configuration for integration/scenario suites."""

from __future__ import annotations

import socket
from pathlib import Path
from typing import Iterable

import pytest

from tests.support.runtime_fixture import RuntimeFixture


def _repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def _first_existing(paths: Iterable[Path]) -> Path | None:
    for path in paths:
        if path.exists() and path.is_file():
            return path.resolve()
    return None


def _runtime_candidates(root: Path) -> list[Path]:
    names = ["anolis-runtime.exe", "anolis-runtime"]
    build_root = root / "build"
    candidates: list[Path] = []

    preset_dirs = [
        "dev-release",
        "dev-debug",
        "dev-windows-release",
        "dev-windows-debug",
        "ci-linux-release",
        "ci-linux-release-strict",
        "ci-linux-arm64-release",
        "ci-linux-arm64-release-strict",
        "ci-windows-release",
        "ci-windows-release-strict",
        "ci-coverage",
        "ci-asan",
        "ci-ubsan",
        "ci-tsan",
        "ci-linux-compat",
    ]

    for name in names:
        # Preset-first build tree candidates (deterministic order).
        for preset in preset_dirs:
            candidates.extend(
                [
                    build_root / preset / "core" / name,
                    build_root / preset / "core" / "Release" / name,
                    build_root / preset / "core" / "Debug" / name,
                    build_root / preset / "core" / "RelWithDebInfo" / name,
                    build_root / preset / "core" / "MinSizeRel" / name,
                ]
            )

        # Backstop legacy/non-preset paths (still deterministic).
        candidates.extend(
            [
                build_root / "core" / "Release" / name,
                build_root / "core" / "Debug" / name,
                build_root / "core" / "RelWithDebInfo" / name,
                build_root / "core" / "MinSizeRel" / name,
                build_root / "core" / name,
            ]
        )
    return candidates


def _provider_candidates(root: Path) -> list[Path]:
    names = ["anolis-provider-sim.exe", "anolis-provider-sim"]
    build_roots = [
        root.parent / "anolis-provider-sim" / "build",
        root / "anolis-provider-sim" / "build",
    ]
    candidates: list[Path] = []

    preset_dirs = [
        "dev-release",
        "dev-debug",
        "dev-windows-release",
        "dev-windows-debug",
        "ci-linux-release",
        "ci-windows-release",
        "ci-linux-fluxgraph-on",
    ]

    for build_root in build_roots:
        for name in names:
            for preset in preset_dirs:
                candidates.extend(
                    [
                        build_root / preset / name,
                        build_root / preset / "Release" / name,
                        build_root / preset / "Debug" / name,
                        build_root / preset / "RelWithDebInfo" / name,
                        build_root / preset / "MinSizeRel" / name,
                    ]
                )

            candidates.extend(
                [
                    build_root / "Release" / name,
                    build_root / "Debug" / name,
                    build_root / "RelWithDebInfo" / name,
                    build_root / "MinSizeRel" / name,
                    build_root / name,
                ]
            )
    return candidates


def _resolve_runtime(requested: str | None) -> Path:
    if requested:
        path = Path(requested)
        if path.exists() and path.is_file():
            return path.resolve()
        raise RuntimeError(f"--runtime path does not exist: {path}")

    root = _repo_root()
    found = _first_existing(_runtime_candidates(root))
    if found is None:
        raise RuntimeError("Could not resolve anolis-runtime executable. Pass --runtime=/path/to/anolis-runtime.")
    return found


def _resolve_provider(requested: str | None) -> Path:
    if requested:
        path = Path(requested)
        if path.exists() and path.is_file():
            return path.resolve()
        raise RuntimeError(f"--provider path does not exist: {path}")

    root = _repo_root()
    found = _first_existing(_provider_candidates(root))
    if found is None:
        raise RuntimeError(
            "Could not resolve anolis-provider-sim executable. Pass --provider=/path/to/anolis-provider-sim."
        )
    return found


def _pick_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        return int(sock.getsockname()[1])


def pytest_addoption(parser: pytest.Parser) -> None:
    parser.addoption("--runtime", action="store", default=None, help="Path to anolis-runtime executable")
    parser.addoption(
        "--provider",
        action="store",
        default=None,
        help="Path to anolis-provider-sim executable",
    )
    parser.addoption(
        "--integration-timeout",
        action="store",
        default="120",
        help="Default per-suite integration timeout in seconds",
    )


@pytest.fixture(scope="session")
def runtime_exe(request: pytest.FixtureRequest) -> Path:
    return _resolve_runtime(request.config.getoption("--runtime"))


@pytest.fixture(scope="session")
def provider_exe(request: pytest.FixtureRequest) -> Path:
    return _resolve_provider(request.config.getoption("--provider"))


@pytest.fixture(scope="session")
def integration_timeout(request: pytest.FixtureRequest) -> float:
    value = request.config.getoption("--integration-timeout")
    try:
        return float(value)
    except ValueError as exc:
        raise RuntimeError(f"Invalid --integration-timeout value: {value}") from exc


@pytest.fixture
def unique_port() -> int:
    return _pick_free_port()


@pytest.fixture
def runtime_factory(runtime_exe: Path, provider_exe: Path):
    started: list[RuntimeFixture] = []

    def _start(*, config_dict: dict | None = None, port: int = 8080, verbose: bool = False) -> RuntimeFixture:
        fixture = RuntimeFixture(
            runtime_exe,
            provider_exe,
            http_port=port,
            verbose=verbose,
            config_dict=config_dict,
        )
        if not fixture.start():
            capture = fixture.get_output_capture()
            output = capture.get_recent_output(100) if capture else "(no output capture)"
            fixture.cleanup()
            raise AssertionError(f"Failed to start runtime fixture on port {port}\n{output}")
        started.append(fixture)
        return fixture

    yield _start

    for fixture in reversed(started):
        fixture.cleanup()
