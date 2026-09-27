import hashlib
from contextlib import asynccontextmanager
from pathlib import Path

from fastapi import FastAPI, Request
from fastapi.responses import HTMLResponse
from fastapi.staticfiles import StaticFiles
from fastapi.templating import Jinja2Templates

from .classifier import Classifier, build_classifier
from .config import Settings, load_settings
from .db import init_db
from .routers import catalog, categories, items, sync

PACKAGE_DIR = Path(__file__).resolve().parent
STATIC_DIR = PACKAGE_DIR / "static"


def _static_version() -> str:
    """Cache-busting query string for the assets index.html links to.

    Derived from their content, so it changes exactly when a deploy changes them. Cache-Control
    alone isn't enough to stop a phone serving a stale app.js: the asset also has to live at a URL
    the browser hasn't already cached.
    """
    h = hashlib.sha256()
    for name in ("app.js", "style.css"):
        h.update((STATIC_DIR / name).read_bytes())
    return h.hexdigest()[:10]


def create_app(settings: Settings | None = None, classifier: Classifier | None = None) -> FastAPI:
    settings = settings or load_settings()

    @asynccontextmanager
    async def lifespan(app: FastAPI):
        init_db(settings.db_path)
        yield

    app = FastAPI(title="PaperMono Shopping List", lifespan=lifespan)
    app.state.settings = settings
    app.state.classifier = classifier or build_classifier(settings)

    app.mount("/static", StaticFiles(directory=STATIC_DIR), name="static")
    # PaperMono web flasher (esptool-js over WebSerial), folded in here so it's served from the
    # same authenticated origin instead of running as its own separate static site.
    app.mount("/flash", StaticFiles(directory=PACKAGE_DIR / "flasher", html=True), name="flasher")
    templates = Jinja2Templates(directory=PACKAGE_DIR / "templates")
    static_version = _static_version()

    @app.middleware("http")
    async def no_heuristic_caching(request: Request, call_next):
        # StaticFiles sends ETag/Last-Modified but no Cache-Control, so browsers fall back to
        # heuristic freshness (RFC 9111 4.2.2) and may skip the request entirely. `no-cache` forces
        # a revalidation, which is still a cheap 304 when nothing changed.
        response = await call_next(request)
        response.headers["Cache-Control"] = "no-cache"
        return response

    app.include_router(categories.router)
    app.include_router(items.router)
    app.include_router(catalog.router)
    app.include_router(sync.router)

    @app.get("/", response_class=HTMLResponse, include_in_schema=False)
    def index(request: Request):
        return templates.TemplateResponse(request, "index.html", {"static_version": static_version})

    @app.get("/api/health", tags=["health"])
    def health():
        return {"status": "ok"}

    return app


app = create_app()
