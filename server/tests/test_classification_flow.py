"""How a newly added item gets its aisle, end to end through the API."""

from shopping_list.classifier import Classification


def _aisle(client, name):
    return client.post("/api/categories", json={"name": name}).json()


def test_classifier_picks_existing_aisle(classified_client, classifier):
    produce = _aisle(classified_client, "Produce")
    classifier.answers["Bananas"] = Classification("Produce", is_new=False)

    item = classified_client.post("/api/items", json={"name": "Bananas"}).json()

    assert item["category_id"] == produce["id"]


def test_classifier_can_create_a_new_aisle(classified_client, classifier):
    _aisle(classified_client, "Produce")
    classifier.answers["Ice Cream"] = Classification("Frozen", is_new=True)

    item = classified_client.post("/api/items", json={"name": "Ice Cream"}).json()

    assert item["category_name"] == "Frozen"
    names = [c["name"] for c in classified_client.get("/api/categories").json()]
    assert names == ["Produce", "Frozen", "Uncategorized"]


def test_new_aisle_differing_only_by_case_reuses_existing(classified_client, classifier):
    dairy = _aisle(classified_client, "Dairy")
    _aisle(classified_client, "Produce")
    # The CLI parser already folds case against the list it was given; this covers a classifier
    # that doesn't, so the UNIQUE constraint can never be hit with a near-duplicate.
    classifier.answers["Milk"] = Classification("DAIRY", is_new=True)

    item = classified_client.post("/api/items", json={"name": "Milk"}).json()

    assert item["category_id"] == dairy["id"]
    assert len(classified_client.get("/api/categories").json()) == 3


def test_classifier_not_called_without_any_aisles(classified_client, classifier):
    item = classified_client.post("/api/items", json={"name": "Bananas"}).json()

    assert item["category_name"] == "Uncategorized"
    assert classifier.calls == []


def test_classifier_runs_once_per_distinct_name(classified_client, classifier):
    _aisle(classified_client, "Produce")
    classifier.answers["Bananas"] = Classification("Produce", is_new=False)

    classified_client.post("/api/items", json={"name": "Bananas"})
    classified_client.post("/api/items", json={"name": "bananas"})

    assert classifier.calls == ["Bananas"]


def test_explicit_category_skips_classifier(classified_client, classifier):
    produce = _aisle(classified_client, "Produce")

    classified_client.post("/api/items", json={"name": "Bananas", "category_id": produce["id"]})

    assert classifier.calls == []


def test_manual_refile_teaches_the_catalog(classified_client, classifier):
    produce = _aisle(classified_client, "Produce")
    bakery = _aisle(classified_client, "Bakery")
    classifier.answers["Croissant"] = Classification("Produce", is_new=False)  # wrong

    item = classified_client.post("/api/items", json={"name": "Croissant"}).json()
    assert item["category_id"] == produce["id"]
    classified_client.patch(f"/api/items/{item['id']}", json={"category_id": bakery["id"]})

    again = classified_client.post("/api/items", json={"name": "Croissant"}).json()
    assert again["category_id"] == bakery["id"]
    assert classifier.calls == ["Croissant"]


def test_item_whose_aisle_was_deleted_is_reclassified(classified_client, classifier):
    produce = _aisle(classified_client, "Produce")
    fruit = _aisle(classified_client, "Fruit")
    classified_client.post("/api/items", json={"name": "Apples", "category_id": produce["id"]})
    classified_client.delete(f"/api/categories/{produce['id']}")
    classifier.answers["Apples"] = Classification("Fruit", is_new=False)

    item = classified_client.post("/api/items", json={"name": "Apples"}).json()

    assert item["category_id"] == fruit["id"]
