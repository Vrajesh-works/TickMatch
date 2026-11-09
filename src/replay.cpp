#include "tickmatch/replay.hpp"

#include <algorithm>
#include <array>
#include <memory>
#include <sstream>
#include <thread>
#include <vector>

#include "tickmatch/lobster.hpp"
#include "tickmatch/order_book.hpp"
#include "tickmatch/spsc_queue.hpp"

#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#endif

namespace tickmatch {

namespace {

inline void cpu_relax() noexcept {
#if defined(__x86_64__) || defined(__i386__)
    _mm_pause();
#else
    std::this_thread::yield();
#endif
}

// One unit of work handed from the ingest thread to the matching thread.
struct Envelope {
    OrderRequest req;
    OrderId lobster_id;  // for executions: the resting order LOBSTER says was hit
    std::uint64_t t_enqueue;
    bool is_execution;
    bool end;  // sentinel: no more input
};

struct IngestCounts {
    std::uint64_t lines{0};
    std::uint64_t malformed{0};
    std::uint64_t skipped{0};
};

// Calls `emit(Envelope)` for every line that maps to an engine request.
template <typename Emit>
IngestCounts ingest(std::string_view text, Emit&& emit) {
    IngestCounts c;
    OrderId synthetic = lobster::kSyntheticIdBase;
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t eol = text.find('\n', pos);
        if (eol == std::string_view::npos) eol = text.size();
        const std::string_view line = text.substr(pos, eol - pos);
        pos = eol + 1;
        if (line.empty() || line == "\r") continue;
        ++c.lines;
        const auto msg = lobster::parse_line(line);
        if (!msg) {
            ++c.malformed;
            continue;
        }
        const auto req = lobster::to_request(*msg, synthetic);
        if (!req) {
            ++c.skipped;
            continue;
        }
        const bool exec = msg->type == lobster::EventType::ExecuteVisible;
        emit(Envelope{*req, msg->order_id, 0, exec, false});
    }
    return c;
}

std::uint64_t measure_timer_overhead() {
    std::array<std::uint64_t, 1001> samples{};
    for (auto& s : samples) {
        const std::uint64_t a = now_ns();
        const std::uint64_t b = now_ns();
        s = b - a;
    }
    std::nth_element(samples.begin(), samples.begin() + 500, samples.end());
    return samples[500];
}

class Matcher {
  public:
    Matcher(const ReplayOptions& opt, ReplayStats& st)
        : opt_(opt), st_(st), book_(opt.book_capacity) {
        fills_.reserve(1024);
    }

    void process(const Envelope& e) {
        fills_.clear();
        const std::uint64_t t0 = now_ns();
        const Status s = book_.submit(e.req, fills_);
        const std::uint64_t t1 = now_ns();
        st_.book_latency.record(t1 - t0);
        if (e.t_enqueue != 0) st_.e2e_latency.record(t1 - e.t_enqueue);

        ++st_.requests;
        switch (e.req.type) {
            case OrderType::Limit:
                ++st_.limits;
                break;
            case OrderType::Market:
                ++st_.markets;
                break;
            case OrderType::Cancel:
                ++st_.cancels;
                break;
            case OrderType::Reduce:
                ++st_.reduces;
                break;
        }
        if (s == Status::RejectUnknownId) ++st_.reject_unknown_id;
        if (s == Status::RejectDuplicateId) ++st_.reject_duplicate_id;
        if (s == Status::RejectInvalid) ++st_.reject_invalid;

        std::uint64_t filled = 0;
        for (const Fill& f : fills_) filled += f.qty;
        st_.fills += fills_.size();
        st_.volume += filled;
        if (e.is_execution) {
            ++st_.executions;
            const bool all_named = std::all_of(fills_.begin(), fills_.end(), [&](const Fill& f) {
                return f.maker_id == e.lobster_id;
            });
            if (!fills_.empty() && all_named && filled == e.req.qty) ++st_.executions_matched;
        }
        if (opt_.check_every != 0 && st_.requests % opt_.check_every == 0 && st_.invariants_ok) {
            check();
        }
    }

    void finish() {
        if (st_.invariants_ok) check();
        st_.resting_orders = book_.order_count();
        st_.best_bid = book_.best_bid();
        st_.best_ask = book_.best_ask();
    }

  private:
    void check() {
        std::string err = book_.check_invariants();
        if (!err.empty()) {
            st_.invariants_ok = false;
            st_.invariant_error = "after request " + std::to_string(st_.requests) + ": " + err;
        }
    }

    const ReplayOptions& opt_;
    ReplayStats& st_;
    OrderBook book_;
    std::vector<Fill> fills_;
};

}  // namespace

