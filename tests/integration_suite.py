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
    --threads N           server worker count when the suite launches the server
    --queue-size N        queue size when the suite launches the server
    --debug-sleep SEC     log debug sleep; several tests benefit from > 0
"""

from __future__ import annotations

import argparse
import contextlib
import dataclasses
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
TESTS: List[Tuple[str, Callable[["TestContext"], None]]] = []


def test(name: str) -> Callable[[Callable[["TestContext"], None]], Callable[["TestContext"], None]]:
    def register(fn: Callable[["TestContext"], None]) -> Callable[["TestContext"], None]:
        TESTS.append((name, fn))
        return fn

    return register


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


@dataclasses.dataclass
class HttpResponse:
    raw: bytes
    status: int
    headers_text: str
    body: bytes
    stats: Dict[str, str]
    content_length: Optional[int]
    elapsed_s: float

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


@dataclasses.dataclass
class UdpStats:
    raw: str
    stats: Dict[str, str]

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
            req = f"{method} {path} HTTP/1.1\r\nHost: integration-suite\r\n\r\n".encode("ascii")
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


@dataclasses.dataclass
class TestContext:
    args: argparse.Namespace
    http: HttpClient
    udp: UdpClient

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
    check(resp.thread_count == resp.thread_static + resp.thread_dynamic + resp.thread_post,
          "per-thread counter invariant failed")
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


@test("basic_http_and_thread_headers")
def basic_http_and_thread_headers(ctx: TestContext) -> None:
    get_resp = ctx.get()
    validate_http_response(ctx, get_resp)

    post_resp = ctx.post()
    validate_http_response(ctx, post_resp)
    check(post_resp.content_length == len(post_resp.body),
          "POST body length matches Content-Length")


@test("thread_id_headers_match_udp_counter_deltas")
def thread_id_headers_match_udp_counter_deltas(ctx: TestContext) -> None:
    before = ctx.udp_all(timeout_s=2.0)
    seen = {
        i: {"total": 0, "static": 0, "dynamic": 0, "post": 0}
        for i in range(1, ctx.threads + 1)
    }

    for _ in range(max(8, ctx.threads * 3)):
        resp = ctx.get()
        validate_http_response(ctx, resp)
        s = seen[resp.thread_id]
        s["total"] += 1
        s["static"] += 1

    for _ in range(max(4, ctx.threads)):
        resp = ctx.post()
        validate_http_response(ctx, resp)
        s = seen[resp.thread_id]
        s["total"] += 1
        s["post"] += 1

    after = ctx.udp_all(timeout_s=2.0)
    for tid in range(1, ctx.threads + 1):
        delta_total = after[tid].thread_count - before[tid].thread_count
        delta_static = after[tid].thread_static - before[tid].thread_static
        delta_dynamic = after[tid].thread_dynamic - before[tid].thread_dynamic
        delta_post = after[tid].thread_post - before[tid].thread_post
        check(delta_total == seen[tid]["total"],
              f"thread {tid}: HTTP ids counted {seen[tid]['total']} total, UDP delta was {delta_total}")
        check(delta_static == seen[tid]["static"],
              f"thread {tid}: static delta mismatch")
        check(delta_dynamic == seen[tid]["dynamic"],
              f"thread {tid}: dynamic delta mismatch")
        check(delta_post == seen[tid]["post"],
              f"thread {tid}: post delta mismatch")


@test("log_entry_reflected_and_debug_sleep_timing")
def log_entry_reflected_and_debug_sleep_timing(ctx: TestContext) -> None:
    before = ctx.post()
    validate_http_response(ctx, before)
    before_count = count_log_entries(before)

    written: List[HttpResponse] = []
    for _ in range(3):
        resp = ctx.get()
        validate_http_response(ctx, resp)
        written.append(resp)

    after = ctx.post()
    validate_http_response(ctx, after)
    after_text = after.body_text()
    check(count_log_entries(after) == before_count + 3,
          "POST-visible log grew by exactly the number of valid GET writers")
    for resp in written:
        check(body_contains_stats(after_text, resp),
              "GET response Stat-* entry is reflected exactly in the POST log body")

    if ctx.debug_sleep > 0:
        min_expected = ctx.debug_sleep * 0.75
        for resp in written:
            log_time = resp.log_dispatch - resp.log_arrival
            check(log_time >= min_expected,
                  f"GET log time {log_time:.3f}s did not include debug sleep {ctx.debug_sleep:.3f}s")
        post_log_time = after.log_dispatch - after.log_arrival
        check(post_log_time >= min_expected,
              f"POST log time {post_log_time:.3f}s did not include debug sleep {ctx.debug_sleep:.3f}s")


@test("udp_responsive_when_tcp_queue_is_full")
def udp_responsive_when_tcp_queue_is_full(ctx: TestContext) -> None:
    if ctx.queue_size >= ctx.threads:
        raise SkipTest("requires queue_size < threads so at least one worker can be idle while TCP is full")

    slow_seconds = max(0.8, ctx.debug_sleep + 0.6)
    workers: List[threading.Thread] = []
    boxes: List[Dict[str, object]] = []
    for _ in range(ctx.queue_size):
        th, box = run_async(lambda s=slow_seconds: ctx.slow_get(s, timeout_s=s + 8.0))
        workers.append(th)
        boxes.append(box)

    time.sleep(0.20)
    responsive = 0
    mismatches = 0
    for tid in range(1, ctx.threads + 1):
        try:
            start = time.monotonic()
            stats = ctx.udp_stats(tid, timeout_s=0.35)
            elapsed = time.monotonic() - start
            if stats.thread_id == tid and elapsed < 0.35:
                responsive += 1
            elif stats.thread_id != tid:
                mismatches += 1
        except (TimeoutError, OSError, socket.timeout):
            pass

    check(mismatches == 0, "no UDP replies were routed to the wrong worker")
    check(responsive >= 1,
          "at least one UDP ping completed quickly while TCP capacity was full")

    for th in workers:
        th.join(timeout=slow_seconds + 8.0)
    for box in boxes:
        if "exc" in box:
            raise box["exc"]  # type: ignore[misc]
        resp = box.get("value")
        check(isinstance(resp, HttpResponse), "slow TCP request returned a response")
        validate_http_response(ctx, resp, expect_length=False)


@test("server_wakes_after_tcp_queue_was_full")
def server_wakes_after_tcp_queue_was_full(ctx: TestContext) -> None:
    if ctx.queue_size != 1:
        raise SkipTest("best run with queue_size == 1 for a deterministic full-queue wakeup probe")

    slow_seconds = max(0.9, ctx.debug_sleep + 0.7)
    slow_th, slow_box = run_async(lambda: ctx.slow_get(slow_seconds, timeout_s=slow_seconds + 8.0))
    time.sleep(0.20)

    queued_th, queued_box = run_async(lambda: ctx.get(timeout_s=slow_seconds + 8.0))
    time.sleep(min(0.35, slow_seconds / 2.0))
    check(queued_th.is_alive(),
          "second TCP request did not complete while the one-slot capacity was still full")

    slow_th.join(timeout=slow_seconds + 8.0)
    queued_th.join(timeout=slow_seconds + 8.0)
    check(not queued_th.is_alive(), "server did not wake and accept queued TCP after capacity freed")

    for box in (slow_box, queued_box):
        if "exc" in box:
            raise box["exc"]  # type: ignore[misc]
        resp = box.get("value")
        check(isinstance(resp, HttpResponse), "TCP request returned a response after wakeup")
        validate_http_response(ctx, resp, expect_length=False)


@test("strict_writer_priority_over_later_readers")
def strict_writer_priority_over_later_readers(ctx: TestContext) -> None:
    if ctx.debug_sleep <= 0:
        raise SkipTest("requires debug_sleep > 0 to create an observable reader/writer queue")

    lead_reader_th, lead_reader_box = run_async(lambda: ctx.post(timeout_s=ctx.debug_sleep + 8.0))
    time.sleep(min(0.15, ctx.debug_sleep / 2.0))

    writer_th, writer_box = run_async(lambda: ctx.get(timeout_s=ctx.debug_sleep * 3.0 + 8.0))
    time.sleep(min(0.15, ctx.debug_sleep / 2.0))

    late_readers: List[threading.Thread] = []
    late_boxes: List[Dict[str, object]] = []
    late_count = max(3, ctx.threads)
    for _ in range(late_count):
        th, box = run_async(lambda: ctx.post(timeout_s=ctx.debug_sleep * 4.0 + 8.0))
        late_readers.append(th)
        late_boxes.append(box)

    lead_reader_th.join(timeout=ctx.debug_sleep * 4.0 + 8.0)
    writer_th.join(timeout=ctx.debug_sleep * 4.0 + 8.0)
    for th in late_readers:
        th.join(timeout=ctx.debug_sleep * 4.0 + 8.0)

    for box in [lead_reader_box, writer_box, *late_boxes]:
        if "exc" in box:
            raise box["exc"]  # type: ignore[misc]
        check(isinstance(box.get("value"), HttpResponse), "priority scenario request completed")

    writer = writer_box["value"]  # type: ignore[assignment]
    check(isinstance(writer, HttpResponse), "writer response present")
    validate_http_response(ctx, writer)

    for box in late_boxes:
        late = box["value"]
        check(isinstance(late, HttpResponse), "late reader response present")
        validate_http_response(ctx, late)
        check(writer.log_dispatch <= late.log_dispatch + 0.001,
              "writer log dispatch happened before each later reader completed its log read")
        check(body_contains_stats(late.body_text(), writer),
              "late POST reader saw the writer entry that arrived before it")


@test("mixed_load_no_deadlock_and_basic_performance")
def mixed_load_no_deadlock_and_basic_performance(ctx: TestContext) -> None:
    total = max(40, 80 * ctx.scale)
    concurrency = min(max(4, ctx.threads * 3), total)
    lock = threading.Lock()
    errors: List[str] = []
    latencies: List[float] = []
    workers_used: set[int] = set()
    next_index = 0

    def worker() -> None:
        nonlocal next_index
        while True:
            with lock:
                if next_index >= total:
                    return
                i = next_index
                next_index += 1
            try:
                if i % 5 == 0:
                    resp = ctx.post(timeout_s=10.0)
                else:
                    resp = ctx.get(timeout_s=10.0)
                validate_http_response(ctx, resp)
                with lock:
                    latencies.append(resp.elapsed_s)
                    workers_used.add(resp.thread_id)
            except BaseException as exc:
                with lock:
                    errors.append(str(exc))

    threads = [threading.Thread(target=worker, daemon=True) for _ in range(concurrency)]
    start = time.monotonic()
    for th in threads:
        th.start()
    for th in threads:
        th.join(timeout=30.0)
    elapsed = time.monotonic() - start

    stuck = sum(1 for th in threads if th.is_alive())
    check(stuck == 0, f"{stuck} load workers stuck, possible deadlock")
    check(not errors, "mixed load completed without failed HTTP responses: " + "; ".join(errors[:3]))
    check(len(latencies) == total, f"completed {len(latencies)}/{total} mixed requests")
    check(len(workers_used) >= min(ctx.threads, 2),
          f"load used too few workers: {sorted(workers_used)}")

    latencies.sort()
    p50 = latencies[len(latencies) // 2] * 1000.0
    p95 = latencies[int((len(latencies) - 1) * 0.95)] * 1000.0
    throughput = total / elapsed if elapsed > 0 else 0.0
    print(f"      performance: throughput={throughput:.1f} req/s p50={p50:.1f}ms p95={p95:.1f}ms "
          f"workers={sorted(workers_used)}")


def build_and_stage(args: argparse.Namespace) -> None:
    if args.no_build:
        return
    subprocess.check_call(["make", "server", "output.cgi"], cwd=REPO)
    public = REPO / "public"
    public.mkdir(exist_ok=True)
    output = REPO / "output.cgi"
    if output.exists():
        shutil.copy2(output, public / "output.cgi")


def wait_for_tcp(host: str, port: int, timeout_s: float) -> bool:
    end = time.monotonic() + timeout_s
    while time.monotonic() < end:
        try:
            with socket.create_connection((host, port), timeout=0.2):
                return True
        except OSError:
            time.sleep(0.05)
    return False


@contextlib.contextmanager
def managed_server(args: argparse.Namespace) -> Iterable[None]:
    if args.use_running:
        check(wait_for_tcp(args.host, args.tcp_port, 3.0),
              f"no running server reachable at {args.host}:{args.tcp_port}")
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
        if not wait_for_tcp(args.host, args.tcp_port, 5.0):
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
    args = parse_args(argv)
    selected = [(name, fn) for name, fn in TESTS if args.only in name]
    if not selected:
        print(f"no tests matched --only {args.only!r}", file=sys.stderr)
        return 2

    print("HW3 integration suite")
    print(f"  host={args.host} tcp={args.tcp_port} udp={args.udp_port} "
          f"threads={args.threads} queue={args.queue_size} debug_sleep={args.debug_sleep}")

    passed = failed = skipped = 0
    with managed_server(args):
        ctx = TestContext(
            args=args,
            http=HttpClient(args.host, args.tcp_port, args.timeout),
            udp=UdpClient(args.host, args.udp_port, args.timeout),
        )
        for name, fn in selected:
            print(f"\n[ RUN  ] {name}")
            start = time.monotonic()
            try:
                fn(ctx)
            except SkipTest as exc:
                skipped += 1
                print(f"[ SKIP ] {name}: {exc}")
            except BaseException as exc:
                failed += 1
                print(f"[ FAIL ] {name}: {exc}")
            else:
                passed += 1
                print(f"[ PASS ] {name} ({time.monotonic() - start:.2f}s)")

    print(f"\nsummary: passed={passed} failed={failed} skipped={skipped}")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
