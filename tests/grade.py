#!/usr/bin/env python3
"""Self-contained 10-point grader. Python 3.9+; C11 GCC or Clang.

Public cases are practice checks, not a tamper-proof examination service.
An instructor should run a trusted copy of this grader and review the algorithm.
"""

import argparse
import hashlib
import html
import json
import math
import os
from pathlib import Path
import random
import shlex
import shutil
import signal
import subprocess
import sys
import tempfile
import time


ROOT = Path(__file__).resolve().parents[1]
MAX_OUTPUT = 2 * 1024 * 1024
TOTAL = 10
GROUPS = [
    ("preprocessing", "1. Урьдчилсан хүснэгт / hash", 1),
    ("basic", "2. Үндсэн substring хайлт", 1),
    ("case", "3. ASCII том, жижиг үсэг ба -i", 1),
    ("overlap", "4. Давхцал, мөрийн дугаар", 2),
    ("boundary", "5. Хязгаар, byte өгөгдөл, CLI алдаа", 2),
    ("random", "6. Давтан API, санамсаргүй тест, collision", 1),
    ("performance", "7. Хоёр 4 MiB тест / хугацаа", 2),
]


def configure_console():
    """Use UTF-8 even when Python stdout is a Windows pipe/legacy code page.

    verify_all.py captures this process as UTF-8. This also protects direct
    invocation with PYTHONIOENCODING=cp1252; users need no shell encoding setup.
    It changes diagnostic streams only, never the C program's byte output.
    """
    for stream in (sys.stdout, sys.stderr):
        reconfigure = getattr(stream, "reconfigure", None)
        if callable(reconfigure):
            reconfigure(encoding="utf-8", errors="backslashreplace")


def child_limits():
    """Bound disk output on POSIX; time limits are enforced by the parent."""
    try:
        import resource
        resource.setrlimit(resource.RLIMIT_FSIZE, (MAX_OUTPUT, MAX_OUTPUT))
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    except (ImportError, OSError, ValueError):
        pass


