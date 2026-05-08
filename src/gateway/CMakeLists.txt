add_library(lob_gateway STATIC
    exchange_manager.cpp
    exchange_manager.hpp
    event_dispatcher.cpp
    event_dispatcher.hpp
    replay_logger.cpp
    replay_logger.hpp
    gateway.cpp
    gateway.hpp
)

target_include_directories(lob_gateway PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_include_directories(lob_gateway PRIVATE ${ZMQ_INCLUDE_DIRS})

target_link_libraries(lob_gateway
    PUBLIC
        lob_core
        lob_events
        lob_serialization
)

# Use full library path to ensure linker finds ZMQ regardless of rpath
target_link_libraries(lob_gateway PRIVATE ${ZMQ_LINK_LIBRARIES})
