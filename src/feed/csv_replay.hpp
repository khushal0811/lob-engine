#pragma once
#include "core/event.hpp"
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace lob {

class CsvReplayReader {
public:
    explicit CsvReplayReader(const std::string& path);

    bool next(OrderEvent& out);

    [[nodiscard]] uint64_t line_number() const noexcept { return line_number_; }
    [[nodiscard]] uint64_t error_count() const noexcept { return error_count_; }

private:
    std::ifstream file_;
    uint64_t line_number_{0};
    uint64_t error_count_{0};

    static Side parse_side(const std::string& s);
    static OrderType parse_order_type(const std::string& s);
    static std::vector<std::string> split(const std::string& line, char delim);
};

} // namespace lob
