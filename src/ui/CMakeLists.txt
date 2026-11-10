cmake_minimum_required(VERSION 3.20)
project(blaise_native_dependency LANGUAGES CXX)
if(NOT EXISTS "${NATIVE_ROOT}/include/native/standard_icon.h")
    message(FATAL_ERROR
        "Blaise requires Native's shared toolbar metrics and standard icons. "
        "Use the pinned dependency (unset NATIVE_ROOT and NATIVE_REF), "
        "or update your custom Native checkout.")
endif()
# Native permits consumers to choose their own root build directory.
add_subdirectory("${NATIVE_ROOT}" native)
if(NOT TARGET native_rc)
    add_subdirectory("${NATIVE_ROOT}/tools/rc" native-rc)
endif()

# Artwork regeneration is explicit; ordinary builds use the checked-in PNGs.
add_executable(export_icons EXCLUDE_FROM_ALL ../resources/export_icons.cpp)
target_link_libraries(export_icons PRIVATE native)

# Native's public headers are external to blaise. Treat them as system
# headers in both the dependency and its consumers.
get_target_property(blaise_native_includes native INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(native SYSTEM PUBLIC ${blaise_native_includes})
