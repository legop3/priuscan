#!/usr/bin/env python3
"""SavvyCAN CSV analysis helpers for the Prius v reverse engineering pass."""

from __future__ import annotations

import argparse
import csv
import math
import re
from collections import Counter, defaultdict
from pathlib import Path


DBC_BO_RE = re.compile(r"^BO_\s+(\d+)\s+([^:]+):\s+(\d+)\s+(\S+)")


class IdStats:
    def __init__(self, can_id: int) -> None:
        self.can_id = can_id
        self.count = 0
        self.dlcs: Counter[int] = Counter()
        self.first_ts: int | None = None
        self.last_ts: int | None = None
        self.prev_ts: int | None = None
        self.good_intervals: list[int] = []
        self.wraps = 0
        self.byte_min = [255] * 8
        self.byte_max = [0] * 8
        self.byte_values = [Counter() for _ in range(8)]
        self.bit_ones = [0] * 64
        self.payload_values: Counter[str] = Counter()
        self.direction: Counter[str] = Counter()
        self.bus: Counter[str] = Counter()

    def add(self, ts: int, dlc: int, data: list[int], payload: str, direction: str, bus: str) -> None:
        self.count += 1
        self.dlcs[dlc] += 1
        self.direction[direction] += 1
        self.bus[bus] += 1
        if self.first_ts is None:
            self.first_ts = ts
        self.last_ts = ts
        if self.prev_ts is not None:
            delta = ts - self.prev_ts
            if delta >= 0:
                self.good_intervals.append(delta)
            else:
                self.wraps += 1
        self.prev_ts = ts

        self.payload_values[payload] += 1
        for idx in range(8):
            value = data[idx] if idx < len(data) else 0
            self.byte_min[idx] = min(self.byte_min[idx], value)
            self.byte_max[idx] = max(self.byte_max[idx], value)
            self.byte_values[idx][value] += 1
            for bit in range(8):
                if value & (1 << bit):
                    self.bit_ones[idx * 8 + bit] += 1

    def entropy(self, counter: Counter[int]) -> float:
        total = sum(counter.values())
        if not total:
            return 0.0
        return -sum((n / total) * math.log2(n / total) for n in counter.values())

    def interval_summary(self) -> tuple[float | None, int | None, int | None]:
        if not self.good_intervals:
            return None, None, None
        intervals = sorted(self.good_intervals)
        mean = sum(intervals) / len(intervals)
        median = intervals[len(intervals) // 2]
        p95 = intervals[int((len(intervals) - 1) * 0.95)]
        return mean, median, p95

    def changing_bytes(self) -> str:
        changed = []
        for idx in range(8):
            if self.byte_min[idx] != self.byte_max[idx]:
                changed.append(f"D{idx + 1}")
        return " ".join(changed)

    def changing_bits(self) -> str:
        changed = []
        for byte_idx in range(8):
            bits = []
            for bit in range(8):
                ones = self.bit_ones[byte_idx * 8 + bit]
                if 0 < ones < self.count:
                    bits.append(str(bit))
            if bits:
                changed.append(f"D{byte_idx + 1}:b{','.join(bits)}")
        return " ".join(changed)

    def byte_ranges(self) -> str:
        return " ".join(f"{mn:02X}-{mx:02X}" for mn, mx in zip(self.byte_min, self.byte_max))

    def byte_unique_counts(self) -> str:
        return " ".join(str(len(c)) for c in self.byte_values)

    def byte_entropy(self) -> str:
        return " ".join(f"{self.entropy(c):.2f}" for c in self.byte_values)

    def top_payloads(self, limit: int = 5) -> str:
        return "; ".join(f"{payload} x{count}" for payload, count in self.payload_values.most_common(limit))


def parse_dbc_messages(paths: list[Path]) -> dict[int, list[str]]:
    messages: dict[int, list[str]] = defaultdict(list)
    for path in paths:
        with path.open(errors="replace") as fh:
            for line in fh:
                match = DBC_BO_RE.match(line)
                if not match:
                    continue
                can_id = int(match.group(1))
                name = match.group(2)
                messages[can_id].append(f"{path.name}:{name}")
    return dict(messages)


def iter_csv_rows(path: Path):
    with path.open(newline="", errors="replace") as fh:
        reader = csv.DictReader(fh)
        for row in reader:
            can_id = int(row["ID"], 16)
            ts = int(row["Time Stamp"])
            dlc = int(row["LEN"])
            data = []
            for idx in range(1, 9):
                raw = row.get(f"D{idx}", "")
                data.append(int(raw, 16) if raw else 0)
            payload = " ".join(f"{value:02X}" for value in data)
            yield can_id, ts, dlc, data, payload, row.get("Dir", ""), row.get("Bus", "")


def analyze_file(path: Path) -> dict[int, IdStats]:
    stats: dict[int, IdStats] = {}
    for can_id, ts, dlc, data, payload, direction, bus in iter_csv_rows(path):
        stats.setdefault(can_id, IdStats(can_id)).add(ts, dlc, data, payload, direction, bus)
    return stats


def write_id_summary(path: Path, stats: dict[int, IdStats], dbc: dict[int, list[str]]) -> None:
    fields = [
        "id_hex",
        "id_dec",
        "dbc_names",
        "count",
        "dlc",
        "mean_us",
        "median_us",
        "p95_us",
        "wraps",
        "unique_payloads",
        "changing_bytes",
        "byte_ranges",
        "byte_unique_counts",
        "byte_entropy",
        "changing_bits",
        "top_payloads",
    ]
    with path.open("w", newline="") as fh:
        writer = csv.DictWriter(fh, fieldnames=fields)
        writer.writeheader()
        for can_id in sorted(stats):
            item = stats[can_id]
            mean, median, p95 = item.interval_summary()
            writer.writerow(
                {
                    "id_hex": f"0x{can_id:03X}",
                    "id_dec": can_id,
                    "dbc_names": " | ".join(dbc.get(can_id, [])),
                    "count": item.count,
                    "dlc": " ".join(f"{k}:{v}" for k, v in sorted(item.dlcs.items())),
                    "mean_us": "" if mean is None else f"{mean:.2f}",
                    "median_us": "" if median is None else median,
                    "p95_us": "" if p95 is None else p95,
                    "wraps": item.wraps,
                    "unique_payloads": len(item.payload_values),
                    "changing_bytes": item.changing_bytes(),
                    "byte_ranges": item.byte_ranges(),
                    "byte_unique_counts": item.byte_unique_counts(),
                    "byte_entropy": item.byte_entropy(),
                    "changing_bits": item.changing_bits(),
                    "top_payloads": item.top_payloads(),
                }
            )


def write_capture_matrix(path: Path, captures: list[Path], dbc: dict[int, list[str]]) -> None:
    all_stats = {capture: count_ids(capture) for capture in captures}
    all_ids = sorted({can_id for stats in all_stats.values() for can_id in stats})
    with path.open("w", newline="") as fh:
        fields = ["id_hex", "id_dec", "dbc_names"] + [capture.name for capture in captures]
        writer = csv.DictWriter(fh, fieldnames=fields)
        writer.writeheader()
        for can_id in all_ids:
            row = {
                "id_hex": f"0x{can_id:03X}",
                "id_dec": can_id,
                "dbc_names": " | ".join(dbc.get(can_id, [])),
            }
            for capture in captures:
                count = all_stats[capture].get(can_id)
                row[capture.name] = "" if count is None else count
            writer.writerow(row)


def count_ids(path: Path) -> Counter[int]:
    counts: Counter[int] = Counter()
    with path.open(newline="", errors="replace") as fh:
        reader = csv.DictReader(fh)
        for row in reader:
            counts[int(row["ID"], 16)] += 1
    return counts


def write_markdown(path: Path, full_path: Path, stats: dict[int, IdStats], dbc: dict[int, list[str]]) -> None:
    total_rows = sum(item.count for item in stats.values())
    known = [can_id for can_id in stats if can_id in dbc]
    unknown = [can_id for can_id in stats if can_id not in dbc]
    ranked_unknown = sorted(unknown, key=lambda can_id: stats[can_id].count, reverse=True)

    with path.open("w") as fh:
        fh.write("# Full Drive 1 Reverse Engineering Report\n\n")
        fh.write("## Scope\n")
        fh.write("- Vehicle: 2016 Prius v Four, no Advanced Technology Package.\n")
        fh.write("- Capture point: OBD-II DLC3 HS-CAN.\n")
        fh.write("- Priority: cabin/body/display/HVAC/text/control signals over common drivetrain signals.\n")
        fh.write("- This is a running report. Confidence labels are used where the passive log does not prove meaning.\n\n")
        fh.write("## Inputs\n")
        fh.write(f"- Main log: `{full_path}`\n")
        fh.write("- Existing DBCs are used as coverage filters, not as the final output target.\n\n")
        fh.write("## First-Pass Inventory\n")
        fh.write(f"- Total frames parsed: {total_rows}\n")
        fh.write(f"- Standard IDs observed: {len(stats)}\n")
        fh.write(f"- IDs already present in at least one existing DBC: {len(known)}\n")
        fh.write(f"- IDs not present in the existing DBC set: {len(unknown)}\n\n")
        fh.write("## Highest-Volume IDs Not In Existing DBCs\n")
        fh.write("| ID | Count | DLC | Median us | Approx Hz | Changing bytes | Byte ranges | Notes |\n")
        fh.write("|---|---:|---|---:|---|---|---|\n")
        for can_id in ranked_unknown[:40]:
            item = stats[can_id]
            _, median, _ = item.interval_summary()
            hz = "" if not median else f"{1_000_000 / median:.1f}"
            dlc = " ".join(str(k) for k in sorted(item.dlcs))
            fh.write(
                f"| 0x{can_id:03X} | {item.count} | {dlc} | {'' if median is None else median} | {hz} | "
                f"{item.changing_bytes()} | {item.byte_ranges()} | candidate |\n"
            )
        fh.write("\n")
        fh.write("## Immediate Known Local Findings\n")
        fh.write("- `0x58E` byte `D6` is confirmed steering wheel directional/enter/back button code from `steerbuttons1.csv` and existing firmware notes.\n")
        fh.write("- `0x750`/`0x758` and `0x7B0`/`0x7B8` are diagnostic command/response channels for window and buzzer active tests, not normal passive cabin broadcasts.\n")
        fh.write("- `0x1568`/decimal `548` style door/seatbelt signals exist in existing DBCs, but full-log behavior should still be checked for Prius v-specific body details.\n\n")
        fh.write("## Generated Artifacts\n")
        fh.write("- `analysis/full_drive_1_id_summary.csv`: per-ID statistics for the main log.\n")
        fh.write("- `analysis/capture_id_matrix.csv`: frame-count matrix across the main and focused captures.\n\n")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--full", type=Path, required=True)
    parser.add_argument("--dbc", type=Path, action="append", default=[])
    parser.add_argument("--capture", type=Path, action="append", default=[])
    parser.add_argument("--out-dir", type=Path, default=Path("analysis"))
    args = parser.parse_args()

    args.out_dir.mkdir(parents=True, exist_ok=True)
    dbc = parse_dbc_messages(args.dbc)
    stats = analyze_file(args.full)
    write_id_summary(args.out_dir / "full_drive_1_id_summary.csv", stats, dbc)
    write_markdown(args.out_dir / "full-drive-1-reverse-engineering.md", args.full, stats, dbc)
    if args.capture:
        write_capture_matrix(args.out_dir / "capture_id_matrix.csv", [args.full] + args.capture, dbc)


if __name__ == "__main__":
    main()
