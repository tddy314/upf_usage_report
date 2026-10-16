FROM gcc:14-bookworm AS build
WORKDIR /app
COPY main.c Makefile ./
RUN make

FROM python:3.12-slim-bookworm
WORKDIR /app
COPY --from=build /app/demo ./demo
COPY --chmod=644 server.py ui.html ./
ENV APP_HOST=0.0.0.0 PORT=10000 PYTHONUNBUFFERED=1 PYTHONDONTWRITEBYTECODE=1
USER 10001:10001
EXPOSE 10000
CMD ["python3", "server.py"]
