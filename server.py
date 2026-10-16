#!/usr/bin/env python3
"""Giao diện demo C: chạy cục bộ hoặc trên Render, không cần pip."""

import json
import os
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import re
import subprocess
import threading

ROOT = Path(__file__).resolve().parent
PORT = int(os.environ.get("PORT", "8000"))
HOST = os.environ.get("APP_HOST", "127.0.0.1")
# Render tự cấp URL công khai. Không tin Origin/Host do client tự chọn.
ALLOWED_ORIGINS = {f"http://127.0.0.1:{PORT}", f"http://localhost:{PORT}"}
PUBLIC_ORIGIN = os.environ.get("RENDER_EXTERNAL_URL", "").rstrip("/")
if PUBLIC_ORIGIN:
    ALLOWED_ORIGINS.add(PUBLIC_ORIGIN)
RUN_LOCK = threading.Lock()
REPORT = re.compile(
    r"Report #(\d+) \| queue=([\d.]+) ms \| processing=([\d.]+) ms "
    r"\| total=([\d.]+) ms \| (OK|SLOW)"
)
DROP = re.compile(r"Report #(\d+) \| DROPPED .+")


def report_event(line):
    """Chỉ lấy độ trễ từ stdout của chương trình C."""
    match = REPORT.fullmatch(line)
    if match:
        report_id, wait, processing, total, status = match.groups()
        return dict(type="report", id=int(report_id), queue=float(wait),
                    processing=float(processing), total=float(total), status=status)
    match = DROP.fullmatch(line)
    if match:
        return dict(type="report", id=int(match[1]), status="DROPPED")
    return None


def summary_event(output):
    counts = re.search(r"created=(\d+) \| processed=(\d+) \| dropped=(\d+) \| SLOW=(\d+)", output)
    total = re.search(r"total: avg=([\d.]+) ms \| max=([\d.]+) ms", output)
    wait = re.search(r"queue_wait: avg=([\d.]+) ms \| max=([\d.]+) ms", output)
    queue = re.search(r"queue_max=(\d+) ", output)
    if not all((counts, total, wait, queue)):
        raise ValueError("Không đọc được tổng kết từ chương trình C.")
    created, processed, dropped, slow = map(int, counts.groups())
    if created != processed + dropped or "created = processed + dropped: PASS" not in output:
        raise ValueError("Bộ đếm report không khớp.")
    return dict(type="done", created=created, processed=processed, dropped=dropped,
                slow=slow, total_avg=float(total[1]), total_max=float(total[2]),
                wait_avg=float(wait[1]), wait_max=float(wait[2]), queue_max=int(queue[1]))


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/healthz":
            ready = os.access(ROOT / "demo", os.X_OK) and (ROOT / "ui.html").is_file()
            body = b"ok\n" if ready else b"demo unavailable\n"
            self.send_response(200 if ready else 503)
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        # Chỉ phục vụ một trang, không công khai file hay thư mục khác.
        if self.path != "/":
            self.send_error(404)
            return
        page = (ROOT / "ui.html").read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(page)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(page)

    def do_POST(self):
        # Chỉ nhận hai kịch bản cố định, không chạy lệnh tùy ý từ trình duyệt.
        modes = {"/run/normal": "normal", "/run/overload": "overload"}
        if self.path not in modes:
            self.send_error(400, "Unknown scenario")
            return
        origin = self.headers.get("Origin")
        if origin and origin not in ALLOWED_ORIGINS:
            self.send_error(403, "Origin not allowed")
            return
        if not RUN_LOCK.acquire(blocking=False):
            self.send_error(409, "A simulation is already running")
            return
        process = timer = None
        try:
            try:
                process = subprocess.Popen(
                    [str(ROOT / "demo"), modes[self.path]], cwd=ROOT,
                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                    text=True, encoding="utf-8", bufsize=1,
                )
            except OSError:
                self.send_error(503, "Cannot start demo. Run make first.")
                return
            # Giới hạn thời gian và luôn thu hồi tiến trình nếu client ngắt kết nối.
            timer = threading.Timer(20, process.kill)
            timer.start()
            self.send_response(200)
            self.send_header("Content-Type", "application/x-ndjson; charset=utf-8")
            self.send_header("Cache-Control", "no-store")
            self.send_header("X-Accel-Buffering", "no")
            self.end_headers()

            def emit(event):
                self.wfile.write((json.dumps(event, ensure_ascii=False) + "\n").encode("utf-8"))
                self.wfile.flush()

            output = []
            for line in process.stdout:
                output.append(line)
                emit(dict(type="log", line=line.rstrip()))
                event = report_event(line.strip())
                if event:
                    emit(event)
            code = process.wait()
            if code != 0:
                emit(dict(type="error", message=f"Demo dừng với mã {code} (giới hạn 20 giây)."))
            else:
                try:
                    emit(summary_event("".join(output)))
                except ValueError as error:
                    emit(dict(type="error", message=str(error)))
        except (BrokenPipeError, ConnectionResetError):
            pass
        finally:
            if timer:
                timer.cancel()
                timer.join()
            if process:
                if process.poll() is None:
                    process.kill()
                process.wait()
                process.stdout.close()
            RUN_LOCK.release()


if __name__ == "__main__":
    server = ThreadingHTTPServer((HOST, PORT), Handler)
    print(f"Giao diện sẵn sàng tại {HOST}, cổng {PORT}. Ctrl+C để dừng.", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
