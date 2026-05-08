add_library(lob_serialization STATIC serializer.cpp)

target_include_directories(lob_serialization PUBLIC ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(lob_serialization
    PUBLIC
        lob_events
    PRIVATE
        nlohmann_json::nlohmann_json
)