ReplayStats replay_lobster(std::string_view text, const ReplayOptions& options) {
    ReplayStats st;
    st.timer_overhead_ns = measure_timer_overhead();
    Matcher matcher(options, st);
    IngestCounts counts;

    if (options.mode == ReplayMode::Inline) {
        std::vector<Envelope> work;
        work.reserve(text.size() / 32);
        counts = ingest(text, [&](const Envelope& e) { work.push_back(e); });
        const std::uint64_t start = now_ns();
        for (const Envelope& e : work) matcher.process(e);
        st.wall_seconds = static_cast<double>(now_ns() - start) / 1e9;
    } else {
        auto queue = std::make_unique<SpscQueue<Envelope>>(options.queue_capacity);
        const std::uint64_t start = now_ns();
        std::thread producer([&] {
            const std::uint64_t interval_ns = options.rate ? 1'000'000'000ULL / options.rate : 0;
            std::uint64_t next_due = now_ns();
            auto push = [&](Envelope e) {
                if (interval_ns != 0) {
                    while (now_ns() < next_due) cpu_relax();
                    next_due += interval_ns;
                }
                e.t_enqueue = now_ns();
                while (!queue->try_push(e)) cpu_relax();
            };
            counts = ingest(text, push);
            push(Envelope{OrderRequest{}, 0, 0, false, true});
        });
        for (;;) {
            Envelope* e = queue->front();
            if (e == nullptr) {
                cpu_relax();
                continue;
            }
            if (e->end) {
                queue->pop();
                break;
            }
            matcher.process(*e);
            queue->pop();
        }
        producer.join();
        st.wall_seconds = static_cast<double>(now_ns() - start) / 1e9;
    }

    matcher.finish();
    st.lines = counts.lines;
    st.malformed = counts.malformed;
    st.skipped = counts.skipped;
    return st;
}

std::string_view to_string(ReplayMode mode) noexcept {
    return mode == ReplayMode::Inline ? "inline" : "pipeline";
}

namespace {

void write_latency(std::ostringstream& os, const LatencyHistogram& h) {
    os << "{\"count\":" << h.count() << ",\"min\":" << h.min() << ",\"p50\":" << h.percentile(50)
       << ",\"p90\":" << h.percentile(90) << ",\"p99\":" << h.percentile(99)
       << ",\"p999\":" << h.percentile(99.9) << ",\"max\":" << h.max() << ",\"mean\":" << h.mean()
       << "}";
}

std::string json_escape(std::string_view s) {
    std::string out;
    for (char ch : s) {
        if (ch == '"' || ch == '\\') {
            out += '\\';
            out += ch;
        } else if (static_cast<unsigned char>(ch) < 0x20) {
            out += ' ';
        } else {
            out += ch;
        }
    }
    return out;
}

}  // namespace

std::string to_json(const ReplayStats& s, ReplayMode mode) {
    std::ostringstream os;
    auto opt_price = [&](const std::optional<Price>& p) {
        if (p) {
            os << *p;
        } else {
            os << "null";
        }
    };
    os << "{\"mode\":\"" << to_string(mode) << "\",\"lines\":" << s.lines
       << ",\"malformed\":" << s.malformed << ",\"skipped\":" << s.skipped
       << ",\"messages\":" << s.requests << ",\"limits\":" << s.limits
       << ",\"markets\":" << s.markets << ",\"cancels\":" << s.cancels
       << ",\"reduces\":" << s.reduces << ",\"fills\":" << s.fills << ",\"volume\":" << s.volume
       << ",\"reject_unknown_id\":" << s.reject_unknown_id
       << ",\"reject_duplicate_id\":" << s.reject_duplicate_id
       << ",\"reject_invalid\":" << s.reject_invalid << ",\"executions\":" << s.executions
       << ",\"executions_matched\":" << s.executions_matched
       << ",\"resting_orders\":" << s.resting_orders << ",\"best_bid\":";
    opt_price(s.best_bid);
    os << ",\"best_ask\":";
    opt_price(s.best_ask);
    os << ",\"invariants_ok\":" << (s.invariants_ok ? "true" : "false") << ",\"invariant_error\":\""
       << json_escape(s.invariant_error) << "\",\"wall_seconds\":" << s.wall_seconds
       << ",\"throughput_msgs_per_sec\":" << s.throughput()
       << ",\"timer_overhead_ns\":" << s.timer_overhead_ns << ",\"latency_ns\":{\"book\":";
    write_latency(os, s.book_latency);
    os << ",\"e2e\":";
    write_latency(os, s.e2e_latency);
    os << "}}";
    return os.str();
}

}  // namespace tickmatch
