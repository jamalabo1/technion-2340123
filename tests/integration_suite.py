#!/usr/bin/env python3
"""
Black-box integration tests for the HW3 web server.

The suite starts the server, drives TCP/UDP clients, parses the Stat-* headers,
and checks synchronization behavior that is hard to see from unit tests:

  * bounded TCP queue wakeup after becoming full
  * UDP still being collected while TCP is full
  * per-thread counters via HTTP thread ids and UDP pings
  * log entry contents and debug-sleep timing
  * strict writer priority over later POST readers
  * mixed-load deadlock and basic performance probes

To add a test, define a function that accepts TestContext and decorate it:

    @test("my_new_case")
    def my_new_case(ctx):
        ...

Run from the repository root:

    python3 tests/integration_suite.py

Useful options:

    --use-running          test an already-running server instead of spawning one
    --only PATTERN         run only tests whose names contain PATTERN
    --category NAME        run one or more categories, e.g. http,udp or log
    --threads N           server worker count when the suite launches the server
    --queue-size N        queue size when the suite launches the server
    --debug-sleep SEC     log debug sleep; several tests benefit from > 0

Test cases live under tests/integration_cases/ and are grouped by category:
http, udp, log, queue, and load.
"""

import argparse
import contextlib
import importlib
import os
import re
import shutil
import signal
import socket
import subprocess
import sys
import threading
import time
from pathlib import Path
from typing import Callable, Dict, Iterable, List, Optional, Tuple


REPO = Path(__file__).resolve().parents[1]
TESTS: List[Tuple[str, str, Callable[["TestContext"], None]]] = []
CATEGORY_MODULES = (
    "integration_cases.basic_correctness",
    "integration_cases.udp_correctness",
    "integration_cases.log_sync",
    "integration_cases.queue_sync",
    "integration_cases.load",
)


def test(name: str, category: str = "general") -> Callable[[Callable[["TestContext"], None]], Callable[["TestContext"], None]]:
    def register(fn: Callable[["TestContext"], None]) -> Callable[["TestContext"], None]:
        TESTS.append((category, name, fn))
        return fn

    return register


def load_category_modules() -> None:
    # When this file is executed as a script, category modules still import it as
    # integration_suite. Publish the current module under that name to avoid a
    # second copy of the registry.
    sys.modules.setdefault("integration_suite", sys.modules[__name__])
    for module_name in CATEGORY_MODULES:
        importlib.import_module(module_name)


class TestFailure(AssertionError):
    pass


class SkipTest(Exception):
    pass


def fail(message: str) -> None:
    raise TestFailure(message)


def check(condition: bool, message: str) -> None:
    if not condition:
        fail(message)


def wait_until(deadline_s: float, pred: Callable[[], bool], interval_s: float = 0.03) -> bool:
    end = time.monotonic() + deadline_s
    while time.monotonic() < end:
        if pred():
            return True
        time.sleep(interval_s)
    return pred()


def parse_stat_lines(text: str) -> Dict[str, str]:
    stats: Dict[str, str] = {}
    for raw in text.splitlines():
        if "::" not in raw:
            continue
        key, value = raw.split("::", 1)
        stats[key.strip()] = value.strip()
    return stats


def stat_int(stats: Dict[str, str], name: str, default: int = -1) -> int:
    try:
        return int(stats.get(name, str(default)))
    except ValueError:
        return default


def stat_float(stats: Dict[str, str], name: str, default: float = -1.0) -> float:
    try:
        return float(stats.get(name, str(default)))
    except ValueError:
        return default


