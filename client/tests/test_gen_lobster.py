import io

import pytest

from tickmatch_client import gen_lobster as g


def test_generates_exact_count_and_is_deterministic():
    a = list(g.generate(2_000, seed=3))
    b = list(g.generate(2_000, seed=3))
    c = list(g.generate(2_000, seed=4))
    assert len(a) == 2_000
    assert a == b
    assert a != c


def test_lines_have_lobster_shape():
    for line in g.generate(500, seed=1):
        t, kind, oid, size, price, direction = line.split(",")
        assert float(t) >= 34_200.0
        assert int(kind) in (1, 2, 3, 4)
        assert int(oid) > 0 and int(size) > 0 and int(price) > 0
        assert int(direction) in (1, -1)
        assert int(price) % 100 == 0  # one cent tick


def test_stream_is_internally_consistent_and_mixes_all_events():
    counts = g.validate(g.generate(20_000, seed=11))
    assert all(v > 0 for v in counts.values()), counts
    assert counts["submit"] > counts["execute"]


def test_validate_rejects_crossing_submit():
    lines = ["34200.0,1,1,100,1000000,-1", "34200.1,1,2,100,1000000,1"]
    with pytest.raises(ValueError, match="crosses"):
        g.validate(lines)


def test_validate_rejects_execution_not_at_head():
    lines = [
        "34200.0,1,1,100,1000000,-1",
        "34200.1,1,2,100,1000000,-1",
        "34200.2,4,2,10,1000000,-1",
    ]
    with pytest.raises(ValueError, match="head"):
        g.validate(lines)


def test_main_writes_file(tmp_path):
    out = tmp_path / "m.csv"
    assert g.main(["--messages", "100", "--seed", "2", "--out", str(out)]) == 0
    assert len(out.read_text().splitlines()) == 100


def test_write_to_stream():
    buf = io.StringIO()
    assert g.write(["a", "b"], buf) == 2
    assert buf.getvalue() == "a\nb\n"
