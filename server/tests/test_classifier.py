import subprocess

import pytest

from shopping_list.classifier import (
    MAX_CATEGORY_NAME_LEN,
    ClassificationError,
    ClaudeCliClassifier,
    NullClassifier,
    build_classifier,
    parse_reply,
)
from shopping_list.config import Settings

AISLES = ["Produce", "Dairy"]


def _fake_run(stdout, returncode=0):
    def run(*args, **kwargs):
        return subprocess.CompletedProcess(args, returncode, stdout=stdout, stderr="")

    return run


def test_null_classifier_always_raises():
    with pytest.raises(ClassificationError):
        NullClassifier().classify("Bananas", AISLES)


def test_exact_existing_category_match():
    result = parse_reply("Produce\n", AISLES)
    assert (result.category_name, result.is_new) == ("Produce", False)


def test_case_insensitive_existing_category_match():
    result = parse_reply("produce", AISLES)
    assert (result.category_name, result.is_new) == ("Produce", False)


def test_new_category_prefix():
    result = parse_reply("NEW: Frozen", AISLES)
    assert (result.category_name, result.is_new) == ("Frozen", True)


def test_new_prefix_naming_an_existing_category_is_not_new():
    result = parse_reply("NEW: dairy", AISLES)
    assert (result.category_name, result.is_new) == ("Dairy", False)


def test_off_format_reply_becomes_a_proposed_category():
    result = parse_reply("Frozen Foods", AISLES)
    assert (result.category_name, result.is_new) == ("Frozen Foods", True)


def test_proposed_category_name_is_truncated():
    result = parse_reply("NEW: " + "x" * 200, AISLES)
    assert len(result.category_name) == MAX_CATEGORY_NAME_LEN


def test_empty_reply_raises():
    with pytest.raises(ClassificationError):
        parse_reply("  \n", AISLES)


def test_cli_classifier_parses_stdout(monkeypatch):
    monkeypatch.setattr(subprocess, "run", _fake_run("Produce\n"))
    assert ClaudeCliClassifier().classify("Bananas", AISLES).category_name == "Produce"


def test_cli_nonzero_exit_raises(monkeypatch):
    monkeypatch.setattr(subprocess, "run", _fake_run("", returncode=1))
    with pytest.raises(ClassificationError):
        ClaudeCliClassifier().classify("Bananas", AISLES)


def test_cli_missing_binary_raises(monkeypatch):
    def raise_missing(*args, **kwargs):
        raise FileNotFoundError("claude not found")

    monkeypatch.setattr(subprocess, "run", raise_missing)
    with pytest.raises(ClassificationError):
        ClaudeCliClassifier().classify("Bananas", AISLES)


def test_cli_timeout_raises(monkeypatch):
    def time_out(*args, **kwargs):
        raise subprocess.TimeoutExpired(cmd="claude", timeout=1)

    monkeypatch.setattr(subprocess, "run", time_out)
    with pytest.raises(ClassificationError):
        ClaudeCliClassifier().classify("Bananas", AISLES)


@pytest.mark.parametrize("name, expected", [("none", NullClassifier), ("claude", ClaudeCliClassifier)])
def test_build_classifier(tmp_path, name, expected):
    settings = Settings(
        db_path=tmp_path / "db", classifier=name, claude_bin="claude", classify_timeout_seconds=1
    )
    assert isinstance(build_classifier(settings), expected)


def test_build_classifier_rejects_unknown(tmp_path):
    settings = Settings(
        db_path=tmp_path / "db", classifier="gpt", claude_bin="claude", classify_timeout_seconds=1
    )
    with pytest.raises(ValueError):
        build_classifier(settings)