def run_bounded(command, timeout, directory):
    start = time.perf_counter()
    result = {"code": None, "timeout": False, "elapsed_seconds": 0.0,
              "stdout": b"", "stderr": b"", "error": None}
    with tempfile.TemporaryFile(dir=directory) as out, tempfile.TemporaryFile(dir=directory) as err:
        try:
            process = subprocess.Popen(
                command, cwd=ROOT, stdout=out, stderr=err,
                start_new_session=(os.name == "posix"),
                preexec_fn=child_limits if os.name == "posix" else None,
            )
            try:
                result["code"] = process.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                result["timeout"] = True
                if os.name == "posix":
                    try:
                        os.killpg(process.pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
                else:
                    process.kill()
                result["code"] = process.wait()
        except OSError as exc:
            result["error"] = str(exc)
        out.seek(0)
        err.seek(0)
        result["stdout"] = out.read(MAX_OUTPUT + 1)
        result["stderr"] = err.read(MAX_OUTPUT + 1)
    result["elapsed_seconds"] = round(time.perf_counter() - start, 6)
    return result


def ascii_lower(data):
    return data.translate(bytes.maketrans(b"ABCDEFGHIJKLMNOPQRSTUVWXYZ", b"abcdefghijklmnopqrstuvwxyz"))


def expected_output(data, pattern, ignore_case=False):
    """Independent Python oracle; includes overlapping matches and raw context."""
    haystack = ascii_lower(data) if ignore_case else data
    needle = ascii_lower(pattern) if ignore_case else pattern
    output = []
    position = haystack.find(needle)
    cursor = 0
    line = 0
    while position >= 0:
        line += data.count(b"\n", cursor, position)
        cursor = position
        left = max(0, position - 5)
        right = min(len(data), position + len(pattern) + 5)
        output.append(str(line).encode("ascii") + b": " + data[left:right] + b"\n")
        position = haystack.find(needle, position + 1)
    return b"".join(output)


def brief(data):
    return repr(data[:160]) + (" ..." if len(data) > 160 else "")


def run_detail(result):
    if result["error"]:
        return result["error"]
    if result["timeout"]:
        return "Хугацаа хэтэрсэн"
    if result["code"] != 0:
        return "exit={} stderr={}".format(result["code"], brief(result["stderr"]))
    return ""


class Checker:
    def __init__(self, executable, unit, temporary, timeout):
        self.executable = executable
        self.unit = unit
        self.temporary = temporary
        self.timeout = timeout
        self.sequence = 0
        self.checks = []

    def record(self, name, result, passed, detail=""):
        self.checks.append({"name": name, "passed": bool(passed),
                            "elapsed_seconds": result["elapsed_seconds"],
                            "detail": detail})
        return bool(passed)

    def api(self, mode):
        result = run_bounded([str(self.unit), mode], self.timeout, self.temporary)
        return self.record("API: " + mode, result,
                           not result["timeout"] and not result["error"] and result["code"] == 0,
                           run_detail(result))

    def cli(self, name, data, pattern, ignore_case=False, literal=False, filename=None):
        self.sequence += 1
        path = self.temporary / (filename or "input-{}.bin".format(self.sequence))
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        args = [str(self.executable)]
        if ignore_case:
            args.append("-i")
        if literal:
            args.append("--")
        # CLI patterns are UTF-8 text; arbitrary byte patterns use API tests.
        args.extend([pattern.decode("utf-8"), str(path)])
        wanted = expected_output(data, pattern, ignore_case)
        result = run_bounded(args, self.timeout, self.temporary)
        passed = (not result["timeout"] and not result["error"] and result["code"] == 0
                  and result["stdout"] == wanted and not result["stderr"])
        detail = run_detail(result)
        if not passed and not detail:
            detail = "expected={} actual={} stderr={}".format(
                brief(wanted), brief(result["stdout"]), brief(result["stderr"]))
        path.unlink()
        return self.record(name, result, passed, detail)

    def error_case(self, name, arguments):
        result = run_bounded([str(self.executable)] + arguments, self.timeout, self.temporary)
        passed = (not result["timeout"] and not result["error"] and result["code"] == 2
                  and not result["stdout"] and bool(result["stderr"]))
        return self.record(name, result, passed,
                           "" if passed else "Expected exit 2 and stderr; " + run_detail(result))

    def group(self, name):
        if name == "preprocessing":
            return self.api("prep")
        if name == "basic":
            return (self.api("basic")
                    and self.cli("Энгийн олон илрэл", b"hay needle hay needle", b"needle")
                    and self.cli("Олдохгүй хэв", b"hay needle hay", b"missing")
                    and self.cli("Бүх файл нэг match", b"exact", b"exact"))
        if name == "case":
            return (self.cli("Case-sensitive", b"ABC abc AbC aBc", b"abc")
                    and self.cli("-i хэв ба текст", b"ABC abc AbC aBc", b"aBc", True)
                    and self.cli("ASCII -i, эх текст хэвээр", b"BSD bsd Bsd\n+Xx+", b"BsD", True)
                    and self.cli("UTF-8 case folding хийгдэхгүй",
                                 "Монгол монгол MONGOL mongol".encode(), "монгол".encode(), True))
        if name == "overlap":
            return (self.api("overlap")
                    and self.cli("Давхцсан aaa", b"aaaaa", b"aaa")
                    and self.cli("Мөрийн эхлэл 0", b"\n\nabc\nabc abc\n", b"abc")
                    and self.cli("Newline агуулсан давхцал", b"a\na\na\n", b"a\na")
                    and self.cli("Newline өөрөө хэв", b"a\n\nb\n", b"\n"))
        if name == "boundary":
            if not (self.api("boundary")
                    and self.cli("Хоосон текст", b"", b"x")
                    and self.cli("Текстээс урт хэв", b"ab", b"abcdef")
                    and self.cli("Эхлэл ба төгсгөл", b"abc0123456789abc", b"abc")
                    and self.cli("Сүүлийн newline байхгүй", b"tailXYZ", b"XYZ")
                    and self.cli("Текстэд NUL болон 128..255 byte", b"\x00\xffabC\x00tail\x80ABC", b"abc", True)
                    and self.cli("CRLF байтуудыг хэвээр хадгалах", b"AA\r\nabc\r\nBB\r\nabc\r\n", b"abc")
                    and self.cli("Дан CR байтыг хэвээр хадгалах", b"before\rABC\rafter", b"abc", True)
                    and self.cli("NUL болон Ctrl-Z нь EOF биш", b"\x1a\x00ABC\x00\x1a ABC tail", b"ABC")
                    and self.cli("Unicode хэв, хавтас, файлын нэр",
                                 "Монгол монгол MONGOL".encode("utf-8"), "монгол".encode("utf-8"),
                                 filename="Монгол хавтас/өгөгдөл 01.bin")
                    and self.cli("-- дараах -i хэв", b"xx-i-ixx", b"-i", literal=True)
                    and self.cli("-i -- дараах хэв", b"-i -I -x", b"-I", True, True)):
                return False
            existing = self.temporary / "args.txt"
            existing.write_bytes(b"abc")
            return (self.error_case("Аргумент дутуу", [])
                    and self.error_case("-i аргумент дутуу", ["-i", str(existing)])
                    and self.error_case("Хоосон хэв", ["", str(existing)])
                    and self.error_case("Байхгүй файл", ["abc", str(self.temporary / "not-found.txt")])
                    and self.error_case("Аргумент илүү", ["abc", str(existing), "extra"]))
        if name == "random":
            return self.api("random")
        if name == "performance":
            n = 4 * 1024 * 1024
            rng = random.Random(20261122)
            alphabet = b"abcdefghijklmnopqrst0123456789 \n"
            table = bytes(alphabet[i % len(alphabet)] for i in range(256))
            data = bytearray(rng.randbytes(n).translate(table))
            key = b"SEARCH_LAB_2026_0123456789_ABCDEFGHIJKLMNOPQRSTUVWXYZ_END"
            for pos in (0, 4095, 30000, 1000001, 3000000, n - len(key)):
                data[pos:pos + len(key)] = key
            if not self.cli("4 MiB: sparse matches", bytes(data), key):
                return False
            # Fair to the lecture's bad-character-only Boyer-Moore variant:
            # each failed alignment mismatches at the final character.
            return self.cli("4 MiB: repeated prefix", b"a" * (n - 1) + b"b", b"a" * 1023 + b"b")
        raise ValueError("Unknown group: " + name)


def badge_svg(score, total):
    color = "#16803c" if score == total else ("#b45309" if score else "#b91c1c")
    label = html.escape("{}/{}".format(score, total))
    return ('<svg xmlns="http://www.w3.org/2000/svg" width="116" height="20" '
            'role="img" aria-label="Points: ' + label + '">'
            '<rect width="60" height="20" fill="#374151"/>'
            '<rect x="60" width="56" height="20" fill="' + color + '"/>'
            '<g fill="#fff" text-anchor="middle" font-family="Verdana,sans-serif" font-size="11">'
            '<text x="30" y="14">Points</text><text x="88" y="14">' + label + '</text></g></svg>\n')


def save_reports(report, directory):
    directory.mkdir(parents=True, exist_ok=True)
    lines = ["# {} — автомат шалгалт".format(report["algorithm"]), "",
             "**Нийт: {}/{}**".format(report["score"], report["total"]), "",
             "| Бүлэг | Оноо | Үр дүн |", "|---|---:|---|"]
    for group in report["groups"]:
        lines.append("| {} | {}/{} | {} |".format(
            group["name"], group["earned"], group["points"], group["status"]))
    lines.extend(["", "Эх код SHA-256: `{}`".format(report["source_sha256"] or "unavailable"),
                  "", "Хугацааны хязгаар: нэг ажиллуулалтад {} секунд.".format(report["timeout_seconds"]),
                  "", "Энэ оноо нь өгөгдсөн тестийн үр дүн. Шаардсан алгоритмыг ашигласныг багш кодоор шалгана."])
    if report.get("fatal_error"):
        lines.extend(["", "Алдаа: " + report["fatal_error"]])
    failures = [(group, case) for group in report["groups"] for case in group.get("checks", []) if not case["passed"]]
    if failures:
        lines.extend(["", "## Шалгах зүйлс", ""])
        for group, case in failures:
            lines.append("- {} / {}: {}".format(group["id"], case["name"], case["detail"]).replace("\n", " "))
    summary = "\n".join(lines) + "\n"
    (directory / "results.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    (directory / "summary.md").write_text(summary, encoding="utf-8")
    (directory / "points.svg").write_text(badge_svg(report["score"], report["total"]), encoding="utf-8")
    step_summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if step_summary:
        with open(step_summary, "a", encoding="utf-8") as handle:
            handle.write(summary)
    github_output = os.environ.get("GITHUB_OUTPUT")
    if github_output:
        with open(github_output, "a", encoding="utf-8") as handle:
            handle.write("score={}\ntotal={}\n".format(report["score"], report["total"]))


def main():
    configure_console()
    parser = argparse.ArgumentParser(description="Substring laboratory grader, 10 points")
    parser.add_argument("--source", type=Path, default=ROOT / "search.c", help="trusted alternate search.c")
    parser.add_argument("--report-dir", type=Path, default=ROOT / ".grading")
    parser.add_argument("--timeout", type=float, default=4.0)
    parser.add_argument("--skip-performance", action="store_true", help="run quick checks; skipped points are NOT awarded")
    args = parser.parse_args()
    if not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error("--timeout must be finite and greater than zero")
    source = args.source.resolve()
    report_dir = args.report_dir.resolve()
    report = {"schema_version": 1, "grader_revision": "2.1",
              "algorithm": ROOT.name, "score": 0, "total": TOTAL,
              "timeout_seconds": args.timeout, "source_file": source.name,
              "source_sha256": None, "compile": {"passed": False, "commands": []},
              "groups": [{"id": ident, "name": name, "points": points, "earned": 0,
                          "status": "NOT RUN", "checks": []} for ident, name, points in GROUPS],
              "fatal_error": None}
    try:
        config = json.loads((ROOT / "tests" / "config.json").read_text(encoding="utf-8"))
        report["algorithm"] = config["algorithm"]
        if config["total"] != TOTAL:
            raise ValueError("config.json total must be 10")
        report["source_sha256"] = hashlib.sha256(source.read_bytes()).hexdigest()
        compiler_setting = os.environ.get("CC", "gcc").strip()
        # Preserve a complete executable path, including Windows backslashes and
        # spaces. Split only when CC is a command plus explicit arguments.
        compiler = ([compiler_setting] if compiler_setting and shutil.which(compiler_setting)
                    else shlex.split(compiler_setting))
        if not compiler or shutil.which(compiler[0]) is None:
            raise RuntimeError("C compiler not found. Install GCC/Clang or set CC.")
        flags = ["-std=c11", "-O2", "-Wall", "-Wextra", "-Wpedantic"]
        flags += shlex.split(os.environ.get("EXTRA_CFLAGS", ""))
        with tempfile.TemporaryDirectory(prefix="substring-grade-") as folder:
            tmp = Path(folder)
            suffix = ".exe" if os.name == "nt" else ""
            executable, unit = tmp / ("mygrep" + suffix), tmp / ("unit" + suffix)
            compile_passed = True
            for main_file, target in [(ROOT / "mygrep.c", executable), (ROOT / "tests" / "unit.c", unit)]:
                # The native Windows CLI obtains UTF-16 argv through Shell32.
                libraries = ["-lshell32"] if os.name == "nt" and target == executable else []
                command = compiler + flags + ["-I", str(ROOT), str(main_file), str(source), "-o", str(target)] + libraries
                result = run_bounded(command, 30.0, tmp)
                passed = not result["timeout"] and not result["error"] and result["code"] == 0
                compile_passed = compile_passed and passed
                log = (result["stdout"] + result["stderr"]).decode("utf-8", "replace")[:65536]
                report["compile"]["commands"].append({"target": target.name, "passed": passed,
                                                     "elapsed_seconds": result["elapsed_seconds"],
                                                     "exit_code": result["code"], "log": log,
                                                     "error": run_detail(result)})
                if log:
                    print(log.rstrip())
                if not passed:
                    print("Compile failed: " + run_detail(result))
            report["compile"]["passed"] = compile_passed
            if compile_passed:
                checker = Checker(executable, unit, tmp, args.timeout)
                for group in report["groups"]:
                    if group["id"] == "performance" and args.skip_performance:
                        group["status"] = "SKIPPED"
                        print("{}: skipped (0/{})".format(group["name"], group["points"]))
                        continue
                    checker.checks = []
                    passed = checker.group(group["id"])
                    group["checks"] = checker.checks
                    group["elapsed_seconds"] = round(sum(c["elapsed_seconds"] for c in checker.checks), 6)
                    group["earned"] = group["points"] if passed else 0
                    group["status"] = "PASS" if passed else "FAIL"
                    report["score"] += group["earned"]
                    print("{}: {}/{} {}".format(group["name"], group["earned"], group["points"], group["status"]))
                    for check in checker.checks:
                        if not check["passed"]:
                            print("  {}: {}".format(check["name"], check["detail"]))
            else:
                report["fatal_error"] = "C code did not compile; no points awarded."
    except (OSError, ValueError, KeyError, RuntimeError) as exc:
        report["fatal_error"] = str(exc)
        print("Grader error: " + str(exc), file=sys.stderr)
    finally:
        save_reports(report, report_dir)
    print("\nRESULT: {}/{}".format(report["score"], report["total"]))
    print("Reports: " + str(report_dir))
    return 0 if report["score"] == TOTAL and not report["fatal_error"] else 1


if __name__ == "__main__":
    sys.exit(main())
