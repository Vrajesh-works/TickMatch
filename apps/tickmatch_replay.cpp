// tickmatch_replay: replay a LOBSTER message file through the TickMatch book
// and report fills, final book state and p50/p99 latency.

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>

#include "tickmatch/replay.hpp"

namespace {

void usage(std::ostream& os) {
    os << "usage: tickmatch_replay --input FILE [options]\n"
          "  --input FILE          LOBSTER message file (Time,Type,OrderID,Size,Price,Direction)\n"
          "  --mode MODE           pipeline (default, ingest thread + SPSC queue) or inline\n"
          "  --capacity N          max resting orders (default 1048576)\n"
          "  --queue N             SPSC queue capacity (default 65536)\n"
          "  --check-every N       run full book invariant checks every N requests\n"
          "  --rate N              pipeline: pace ingest to N msgs/s (default unpaced)\n"
          "  --hist-csv FILE       write the in-book latency histogram as CSV\n"
          "  --summary-json FILE   write a JSON summary\n"
          "  --quiet               only print errors\n";
}

bool parse_size(std::string_view s, std::size_t& out) {
    try {
        std::size_t idx = 0;
        out = std::stoull(std::string(s), &idx);
        return idx == s.size();
    } catch (...) {
        return false;
    }
}

std::string fmt_price(const std::optional<tickmatch::Price>& p) {
    if (!p) return "-";
    std::ostringstream os;
    os << static_cast<double>(*p) / 10000.0;
    return os.str();
}

}  // namespace

int main(int argc, char** argv) {
    std::string input;
    std::string hist_csv;
    std::string summary_json;
    bool quiet = false;
    tickmatch::ReplayOptions opt;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        auto value = [&]() -> std::string_view {
            if (i + 1 >= argc) {
                std::cerr << "missing value for " << arg << "\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if (arg == "--input") {
            input = value();
        } else if (arg == "--mode") {
            const auto m = value();
            if (m == "inline") {
                opt.mode = tickmatch::ReplayMode::Inline;
            } else if (m == "pipeline") {
                opt.mode = tickmatch::ReplayMode::Pipeline;
            } else {
                std::cerr << "unknown mode " << m << "\n";
                return 2;
            }
        } else if (arg == "--capacity" || arg == "--queue" || arg == "--check-every" ||
                   arg == "--rate") {
            std::size_t n = 0;
            if (!parse_size(value(), n)) {
                std::cerr << "bad number for " << arg << "\n";
                return 2;
            }
            if (arg == "--capacity") opt.book_capacity = n;
            if (arg == "--queue") opt.queue_capacity = n;
            if (arg == "--check-every") opt.check_every = n;
            if (arg == "--rate") opt.rate = n;
        } else if (arg == "--hist-csv") {
            hist_csv = value();
        } else if (arg == "--summary-json") {
            summary_json = value();
        } else if (arg == "--quiet") {
            quiet = true;
        } else if (arg == "--help" || arg == "-h") {
            usage(std::cout);
            return 0;
        } else {
            std::cerr << "unknown argument " << arg << "\n";
            usage(std::cerr);
            return 2;
        }
    }
    if (input.empty()) {
        usage(std::cerr);
        return 2;
    }

    std::ifstream in(input, std::ios::binary);
    if (!in) {
        std::cerr << "cannot open " << input << "\n";
        return 1;
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    const std::string text = buf.str();

    const auto stats =
        std::make_unique<tickmatch::ReplayStats>(tickmatch::replay_lobster(text, opt));
    const auto& s = *stats;

    if (!hist_csv.empty()) {
        std::ofstream out(hist_csv);
        s.book_latency.write_csv(out);
    }
    if (!summary_json.empty()) {
        std::ofstream out(summary_json);
        out << tickmatch::to_json(s, opt.mode) << "\n";
    }

    if (!quiet) {
        std::cout << "TickMatch replay (" << tickmatch::to_string(opt.mode) << ") of " << input
                  << "\n"
                  << "  lines " << s.lines << ", malformed " << s.malformed << ", skipped "
                  << s.skipped << "\n"
                  << "  requests " << s.requests << " (limit " << s.limits << ", market "
                  << s.markets << ", cancel " << s.cancels << ", reduce " << s.reduces << ")\n"
                  << "  fills " << s.fills << ", volume " << s.volume << ", executions matched "
                  << s.executions_matched << "/" << s.executions << "\n"
                  << "  rejects: unknown id " << s.reject_unknown_id << ", duplicate id "
                  << s.reject_duplicate_id << ", invalid " << s.reject_invalid << "\n"
                  << "  final book: " << s.resting_orders << " resting, best bid "
                  << fmt_price(s.best_bid) << ", best ask " << fmt_price(s.best_ask) << "\n"
                  << "  throughput " << static_cast<std::uint64_t>(s.throughput()) << " msg/s over "
                  << s.wall_seconds << " s\n"
                  << "  book latency: " << s.book_latency.summary() << "\n";
        if (s.e2e_latency.count() > 0) {
            std::cout << "  e2e latency:  " << s.e2e_latency.summary() << "\n";
        }
        std::cout << "  timer overhead ~" << s.timer_overhead_ns << " ns per sample (included)\n"
                  << "  invariants: " << (s.invariants_ok ? "ok" : s.invariant_error) << "\n";
    }
    if (!s.invariants_ok) {
        std::cerr << "book invariant violated: " << s.invariant_error << "\n";
        return 3;
    }
    return 0;
}