class HttpResponse:
    raw: bytes
    status: int
    headers_text: str
    body: bytes
    stats: Dict[str, str]
    content_length: Optional[int]
    elapsed_s: float

    def __init__(self, raw: bytes, status: int, headers_text: str, body: bytes,
                 stats: Dict[str, str], content_length: Optional[int], elapsed_s: float):
        self.raw = raw
        self.status = status
        self.headers_text = headers_text
        self.body = body
        self.stats = stats
        self.content_length = content_length
        self.elapsed_s = elapsed_s

    @property
    def thread_id(self) -> int:
        return stat_int(self.stats, "Stat-Thread-Id")

    @property
    def thread_count(self) -> int:
        return stat_int(self.stats, "Stat-Thread-Count")

    @property
    def thread_static(self) -> int:
        return stat_int(self.stats, "Stat-Thread-Static")

    @property
    def thread_dynamic(self) -> int:
        return stat_int(self.stats, "Stat-Thread-Dynamic")

    @property
    def thread_post(self) -> int:
        return stat_int(self.stats, "Stat-Thread-Post")

    @property
    def req_arrival(self) -> float:
        return stat_float(self.stats, "Stat-Req-Arrival")

    @property
    def req_dispatch(self) -> float:
        return stat_float(self.stats, "Stat-Req-Dispatch")

    @property
    def log_arrival(self) -> float:
        return stat_float(self.stats, "Stat-Log-Arrival")

    @property
    def log_dispatch(self) -> float:
        return stat_float(self.stats, "Stat-Log-Dispatch")

    def stat_lines(self) -> List[str]:
        return [line for line in self.headers_text.splitlines() if line.startswith("Stat-")]

    def stat_signature(self) -> str:
        return "\r\n".join(self.stat_lines())

    def body_text(self) -> str:
        return self.body.decode("latin1", errors="replace")


class UdpStats:
    raw: str
    stats: Dict[str, str]

    def __init__(self, raw: str, stats: Dict[str, str]):
        self.raw = raw
        self.stats = stats

    @property
    def thread_id(self) -> int:
        return stat_int(self.stats, "Stat-Thread-Id")

    @property
    def thread_count(self) -> int:
        return stat_int(self.stats, "Stat-Thread-Count")

    @property
    def thread_static(self) -> int:
        return stat_int(self.stats, "Stat-Thread-Static")

    @property
    def thread_dynamic(self) -> int:
        return stat_int(self.stats, "Stat-Thread-Dynamic")

    @property
    def thread_post(self) -> int:
        return stat_int(self.stats, "Stat-Thread-Post")


class HttpClient:
    def __init__(self, host: str, port: int, timeout_s: float):
        self.host = host
        self.port = port
        self.timeout_s = timeout_s

    def request(self, method: str, path: str, timeout_s: Optional[float] = None) -> HttpResponse:
        timeout = self.timeout_s if timeout_s is None else timeout_s
        start = time.monotonic()
        with socket.create_connection((self.host, self.port), timeout=timeout) as sock:
            sock.settimeout(timeout)
            req = f"{method} {path} HTTP/1.1\r\nHost: integration-suite\r\nConnection: close\r\n\r\n".encode("ascii")
            sock.sendall(req)
            chunks: List[bytes] = []
            while True:
                try:
                    chunk = sock.recv(65536)
                except socket.timeout:
                    raise TimeoutError(f"{method} {path} timed out waiting for response")
                if not chunk:
                    break
                chunks.append(chunk)

        raw = b"".join(chunks)
        elapsed = time.monotonic() - start
        head, sep, body = raw.partition(b"\r\n\r\n")
        headers_text = head.decode("latin1", errors="replace")
        status = -1
        match = re.search(r"HTTP/1\.\d\s+(\d+)", headers_text)
        if match:
            status = int(match.group(1))
        content_length = None
        length_match = re.search(r"(?im)^Content-Length:\s*(\d+)\s*$", headers_text)
        if length_match:
            content_length = int(length_match.group(1))
        return HttpResponse(
            raw=raw,
            status=status,
            headers_text=headers_text + ("\r\n\r\n" if sep else ""),
            body=body,
            stats=parse_stat_lines(headers_text),
            content_length=content_length,
            elapsed_s=elapsed,
        )


class UdpClient:
    def __init__(self, host: str, port: int, timeout_s: float):
        self.host = host
        self.port = port
        self.timeout_s = timeout_s

    def ping(self, thread_id: int, timeout_s: Optional[float] = None) -> UdpStats:
        timeout = self.timeout_s if timeout_s is None else timeout_s
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.settimeout(timeout)
            sock.sendto(str(thread_id).encode("ascii"), (self.host, self.port))
            data, _ = sock.recvfrom(8192)
        text = data.decode("latin1", errors="replace")
        return UdpStats(raw=text, stats=parse_stat_lines(text))


