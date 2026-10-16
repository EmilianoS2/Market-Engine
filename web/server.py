"""Local website for the C++ order book. Run: python3 web/server.py"""

import argparse
import json
import os
import shutil
import subprocess
import tempfile
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlsplit


HERE = Path(__file__).resolve().parent
MAX_UINT64 = (1 << 64) - 1


class OrderBookEngine:
    def __init__(self, workdir=None):
        compiler = shutil.which("clang++") or shutil.which("g++")
        if compiler is None:
            raise RuntimeError("A C++ compiler (clang++ or g++) is required.")

        self.build_dir = tempfile.TemporaryDirectory(prefix="orderbook_web_")
        executable = Path(self.build_dir.name) / ("orderbook_backend.exe" if os.name == "nt" else "orderbook_backend")
        subprocess.run(
            [compiler, "-std=c++20", "-Wall", "-Wextra", "-pedantic", "-DORDERBOOK_LIBRARY_ONLY",
             "-I", str(HERE.parent / "include"), str(HERE / "orderbook_backend.cpp"),
             str(HERE.parent / "FINALorderbook.cpp"),
             *[str(HERE.parent / "src" / name) for name in
               ("quote_policy.cpp", "simulation_account.cpp", "engine_adapter.cpp", "market_maker.cpp")],
             "-o", str(executable)],
            check=True,
        )
        self.process = subprocess.Popen(
            [str(executable)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            text=True,
            bufsize=1,
            cwd=workdir,
        )
        self.lock = threading.Lock()

    def request(self, command):
        with self.lock:
            if self.process.poll() is not None:
                raise RuntimeError("The C++ order book process stopped.")
            self.process.stdin.write(command + "\n")
            self.process.stdin.flush()
            response = self.process.stdout.readline()
            if not response:
                raise RuntimeError("The C++ order book did not respond.")
            return json.loads(response)

    def close(self):
        if self.process.poll() is None:
            try:
                self.request("SHUTDOWN")
                self.process.stdin.close()
                self.process.wait(timeout=5)
            except (OSError, RuntimeError, subprocess.TimeoutExpired):
                self.process.terminate()
                self.process.wait(timeout=5)
        self.build_dir.cleanup()


def positive_uint(value, name):
    if not isinstance(value, str) or not value.isascii() or not value.isdecimal():
        raise ValueError(f"{name} must be a positive whole number.")
    number = int(value)
    if not 1 <= number <= MAX_UINT64:
        raise ValueError(f"{name} must be between 1 and {MAX_UINT64}.")
    return str(number)


def nonnegative_uint(value, name):
    if value == "0":
        return "0"
    return positive_uint(value, name)


def counterparty_owner(data):
    source = data.get("source", "manual")
    if source == "manual":
        return "TRADER_MANUAL"
    if source == "traffic" and data.get("side") in ("buy", "sell"):
        return "TRADER_SIM_" + data["side"].upper()
    raise ValueError("Invalid order source or side.")


class Handler(BaseHTTPRequestHandler):
    def send_json(self, data, status=200):
        body = json.dumps(data).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def send_file(self, filename, content_type):
        body = (HERE / filename).read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def read_json(self):
        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError as error:
            raise ValueError("Invalid request length.") from error
        if not 0 < length <= 8192:
            raise ValueError("Request body must be at most 8 KB.")
        try:
            data = json.loads(self.rfile.read(length))
        except (json.JSONDecodeError, UnicodeDecodeError) as error:
            raise ValueError("Invalid JSON body.") from error
        if not isinstance(data, dict):
            raise ValueError("Expected a JSON object.")
        return data

    def do_GET(self):
        path = urlsplit(self.path).path
        if path == "/":
            self.send_file("index.html", "text/html; charset=utf-8")
        elif path == "/styles.css":
            self.send_file("styles.css", "text/css; charset=utf-8")
        elif path == "/app.js":
            self.send_file("app.js", "text/javascript; charset=utf-8")
        elif path == "/api/state":
            try:
                self.send_json(self.server.engine.request("STATE"))
            except (OSError, RuntimeError, json.JSONDecodeError) as error:
                self.send_json({"ok": False, "error": str(error)}, 500)
        elif path == "/favicon.ico":
            self.send_response(204)
            self.end_headers()
        else:
            self.send_json({"ok": False, "error": "Not found."}, 404)

    def do_POST(self):
        path = urlsplit(self.path).path
        try:
            data = self.read_json()
            if path == "/api/orders":
                side = data.get("side")
                if side not in ("buy", "sell"):
                    raise ValueError("Side must be buy or sell.")
                order_type = data.get("type", "limit")
                if order_type not in ("limit", "market"):
                    raise ValueError("Order type must be limit or market.")
                if order_type == "market":
                    if "price" in data:
                        raise ValueError("Market orders do not take a limit price.")
                    price = "0"
                else:
                    price = positive_uint(data.get("price"), "Price")
                command = "ADD {} {} {} {} {} {}".format(
                    positive_uint(data.get("id"), "ID"),
                    side,
                    price,
                    positive_uint(data.get("quantity"), "Quantity"),
                    counterparty_owner(data),
                    order_type,
                )
            elif path == "/api/cancel":
                command = "CANCEL {} {}".format(positive_uint(data.get("id"), "ID"), counterparty_owner(data))
            elif path == "/api/reset":
                command = "RESET"
            elif path == "/api/market-maker":
                action = data.get("action")
                if action == "stop":
                    command = "MM STOP"
                elif action in ("start", "configure"):
                    settings = [("referencePrice", positive_uint), ("spreadBps", positive_uint),
                                ("quoteSize", positive_uint), ("refreshIntervalMs", positive_uint),
                                ("skewBpsPerShare", nonnegative_uint), ("minimumInventory", nonnegative_uint),
                                ("targetInventory", nonnegative_uint), ("maximumInventory", nonnegative_uint)]
                    values = [validate(data.get(name), name) for name, validate in settings]
                    command = "MM " + ("START" if action == "start" else "CONFIG") + " " + " ".join(values)
                else:
                    raise ValueError("Invalid market-maker action.")
            else:
                self.send_json({"ok": False, "error": "Not found."}, 404)
                return

            result = self.server.engine.request(command)
            self.send_json(result, 200 if result.get("ok") else 400)
        except ValueError as error:
            self.send_json({"ok": False, "error": str(error)}, 400)
        except (OSError, RuntimeError, json.JSONDecodeError) as error:
            self.send_json({"ok": False, "error": str(error)}, 500)


def main():
    parser = argparse.ArgumentParser(description="Run the local order book website")
    parser.add_argument("--port", type=int, default=8000)
    args = parser.parse_args()

    engine = OrderBookEngine()
    try:
        with ThreadingHTTPServer(("127.0.0.1", args.port), Handler) as server:
            server.engine = engine
            print(f"Order book website: http://127.0.0.1:{server.server_port}", flush=True)
            print("Press Ctrl+C to stop.", flush=True)
            server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        engine.close()


if __name__ == "__main__":
    main()
