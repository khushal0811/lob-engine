# ---------------------------------------------------------------------------
# New subsystems: events, serialization, gateway
# ---------------------------------------------------------------------------
add_subdirectory(events)
add_subdirectory(serialization)
add_subdirectory(gateway)

# ---------------------------------------------------------------------------
# Exchange binary
# ---------------------------------------------------------------------------
add_executable(lob_exchange exchange_main.cpp)
target_include_directories(lob_exchange PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(lob_exchange PRIVATE lob_gateway ${ZMQ_LINK_LIBRARIES})
target_include_directories(lob_exchange PRIVATE ${ZMQ_INCLUDE_DIRS})

# ---------------------------------------------------------------------------
# Core engine library (unchanged)
# ---------------------------------------------------------------------------
add_library(lob_core STATIC
    core/core.cpp
    core/enums.hpp
    core/order.hpp
    core/trade.hpp
    core/event.hpp
    core/config.hpp
    core/spsc_queue.hpp
    book/price_level.cpp
    book/price_level.hpp
    book/order_book.cpp
    book/order_book.hpp
    engine/matching_engine.cpp
    engine/matching_engine.hpp
    feed/csv_replay.cpp
    feed/csv_replay.hpp
    feed/synthetic_gen.cpp
    feed/synthetic_gen.hpp
    logging/logger.cpp
    logging/logger.hpp
    metrics/histogram.hpp
    metrics/metrics.hpp
    snapshot/snapshot.cpp
    snapshot/snapshot.hpp
)

target_include_directories(lob_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
