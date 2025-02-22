# Set the ADAPTIVEPROXY_SOURCES variable to the list of all source files in the current directory
set(
    ADAPTIVEPROXY_SOURCES
    adaptiveProxy.cpp
    http.cpp
)

# Tell CMake to create an executable named 'adaptiveProxy' from the source files
add_executable(adaptiveProxy ${ADAPTIVEPROXY_SOURCES})

# Ensure that the cxxopts and common libraries are linked to the adaptiveProxy executable
target_link_libraries(adaptiveProxy PRIVATE cxxopts::cxxopts common spdlog::spdlog pugixml::pugixml Boost::regex)

# Include the common directory for headers (e.g. loadBalancer_protocol.h)
target_include_directories(adaptiveProxy PRIVATE ${PROJECT_SOURCE_DIR}/common)
