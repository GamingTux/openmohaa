# MSVC compiler specific settings

if(NOT CMAKE_C_COMPILER_ID STREQUAL "MSVC")
    return()
endif()

include(utils/arch)

#enable_language(ASM_MASM)

set(ASM_SOURCES
    ${SOURCE_DIR}/asm/snapvector.asm
    ${SOURCE_DIR}/asm/ftola.asm
)

if(ARCH MATCHES "x86_64")
    list(APPEND ASM_SOURCES ${SOURCE_DIR}/asm/vm_x86_64.asm)
    set_source_files_properties(
        ${ASM_SOURCES}
        PROPERTIES COMPILE_DEFINITIONS "idx64")
endif()

# Baseline warnings
add_compile_options("$<$<COMPILE_LANGUAGE:C>:/W4>")

# C4267: 'var' : conversion from 'size_t' to 'type', possible loss of data
# There are way too many of these to realistically deal with them
add_compile_options("$<$<COMPILE_LANGUAGE:C>:/wd4267>")

# C4206: nonstandard extension used: translation unit is empty
add_compile_options("$<$<COMPILE_LANGUAGE:C>:/wd4206>")

# C4324: 'struct': structure was padded due to alignment specifier
add_compile_options("$<$<COMPILE_LANGUAGE:C>:/wd4324>")

# C4200: nonstandard extension used: zero-sized array in struct/union
add_compile_options("$<$<COMPILE_LANGUAGE:C>:/wd4200>")

# MSVC doesn't understand __inline__, which libjpeg uses
add_compile_definitions(__inline__=inline)

# It's unlikely that we'll move to the _s variants, so stop the warning
add_compile_definitions(_CRT_SECURE_NO_WARNINGS)

# The sockets platform abstraction layer necessarily uses deprecated APIs
add_compile_definitions(_WINSOCK_DEPRECATED_NO_WARNINGS)

#
# Microsoft compiler specific parameters
#
add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
add_compile_definitions(_CRT_NONSTDC_NO_DEPRECATE)

# Treat no return type as error
add_compile_options(/we4715)

# Release / RelWithDebInfo runtime performance flags (Windows x64 & ARM64)
set(_MSVC_OPT_COND "$<AND:$<OR:$<COMPILE_LANGUAGE:C>,$<COMPILE_LANGUAGE:CXX>>,$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>>")
set(_MSVC_CXX_OPT_COND "$<AND:$<COMPILE_LANGUAGE:CXX>,$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>>")
set(_MSVC_OPT_LINK_COND "$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>")

add_compile_options(
    "$<${_MSVC_OPT_COND}:/O2;/Ob3;/Oi;/Ot;/Gy;/Gw;/fp:contract>"
    "$<${_MSVC_CXX_OPT_COND}:/GR->"
)
if(ARCH MATCHES "arm64")
    # Inline ARMv8.1 LSE atomics & RDMA on Windows ARM64
    add_compile_options("$<${_MSVC_OPT_COND}:/arch:armv8.1>")
endif()
add_link_options(
    "$<${_MSVC_OPT_LINK_COND}:/INCREMENTAL:NO;/OPT:REF;/OPT:NOICF>"
)
