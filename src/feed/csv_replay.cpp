#include "feed/csv_replay.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace lob {

CsvReplayReader::CsvReplayReader(const std::string& path) : file_(path) {
    if (!file_.is_open()) {
        throw std::runtime_error("CsvReplayReader: cannot open " + path);
    }
    // Skip header line
    std::string header;
    if (std::getline(file_, header)) {
        ++line_number_;
    }
}

std::vector<std::string> CsvReplayReader::split(const std::string& line, char delim) {
    std::vector<std::string> tokens;
    std::istringstream stream(line);
    std::string token;
    while (std::getline(stream, token, delim)) {
        tokens.push_back(token);
    }
    return tokens;
}

Side CsvReplayReader::parse_side(const std::string& s) {
    if (s == "BUY")
        return Side::Buy;
    if (s == "SELL")
        return Side::Sell;
    throw std::runtime_error("Invalid side: " + s);
}

OrderType CsvReplayReader::parse_order_type(const std::string& s) {
    if (s == "LIMIT")
        return OrderType::Limit;
    if (s == "MARKET")
        return OrderType::Market;
    if (s == "STOP")
        return OrderType::Stop;
    if (s == "STOPLIMIT")
        return OrderType::StopLimit;
    if (s == "ICEBERG")
        return OrderType::Iceberg;
    throw std::runtime_error("Invalid order type: " + s);
}

bool CsvReplayReader::next(OrderEvent& out) {
    std::string line;
    while (std::getline(file_, line)) {
        ++line_number_;
        if (line.empty())
            continue;

        try {
            auto fields = split(line, ',');
            if (fields.empty())
                continue;

            const auto& event_type = fields[0];

            if (event_type == "NEW") {
                if (fields.size() < 9)
                    throw std::runtime_error("Not enough fields for NEW");
                Order o;
                o.id = std::stoull(fields[1]);
                o.side = parse_side(fields[2]);
                o.type = parse_order_type(fields[3]);
                o.price = std::stoll(fields[4]);
                o.quantity = std::stoull(fields[5]);
                o.orig_qty = o.quantity;
                o.peak_qty = std::stoull(fields[6]);
                o.stop_price = std::stoll(fields[7]);
                o.timestamp = std::stoull(fields[8]);
                o.status = OrderStatus::New;
                out = NewOrderEvent{o};
                return true;
            } else if (event_type == "CANCEL") {
                if (fields.size() < 9)
                    throw std::runtime_error("Not enough fields for CANCEL");
                CancelOrderEvent e;
                e.order_id = std::stoull(fields[1]);
                e.timestamp = std::stoull(fields[8]);
                out = e;
                return true;
            } else if (event_type == "MODIFY") {
                if (fields.size() < 9)
                    throw std::runtime_error("Not enough fields for MODIFY");
                ModifyOrderEvent e;
                e.order_id = std::stoull(fields[1]);
                e.new_price = fields[4].empty() ? 0 : std::stoll(fields[4]);
                e.new_quantity = fields[5].empty() ? 0 : std::stoull(fields[5]);
                e.timestamp = std::stoull(fields[8]);
                out = e;
                return true;
            } else {
                throw std::runtime_error("Unknown event type: " + event_type);
            }
        } catch (const std::exception& ex) {
            ++error_count_;
            std::cerr << "CsvReplayReader: line " << line_number_ << ": " << ex.what()
                      << " — skipping\n";
        }
    }
    return false;
}

} // namespace lob
