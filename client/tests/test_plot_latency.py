from tickmatch_client import plot_latency as p
from tickmatch_client import replay


def write_csv(path, rows):
    path.write_text("lower_ns,upper_ns,count\n" + "".join(f"{a},{b},{c}\n" for a, b, c in rows))


def test_percentile_matches_cpp_rule(tmp_path):
    csv_path = tmp_path / "h.csv"
    write_csv(csv_path, [(10, 10, 50), (100, 103, 49), (5000, 5100, 1)])
    buckets = p.read_histogram(csv_path)
    assert p.percentile(buckets, 50) == 10
    assert p.percentile(buckets, 51) == 103
    assert p.percentile(buckets, 99) == 103
    assert p.percentile(buckets, 100) == 5100
    assert p.percentile([], 50) == 0


def test_format_summary():
    summary = {
        "messages": 10,
        "fills": 3,
        "throughput_msgs_per_sec": 1234.5,
        "latency_ns": {"book": {"p50": 80, "p99": 400, "p999": 900}},
    }
    text = replay.format_summary(summary)
    assert "10 messages" in text and "p99=400ns" in text
