cmake_minimum_required(VERSION 3.21)

if(WIN32)
    set(RESOURCE_FILES app.rc)
endif()

add_executable(platemaker-cli
    main.cpp
    ${RESOURCE_FILES}
)

# CLI uses vips directly (VIPS_INIT, vips_image_new_from_file, vips_error_buffer,
# vips_shutdown) and nlohmann/json directly — declare them explicitly since
# platemaker-lib exposes neither in its public API (both are PRIVATE there).
target_link_libraries(platemaker-cli
    PRIVATE
        Platemaker::platemaker
        VIPS::vips-cpp
        nlohmann_json::nlohmann_json
)
