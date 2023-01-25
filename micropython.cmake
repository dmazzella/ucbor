# Create an INTERFACE library for our C module.
add_library(usermod_ucbor INTERFACE)

# Add our source files to the lib
target_sources(usermod_ucbor INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/modcbor.c
)

# Add the current directory as an include directory.
target_include_directories(usermod_ucbor INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}
)

target_compile_definitions(usermod_ucbor INTERFACE
    MICROPY_PY_UCBOR=1
    MICROPY_PY_UCBOR_CANONICAL=1
)

# Link our INTERFACE library to the usermod target.
target_link_libraries(usermod INTERFACE usermod_ucbor)
