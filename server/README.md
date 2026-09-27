# Server

FastAPI + SQLite backend and phone web UI.

```sh
python3 -m venv .venv && .venv/bin/pip install -e '.[dev]'
SHOPPING_LIST_CLASSIFIER=none .venv/bin/uvicorn shopping_list.main:app --reload
.venv/bin/pytest
```

See [docs/server.md](../docs/server.md) for configuration, auto-sorting and deployment, and
[docs/api.md](../docs/api.md) for the HTTP API.