class TestContext:
    args: argparse.Namespace
    http: HttpClient
    udp: UdpClient

    def __init__(self, args: argparse.Namespace, http: HttpClient, udp: UdpClient):
        self.args = args
        self.http = http
        self.udp = udp

    @property
    def threads(self) -> int:
        return self.args.threads

    @property
    def queue_size(self) -> int:
        return self.args.queue_size

    @property
    def debug_sleep(self) -> float:
        return self.args.debug_sleep

    @property
    def scale(self) -> int:
        return self.args.scale

    def get(self, path: str = "home.html", timeout_s: Optional[float] = None) -> HttpResponse:
        return self.http.request("GET", path, timeout_s)

    def post(self, timeout_s: Optional[float] = None) -> HttpResponse:
        return self.http.request("POST", "home.html", timeout_s)

    def slow_get(self, seconds: float, timeout_s: Optional[float] = None) -> HttpResponse:
        return self.get(f"/output.cgi?{seconds:.3f}", timeout_s=timeout_s or seconds + 10.0)

    def udp_stats(self, thread_id: int, timeout_s: Optional[float] = None) -> UdpStats:
        return self.udp.ping(thread_id, timeout_s)

    def udp_all(self, timeout_s: Optional[float] = None) -> Dict[int, UdpStats]:
        return {i: self.udp_stats(i, timeout_s) for i in range(1, self.threads + 1)}


def validate_http_response(ctx: TestContext, resp: HttpResponse, *, expect_length: bool = True) -> None:
    check(resp.status == 200, f"expected HTTP 200, got {resp.status}\n{resp.headers_text}")
    check(1 <= resp.thread_id <= ctx.threads, f"invalid Stat-Thread-Id {resp.thread_id}")
    check(resp.thread_count >= resp.thread_static + resp.thread_dynamic + resp.thread_post,
          "per-thread total should cover static, dynamic, and post counters")
    for key in (
        "Stat-Req-Arrival",
        "Stat-Req-Dispatch",
        "Stat-Log-Arrival",
        "Stat-Log-Dispatch",
        "Stat-Thread-Id",
        "Stat-Thread-Count",
        "Stat-Thread-Static",
        "Stat-Thread-Dynamic",
        "Stat-Thread-Post",
    ):
        check(key in resp.stats, f"missing {key}")
    check(resp.req_arrival <= resp.req_dispatch + 0.001,
          f"request arrival after dispatch: {resp.req_arrival} > {resp.req_dispatch}")
    check(resp.log_arrival <= resp.log_dispatch + 0.001,
          f"log arrival after dispatch: {resp.log_arrival} > {resp.log_dispatch}")
    if expect_length and resp.content_length is not None:
        check(len(resp.body) == resp.content_length,
              f"Content-Length mismatch: {len(resp.body)} != {resp.content_length}")


def count_log_entries(post_resp: HttpResponse) -> int:
    body = post_resp.body_text()
    return body.count("Stat-Req-Arrival::")


def body_contains_stats(body_text: str, resp: HttpResponse) -> bool:
    return all(line in body_text for line in resp.stat_lines())


def run_async(fn: Callable[[], object]) -> Tuple[threading.Thread, Dict[str, object]]:
    box: Dict[str, object] = {}

    def target() -> None:
        try:
            box["value"] = fn()
        except BaseException as exc:  # store for caller
            box["exc"] = exc

    th = threading.Thread(target=target, daemon=True)
    th.start()
    return th, box


def build_and_stage(args: argparse.Namespace) -> None:
    if args.no_build:
        return
    subprocess.check_call(["make", "server", "output.cgi"], cwd=REPO)
    public = REPO / "public"
    public.mkdir(exist_ok=True)
    output = REPO / "output.cgi"
    if output.exists():
        shutil.copy2(output, public / "output.cgi")


def wait_for_http(host: str, port: int, timeout_s: float) -> bool:
    end = time.monotonic() + timeout_s
    while time.monotonic() < end:
        try:
            timeout = max(0.1, min(1.0, end - time.monotonic()))
            with socket.create_connection((host, port), timeout=timeout) as sock:
                sock.settimeout(timeout)
                sock.sendall(
                    b"GET /home.html HTTP/1.1\r\n"
                    b"Host: integration-suite\r\n"
                    b"Connection: close\r\n\r\n"
                )
                data = b""
                while True:
                    chunk = sock.recv(4096)
                    if not chunk:
                        break
                    data += chunk
                if data.startswith(b"HTTP/"):
                    return True
        except (OSError, socket.timeout):
            time.sleep(0.05)
    return False


