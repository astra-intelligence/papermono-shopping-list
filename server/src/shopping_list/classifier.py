"""Sorting brand-new item names into aisles.

Only consulted on a genuine catalog miss (see `repository.resolve_category_id`): every item name
seen before is resolved from the `catalog` table, so a classifier runs at most once per distinct
item ever added.
"""

import re
import subprocess
from dataclasses import dataclass
from typing import Protocol

from .config import Settings

# Keep in step with CategoryCreate.name's max_length (models.py): a proposed new aisle is inserted
# directly, so it has to fit the same limit a human-created one does.
MAX_CATEGORY_NAME_LEN = 64

_NEW_PREFIX_RE = re.compile(r"^\s*NEW\s*:\s*(.+?)\s*$", re.IGNORECASE)


class ClassificationError(RuntimeError):
    """The classifier couldn't produce an answer; callers fall back to Uncategorized."""


@dataclass(frozen=True)
class Classification:
    category_name: str
    is_new: bool  # True: the classifier is proposing an aisle that doesn't exist yet


class Classifier(Protocol):
    def classify(self, item_name: str, category_names: list[str]) -> Classification: ...


class NullClassifier:
    """Never classifies. Used when no classifier is configured, and in tests."""

    def classify(self, item_name: str, category_names: list[str]) -> Classification:
        raise ClassificationError("classification disabled")


class ClaudeCliClassifier:
    """Asks Claude via the Claude Code CLI (`claude -p`).

    The CLI handles authentication itself, so this works with either an `ANTHROPIC_API_KEY` or a
    long-lived `CLAUDE_CODE_OAUTH_TOKEN` (from `claude setup-token`) in the service's environment.
    """

    def __init__(self, claude_bin: str = "claude", timeout_seconds: float = 30):
        self._claude_bin = claude_bin
        self._timeout_seconds = timeout_seconds

    def classify(self, item_name: str, category_names: list[str]) -> Classification:
        try:
            result = subprocess.run(
                [self._claude_bin, "-p", build_prompt(item_name, category_names)],
                capture_output=True,
                text=True,
                timeout=self._timeout_seconds,
            )
        except (OSError, subprocess.TimeoutExpired) as exc:
            raise ClassificationError(str(exc)) from exc

        if result.returncode != 0:
            raise ClassificationError(result.stderr.strip() or "claude CLI failed")
        return parse_reply(result.stdout, category_names)


def build_prompt(item_name: str, category_names: list[str]) -> str:
    return (
        "You are sorting a grocery shopping list item into the store section "
        "it belongs to, so items can be grouped by where they are in the shop.\n\n"
        f"Existing sections, in shop-walking order: {', '.join(category_names)}\n\n"
        f'Item: "{item_name}"\n\n'
        "Reply with ONLY the exact existing section name if one clearly fits, "
        'or "NEW: <short section name>" if none fit well. No other text, no '
        "punctuation beyond what's in the section name."
    )


def parse_reply(reply: str, category_names: list[str]) -> Classification:
    reply = reply.strip()
    if not reply:
        raise ClassificationError("empty reply from classifier")

    new_match = _NEW_PREFIX_RE.match(reply)
    proposed = new_match.group(1) if new_match else reply

    # A "new" aisle that differs from an existing one only by case is really the existing one.
    for name in category_names:
        if name.lower() == proposed.lower():
            return Classification(name, is_new=False)

    # Anything else - an explicit NEW: or a reply that didn't follow the format - is treated as a
    # proposed new aisle rather than silently dropped.
    proposed = proposed[:MAX_CATEGORY_NAME_LEN].strip()
    if not proposed:
        raise ClassificationError("classifier proposed an empty category name")
    return Classification(proposed, is_new=True)


def build_classifier(settings: Settings) -> Classifier:
    if settings.classifier == "none":
        return NullClassifier()
    if settings.classifier == "claude":
        return ClaudeCliClassifier(settings.claude_bin, settings.classify_timeout_seconds)
    raise ValueError(
        f"Unknown SHOPPING_LIST_CLASSIFIER {settings.classifier!r} (expected 'claude' or 'none')"
    )
