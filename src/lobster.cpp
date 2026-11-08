#include "tickmatch/lobster.hpp"

#include <array>
#include <charconv>
#include <limits>

namespace tickmatch::lobster {

namespace {

template <typename T>
bool parse_int(std::string_view s, T& out) noexcept {
    if (s.empty()) return false;
    const char* first = s.data();
    if (*first == '+') ++first;
    const auto [ptr, ec] = std::from_chars(first, s.data() + s.size(), out);
    return ec == std::errc{} && ptr == s.data() + s.size();
}

bool parse_double(std::string_view s, double& out) noexcept {
    if (s.empty()) return false;
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
    return ec == std::errc{} && ptr == s.data() + s.size();
}

}  // namespace

std::optional<Message> parse_line(std::string_view line) noexcept {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.remove_suffix(1);
    std::array<std::string_view, 6> f;
    std::size_t n = 0;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= line.size(); ++i) {
        if (i == line.size() || line[i] == ',') {
            if (n == f.size()) return std::nullopt;  // too many fields
            f[n++] = line.substr(start, i - start);
            start = i + 1;
        }
    }
    if (n != f.size()) return std::nullopt;

    Message m;
    int type = 0;
    std::int64_t size = 0;
    int dir = 0;
    if (!parse_double(f[0], m.time) || !parse_int(f[1], type) || !parse_int(f[2], m.order_id) ||
        !parse_int(f[3], size) || !parse_int(f[4], m.price) || !parse_int(f[5], dir)) {
        return std::nullopt;
    }
    if (type < 1 || type > 7) return std::nullopt;
    if (dir != 1 && dir != -1) return std::nullopt;
    if (size < 0 || size > std::numeric_limits<Qty>::max()) return std::nullopt;
    m.type = static_cast<EventType>(type);
    m.size = static_cast<Qty>(size);
    m.side = dir == 1 ? Side::Buy : Side::Sell;
    return m;
}

std::optional<OrderRequest> to_request(const Message& m, OrderId& next_synthetic_id) noexcept {
    switch (m.type) {
        case EventType::Submit:
            return OrderRequest::limit(m.order_id, m.side, m.price, m.size);
        case EventType::PartialCancel:
            return OrderRequest::reduce(m.order_id, m.size);
        case EventType::Delete:
            return OrderRequest::cancel(m.order_id);
        case EventType::ExecuteVisible:
            return OrderRequest::market(next_synthetic_id++, opposite(m.side), m.size);
        case EventType::ExecuteHidden:
        case EventType::Cross:
        case EventType::Halt:
            return std::nullopt;
    }
    return std::nullopt;
}

}  // namespace tickmatch::lobster
