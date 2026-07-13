#!/usr/bin/env python3
"""Capture scan4raw serial CSV to a local file.

Default command sent to the board:
    scan4raw 10 250 1 2 3 4

Default output:
    scan4.csv
"""

import argparse
import json
import os
import subprocess
import sys
import time


def auto_port() -> str | None:
    env_port = os.environ.get("SCAN4_PORT")
    if env_port:
        return env_port

    try:
        result = subprocess.run(
            ["pio", "device", "list", "--json-output"],
            check=True,
            capture_output=True,
            text=True,
        )
        devices = json.loads(result.stdout)
    except Exception:
        return None

    for dev in devices:
        port = dev.get("port")
        if port:
            return port
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description="Save scan4raw CSV from the board.")
    parser.add_argument("-p", "--port", default=None, help="Serial port, e.g. /dev/ttyACM0")
    parser.add_argument("-b", "--baud", type=int, default=921600, help="Serial baud")
    parser.add_argument("-o", "--output", default="scan4.csv", help="CSV output file")
    parser.add_argument("--duration", type=int, default=10, help="scan4raw duration in seconds")
    parser.add_argument("--settle-us", type=int, default=250, help="settle delay in microseconds")
    parser.add_argument(
        "--channels",
        nargs=4,
        type=int,
        default=[1, 2, 3, 4],
        metavar=("F1", "F2", "F3", "F4"),
        help="Four F-band channel numbers",
    )
    args = parser.parse_args()

    try:
        import serial
    except ImportError:
        print("pyserial is required: python3 -m pip install pyserial", file=sys.stderr)
        return 2

    port = args.port or auto_port()
    if not port:
        print("No serial port found. Pass --port /dev/ttyACM0 or set SCAN4_PORT.", file=sys.stderr)
        return 2

    command = "scan4raw {} {} {}".format(
        args.duration,
        args.settle_us,
        " ".join(str(ch) for ch in args.channels),
    )

    print(f"Opening {port} @ {args.baud}")
    print(f"Sending: {command}")
    print(f"Writing CSV to: {args.output}")

    deadline = time.monotonic() + args.duration + 90
    in_csv = False
    rows = 0

    with serial.Serial(port, args.baud, timeout=1) as ser, open(args.output, "w", encoding="utf-8") as out:
        time.sleep(0.2)
        ser.reset_input_buffer()
        ser.write((command + "\n").encode("utf-8"))
        ser.flush()

        while time.monotonic() < deadline:
            raw = ser.readline()
            if not raw:
                continue

            line = raw.decode("utf-8", errors="replace").strip()
            if line == "# SCAN4RAW_BEGIN":
                in_csv = True
                continue
            if line.startswith("# SCAN4RAW_END"):
                print(line)
                print(f"Saved {rows} rows to {args.output}")
                return 0

            if in_csv:
                out.write(line + "\n")
                rows += 1
            elif line:
                print(line)

    print("Timed out waiting for # SCAN4RAW_END", file=sys.stderr)
    print(f"Partial rows saved: {rows}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
