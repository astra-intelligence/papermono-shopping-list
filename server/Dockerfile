FROM python:3.13-slim

WORKDIR /app
COPY pyproject.toml ./
COPY src ./src
RUN pip install --no-cache-dir .

# The image doesn't include the Claude Code CLI, so auto-classification is off by default here.
# Mount or install a `claude` binary and set SHOPPING_LIST_CLASSIFIER=claude to enable it.
ENV SHOPPING_LIST_DB=/data/shopping.db \
    SHOPPING_LIST_CLASSIFIER=none
VOLUME /data
EXPOSE 8000

RUN useradd --system --home /data shopping && mkdir -p /data && chown shopping /data
USER shopping
CMD ["uvicorn", "shopping_list.main:app", "--host", "0.0.0.0", "--port", "8000"]
