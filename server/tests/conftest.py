import pytest
from fastapi.testclient import TestClient

from shopping_list.classifier import Classification, ClassificationError, NullClassifier
from shopping_list.config import Settings
from shopping_list.main import create_app


class FakeClassifier:
    """Returns canned answers keyed by item name; raises for anything it wasn't told about."""

    def __init__(self):
        self.answers: dict[str, Classification] = {}
        self.calls: list[str] = []

    def classify(self, item_name, category_names):
        self.calls.append(item_name)
        if item_name not in self.answers:
            raise ClassificationError("no canned answer")
        return self.answers[item_name]


def _settings(tmp_path) -> Settings:
    return Settings(
        db_path=tmp_path / "test.db", classifier="none", claude_bin="claude", classify_timeout_seconds=1
    )


@pytest.fixture
def client(tmp_path):
    with TestClient(create_app(_settings(tmp_path), classifier=NullClassifier())) as c:
        yield c


@pytest.fixture
def classifier():
    return FakeClassifier()


@pytest.fixture
def classified_client(tmp_path, classifier):
    with TestClient(create_app(_settings(tmp_path), classifier=classifier)) as c:
        yield c
