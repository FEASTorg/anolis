"""Anolis test package."""

try:
    import pytest
except Exception:
    pass
else:
    # Ensure assertion rewriting is active for shared helper modules that
    # contain assertion logic used by scenario/integration tests.
    pytest.register_assert_rewrite("tests.scenarios.cases.base")