@contextlib.contextmanager
def managed_server(args: argparse.Namespace) -> Iterable[None]:
    if args.use_running:
        yield
        return

    build_and_stage(args)
    cmd = [
        str((REPO / args.server).resolve() if not os.path.isabs(args.server) else args.server),
        str(args.tcp_port),
        str(args.udp_port),
        str(args.threads),
        str(args.queue_size),
        str(args.debug_sleep),
    ]
    proc = subprocess.Popen(
        cmd,
        cwd=REPO,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.STDOUT,
        preexec_fn=os.setsid if hasattr(os, "setsid") else None,
    )
    try:
        if not wait_for_http(args.host, args.tcp_port, 5.0):
            raise TestFailure(f"server did not start on {args.host}:{args.tcp_port}")
        yield
    finally:
        if args.keep_server:
            print(f"leaving server running, pid={proc.pid}")
        else:
            with contextlib.suppress(ProcessLookupError):
                if hasattr(os, "killpg"):
                    os.killpg(proc.pid, signal.SIGTERM)
                else:
                    proc.terminate()
            with contextlib.suppress(subprocess.TimeoutExpired):
                proc.wait(timeout=3.0)
            if proc.poll() is None:
                with contextlib.suppress(ProcessLookupError):
                    if hasattr(os, "killpg"):
                        os.killpg(proc.pid, signal.SIGKILL)
                    else:
                        proc.kill()


def parse_args(argv: List[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--tcp-port", type=int, default=5003)
    parser.add_argument("--udp-port", type=int, default=5004)
    parser.add_argument("--threads", type=int, default=4)
    parser.add_argument("--queue-size", type=int, default=1)
    parser.add_argument("--debug-sleep", type=float, default=0.25)
    parser.add_argument("--server", default="./server")
    parser.add_argument("--timeout", type=float, default=8.0)
    parser.add_argument("--scale", type=int, default=1)
    parser.add_argument("--only", default="", help="substring filter for test names")
    parser.add_argument("--category", default="", help="comma-separated category filter")
    parser.add_argument("--use-running", action="store_true")
    parser.add_argument("--no-build", action="store_true")
    parser.add_argument("--keep-server", action="store_true")
    args = parser.parse_args(argv)
    if args.threads <= 0:
        parser.error("--threads must be positive")
    if args.queue_size <= 0:
        parser.error("--queue-size must be positive")
    if args.scale <= 0:
        parser.error("--scale must be positive")
    return args


def main(argv: List[str]) -> int:
    load_category_modules()
    args = parse_args(argv)
    category_filter = {part.strip() for part in args.category.split(",") if part.strip()}
    selected = [
        (category, name, fn)
        for category, name, fn in TESTS
        if args.only in name and (not category_filter or category in category_filter)
    ]
    if not selected:
        print(f"no tests matched --only {args.only!r} --category {args.category!r}", file=sys.stderr)
        return 2

    print("HW3 integration suite")
    print(f"  host={args.host} tcp={args.tcp_port} udp={args.udp_port} "
          f"threads={args.threads} queue={args.queue_size} debug_sleep={args.debug_sleep}")
    if category_filter:
        print(f"  categories={','.join(sorted(category_filter))}")

    passed = failed = skipped = 0
    with managed_server(args):
        ctx = TestContext(
            args=args,
            http=HttpClient(args.host, args.tcp_port, args.timeout),
            udp=UdpClient(args.host, args.udp_port, args.timeout),
        )
        for category, name, fn in selected:
            label = f"{category}/{name}"
            print(f"\n[ RUN  ] {label}")
            start = time.monotonic()
            try:
                fn(ctx)
            except SkipTest as exc:
                skipped += 1
                print(f"[ SKIP ] {label}: {exc}")
            except BaseException as exc:
                failed += 1
                print(f"[ FAIL ] {label}: {exc}")
            else:
                passed += 1
                print(f"[ PASS ] {label} ({time.monotonic() - start:.2f}s)")

    print(f"\nsummary: passed={passed} failed={failed} skipped={skipped}")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
