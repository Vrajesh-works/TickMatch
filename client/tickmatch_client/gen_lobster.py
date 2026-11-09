"""Synthetic order flow generator that writes LOBSTER message files.

The output uses the LOBSTER message format (Time,Type,OrderID,Size,Price,Direction)
and is internally consistent: submits never cross the opposite best price,
executions always hit the order at the head of the best level, and cancels
only reference live orders. Replaying it through a correct price-time book
therefore reproduces every execution against the named order.

Usage:
    python -m tickmatch_client.gen_lobster --messages 100000 --seed 7 --out messages.csv
"""

from __future__ import annotations

import argparse
import random
import sys
from collections import deque
from collections.abc import Iterable
from dataclasses import dataclass, field
from pathlib import Path
from typing import TextIO

SUBMIT, PARTIAL_CANCEL, DELETE, EXECUTE = 1, 2, 3, 4
BUY, SELL = 1, -1


@dataclass
class SimBook:
    """Minimal price-time book used both to generate and to validate streams."""

    bids: dict[int, deque[list[int]]] = field(default_factory=dict)
    asks: dict[int, deque[list[int]]] = field(default_factory=dict)
    orders: dict[int, tuple[int, int]] = field(default_factory=dict)  # id -> (side, price)
    live: list[int] = field(default_factory=list)
    live_pos: dict[int, int] = field(default_factory=dict)

    def _levels(self, side: int) -> dict[int, deque[list[int]]]:
        return self.bids if side == BUY else self.asks

    def best(self, side: int) -> int | None:
        levels = self._levels(side)
        if not levels:
            return None
        return max(levels) if side == BUY else min(levels)

    def add(self, oid: int, side: int, price: int, qty: int) -> None:
        self._levels(side).setdefault(price, deque()).append([oid, qty])
        self.orders[oid] = (side, price)
        self.live_pos[oid] = len(self.live)
        self.live.append(oid)

    def _drop_live(self, oid: int) -> None:
        pos = self.live_pos.pop(oid)
        last = self.live.pop()
        if last != oid:
            self.live[pos] = last
            self.live_pos[last] = pos
        del self.orders[oid]

    def find(self, oid: int) -> list[int]:
        side, price = self.orders[oid]
        for entry in self._levels(side)[price]:
            if entry[0] == oid:
                return entry
        raise KeyError(oid)

    def reduce(self, oid: int, qty: int) -> None:
        side, price = self.orders[oid]
        entry = self.find(oid)
        if qty > entry[1]:
            raise ValueError(f"reduce {qty} > remaining {entry[1]} for order {oid}")
        entry[1] -= qty
        if entry[1] == 0:
            level = self._levels(side)[price]
            level.remove(entry)
            if not level:
                del self._levels(side)[price]
            self._drop_live(oid)

    def delete(self, oid: int) -> int:
        entry = self.find(oid)
        qty = entry[1]
        self.reduce(oid, qty)
        return qty

    def head(self, side: int) -> list[int] | None:
        px = self.best(side)
        if px is None:
            return None
        return self._levels(side)[px][0]


def _fmt(t: float, kind: int, oid: int, size: int, price: int, direction: int) -> str:
    return f"{t:.9f},{kind},{oid},{size},{price},{direction}"


