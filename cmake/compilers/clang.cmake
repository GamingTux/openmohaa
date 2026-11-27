# Clang compiler specific settings

if(NOT CMAKE_C_COMPILER_ID STREQUAL "Clang")
    return()
endif()

# Ignore warnings for code like 'assert("Assert string")'
add_compile_options(-Wno-pointer-bool-conversion)

# Prefer Full LTO over CMake's default ThinLTO for maximum cross-TU inlining
if(IPO_SUPPORTED)
    set(CMAKE_C_COMPILE_OPTIONS_IPO "-flto=full")
    set(CMAKE_CXX_COMPILE_OPTIONS_IPO "-flto=full")
    if(UNIX AND NOT APPLE)
        # Active in Release where -fvisibility=hidden + -flto=full are both enabled
        add_compile_options("$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:-fwhole-program-vtables>")
    endif()
endif()

# Enable lld-specific full-LTO & safe identical code folding when linking with lld
if(UNIX AND NOT APPLE AND CMAKE_EXE_LINKER_FLAGS MATCHES "lld")
    add_link_options("$<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:-Wl,--lto-O3;-Wl,--icf=safe>")
endif()
