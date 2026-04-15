"""Contract tests for System Composer control API behavior."""

from __future__ import annotations

import json
import os
import pathlib
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from typing import Any

import pytest

_SC_DIR = pathlib.Path(__file__).resolve().parent.parent.parent
_SERVER_SCRIPT = _SC_DIR / "backend" / "server.py"


def _pick_free_port() -> int:
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
            sock.bind(("127.0.0.1", 0))
            return int(sock.getsockname()[1])
    except PermissionError as exc:
        pytest.skip(f"Socket creation is not permitted in this environment: {exc}")


def _http_json(
    base_url: str,
    path: str,
    *,
    method: str = "GET",
    body: dict[str, Any] | None = None,
    timeout_s: float = 5.0,
) -> tuple[int, dict[str, Any]]:
    payload = None
    headers: dict[str, str] = {}
    if body is not None:
        payload = json.dumps(body).encode("utf-8")
        headers["Content-Type"] = "application/json"
    request = urllib.request.Request(
        f"{base_url}{path}",
        data=payload,
        headers=headers,
        method=method,
    )
    try:
        with urllib.request.urlopen(request, timeout=timeout_s) as response:
            raw = response.read().decode("utf-8")
            data: dict[str, Any] = json.loads(raw) if raw else {}
            return int(response.status), data
    except urllib.error.HTTPError as exc:
        raw = exc.read().decode("utf-8")
        data = json.loads(raw) if raw else {}
        return int(exc.code), data


def _wait_for_ready(base_url: str, proc: subprocess.Popen[str], timeout_s: float = 10.0) -> None:
    deadline = time.time() + timeout_s
    last_error = "service did not respond"
    while time.time() < deadline:
        if proc.poll() is not None:
            stderr = proc.stderr.read() if proc.stderr is not None else ""
            raise AssertionError(
                f"System Composer exited before readiness check (code={proc.returncode}):\n{stderr}"
            )
        try:
            status, _ = _http_json(base_url, "/api/status", timeout_s=0.5)
            if status == 200:
                return
            last_error = f"unexpected status {status}"
        except Exception as exc:  # noqa: BLE001 - test helper surface
            last_error = str(exc)
        time.sleep(0.1)
    raise AssertionError(f"System Composer readiness timeout: {last_error}")


@pytest.fixture
def composer_server(tmp_path: pathlib.Path) -> dict[str, Any]:
    port = _pick_free_port()
    env = os.environ.copy()
    env["ANOLIS_COMPOSER_HOST"] = "127.0.0.1"
    env["ANOLIS_COMPOSER_PORT"] = str(port)
    env["ANOLIS_OPERATOR_UI_BASE"] = "http://localhost:3900"
    env["ANOLIS_COMPOSER_OPEN_BROWSER"] = "0"

    proc = subprocess.Popen(
        [sys.executable, str(_SERVER_SCRIPT)],
        cwd=str(tmp_path),
        env=env,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        text=True,
    )
    base_url = f"http://127.0.0.1:{port}"
    _wait_for_ready(base_url, proc)

    try:
        yield {"base_url": base_url, "port": port, "cwd": str(tmp_path)}
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=5)


def test_status_contract_exposes_composer_metadata(composer_server: dict[str, Any]) -> None:
    status, payload = _http_json(composer_server["base_url"], "/api/status")
    assert status == 200
    assert payload.get("version") == 1
    assert payload.get("running") is False
    assert payload.get("active_project") is None
    assert payload.get("pid") is None

    composer = payload.get("composer")
    assert isinstance(composer, dict)
    assert composer.get("host") == "127.0.0.1"
    assert composer.get("port") == composer_server["port"]
    assert composer.get("operator_ui_base") == "http://localhost:3900"


def test_control_contract_save_validation_failure_shape(composer_server: dict[str, Any]) -> None:
    base_url = composer_server["base_url"]
    project = "contract_shape_test"

    created_status, created = _http_json(
        base_url,
        "/api/projects",
        method="POST",
        body={"name": project, "template": "sim-quickstart"},
    )
    assert created_status == 201, created

    try:
        bad_status, bad = _http_json(
            base_url,
            f"/api/projects/{urllib.parse.quote(project)}",
            method="PUT",
            body={"schema_version": 1},
        )
        assert bad_status == 400
        assert bad.get("error") == "Project validation failed"
        assert bad.get("code") == "validation_failed"

        errors = bad.get("errors")
        assert isinstance(errors, list)
        assert errors
        first = errors[0]
        assert isinstance(first, dict)
        assert isinstance(first.get("source"), str)
        assert isinstance(first.get("code"), str)
        assert isinstance(first.get("path"), str)
        assert isinstance(first.get("message"), str)
    finally:
        _http_json(base_url, f"/api/projects/{urllib.parse.quote(project)}", method="DELETE")


def test_control_contract_missing_project_errors_are_stable(composer_server: dict[str, Any]) -> None:
    base_url = composer_server["base_url"]
    missing = "ghost_project_contract"

    for method, path in [
        ("POST", f"/api/projects/{missing}/preflight"),
        ("POST", f"/api/projects/{missing}/launch"),
        ("POST", f"/api/projects/{missing}/stop"),
        ("POST", f"/api/projects/{missing}/restart"),
        ("GET", f"/api/projects/{missing}/logs"),
    ]:
        status, payload = _http_json(base_url, path, method=method, body={} if method == "POST" else None)
        assert status == 404, (method, path, payload)
        assert payload.get("error") == f"Project '{missing}' not found"


def test_control_contract_invalid_project_name_is_400(composer_server: dict[str, Any]) -> None:
    base_url = composer_server["base_url"]
    invalid_name = urllib.parse.quote("bad$name", safe="")
    status, payload = _http_json(
        base_url,
        f"/api/projects/{invalid_name}/preflight",
        method="POST",
        body={},
    )
    assert status == 400
    assert "Project name must be" in str(payload.get("error"))