def generate(
    messages: int,
    seed: int = 1,
    start_price: int = 1_000_000,
    tick: int = 100,
    start_time: float = 34_200.0,
    target_depth: int = 1_500,
) -> Iterable[str]:
    """Yield `messages` LOBSTER message lines."""
    rng = random.Random(seed)
    book = SimBook()
    fair = start_price
    t = start_time
    next_id = 1

    produced = 0
    while produced < messages:
        t += rng.expovariate(1000.0)  # mean 1 ms between events
        if rng.random() < 0.05:
            fair += rng.choice((-tick, tick))
        roll = rng.random()

        # Keep the book near a steady depth: submit less often once it is deep.
        submit_p = 0.50 if len(book.live) < target_depth else 0.38
        if len(book.live) < 40 or roll < submit_p:
            side = BUY if rng.random() < 0.5 else SELL
            offset = int(rng.expovariate(0.35)) * tick  # most orders near the touch
            if side == BUY:
                price = fair - tick - offset
                ask = book.best(SELL)
                if ask is not None:
                    price = min(price, ask - tick)
            else:
                price = fair + tick + offset
                bid = book.best(BUY)
                if bid is not None:
                    price = max(price, bid + tick)
            qty = rng.choice((100, 100, 100, 200, 300, 500)) if rng.random() < 0.7 else rng.randint(1, 999)
            book.add(next_id, side, price, qty)
            yield _fmt(t, SUBMIT, next_id, qty, price, side)
            produced += 1
            next_id += 1
        elif roll < submit_p + 0.12:
            side = BUY if rng.random() < 0.5 else SELL
            head = book.head(side) or book.head(-side)
            if head is None:
                continue
            oid, remaining = head
            side, price = book.orders[oid]
            size = remaining if rng.random() < 0.4 else rng.randint(1, remaining)
            book.reduce(oid, size)
            yield _fmt(t, EXECUTE, oid, size, price, side)
            produced += 1
        elif roll < submit_p + 0.22:
            oid = rng.choice(book.live)
            side, price = book.orders[oid]
            remaining = book.find(oid)[1]
            if remaining < 2:
                continue
            size = rng.randint(1, remaining - 1)
            book.reduce(oid, size)
            yield _fmt(t, PARTIAL_CANCEL, oid, size, price, side)
            produced += 1
        else:
            oid = rng.choice(book.live)
            side, price = book.orders[oid]
            size = book.delete(oid)
            yield _fmt(t, DELETE, oid, size, price, side)
            produced += 1


def validate(lines: Iterable[str]) -> dict[str, int]:
    """Replay lines in a reference book and check internal consistency.

    Raises ValueError on the first inconsistency; returns counts per event type.
    """
    book = SimBook()
    counts = {"submit": 0, "partial_cancel": 0, "delete": 0, "execute": 0}
    last_t = 0.0
    for n, line in enumerate(lines, 1):
        parts = line.strip().split(",")
        if len(parts) != 6:
            raise ValueError(f"line {n}: expected 6 fields")
        t = float(parts[0])
        kind, oid, size, price, direction = (int(p) for p in parts[1:])
        if t < last_t:
            raise ValueError(f"line {n}: time went backwards")
        last_t = t
        if direction not in (BUY, SELL) or size <= 0:
            raise ValueError(f"line {n}: bad direction or size")
        if kind == SUBMIT:
            opp = book.best(-direction)
            if opp is not None and (price >= opp if direction == BUY else price <= opp):
                raise ValueError(f"line {n}: submit crosses the book")
            if oid in book.orders:
                raise ValueError(f"line {n}: duplicate order id {oid}")
            book.add(oid, direction, price, size)
            counts["submit"] += 1
        elif kind == EXECUTE:
            head = book.head(direction)
            if head is None or head[0] != oid:
                raise ValueError(f"line {n}: execution is not against the head of the best level")
            book.reduce(oid, size)
            counts["execute"] += 1
        elif kind == PARTIAL_CANCEL:
            book.reduce(oid, size)
            counts["partial_cancel"] += 1
        elif kind == DELETE:
            if book.find(oid)[1] != size:
                raise ValueError(f"line {n}: delete size does not match remaining")
            book.delete(oid)
            counts["delete"] += 1
        else:
            raise ValueError(f"line {n}: unsupported type {kind}")
    return counts


def write(lines: Iterable[str], out: TextIO) -> int:
    count = 0
    for line in lines:
        out.write(line)
        out.write("\n")
        count += 1
    return count


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--messages", type=int, default=100_000)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--start-price", type=int, default=1_000_000, help="dollars x 10000")
    parser.add_argument("--tick", type=int, default=100, help="price tick in LOBSTER units")
    parser.add_argument("--out", type=Path, help="output file (default: stdout)")
    args = parser.parse_args(argv)

    lines = generate(args.messages, args.seed, args.start_price, args.tick)
    if args.out is None:
        write(lines, sys.stdout)
    else:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        with args.out.open("w", encoding="ascii") as fh:
            n = write(lines, fh)
        print(f"wrote {n} messages to {args.out}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
