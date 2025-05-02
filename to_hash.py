#!/usr/bin/env python3
import sys

import base58
import bech32


def hex_encode(bytes):
    return "".join(f"{b:02x}" for b in bytes)


def process_line(line: str):
    if line.startswith("s-") or line.startswith("d-") or line.startswith("m-"):
        return

    if line.startswith("bc1"):
        data = bech32.decode("bc", line)[1]
        if data is None or len(data) != 20:
            return "bc1"

        print(hex_encode(data))

    if line.startswith("1") or line.startswith("3"):
        data = base58.b58decode(line)
        data = data[1 : 20 + 1]
        if len(data) != 20:
            return

        print(hex_encode(data))


def main():
    wal_pos = int(sys.argv[1] if len(sys.argv) > 1 else "0")
    while True:
        try:
            line = input()
            line = line.strip().replace(",", ";").replace("\t", ";").replace(" ", ";")
            line = line.split(";")[wal_pos]
            process_line(line)
        except EOFError:
            break


if __name__ == "__main__":
    main()
