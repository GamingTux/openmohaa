# GNU style (GCC/Clang) compiler specific settings

if(NOT CMAKE_C_COMPILER_ID STREQUAL "GNU" AND NOT CMAKE_C_COMPILER_ID MATCHES "^(Apple)?Clang$")
    return()
endif()

enable_language(ASM)

set(ASM_SOURCES
    ${SOURCE_DIR}/asm/ftola.c
    ${SOURCE_DIR}/asm/snapvector.c
)

add_compile_options(-Wall -Wimplicit -Wshadow
    -Wstrict-prototypes -Wformat=2  -Wformat-security
    -Wstrict-aliasing=2 -Wmissing-format-attribute
    -Wdisabled-optimization -Werror-implicit-function-declaration)

add_compile_options(-Wno-format-zero-length -Wno-format-nonliteral)

# There are lots of instances of union based aliasing in the code
# that rely on the compiler not optimising them away, so disable it
add_compile_options(-fno-strict-aliasing)

# Ensure consistent signed char semantics across x86 and ARM/AArch64
add_compile_options(-fsigned-char)

if(OPTIMIZE_PI5)
    add_compile_options(-mcpu=cortex-a76+crypto)
elseif(OPTIMIZE_NATIVE AND NOT APPLE)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|arm)")
        add_compile_options(-mcpu=native)
    else()
        add_compile_options(-march=native)
    endif()
endif()

# Release / RelWithDebInfo runtime performance flags (GCC, Clang, AppleClang)
set(_GNU_OPT_COND "$<AND:$<OR:$<COMPILE_LANGUAGE:C>,$<COMPILE_LANGUAGE:CXX>>,$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>>")
set(_GNU_CXX_OPT_COND "$<AND:$<COMPILE_LANGUAGE:CXX>,$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>>")
set(_GNU_REL_COND "$<AND:$<OR:$<COMPILE_LANGUAGE:C>,$<COMPILE_LANGUAGE:CXX>>,$<CONFIG:Release>>")
set(_GNU_OPT_LINK_COND "$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>")

add_compile_options(
    "$<${_GNU_OPT_COND}:-O3;-fno-math-errno;-fno-trapping-math;-ffp-contract=fast;-ffunction-sections;-fdata-sections>"
    "$<${_GNU_CXX_OPT_COND}:-fno-rtti;-fvisibility-inlines-hidden>"
)

if(UNIX AND NOT APPLE)
    # Unlock cross-TU LTO inlining and direct calls inside shared libraries (.so)
    add_compile_options(
        "$<${_GNU_OPT_COND}:-fno-semantic-interposition;-fomit-frame-pointer>"
        # Hide non-Q_EXPORT symbols in Release for dead-symbol stripping & IPA;
        # keep RelWithDebInfo .dynsym intact for glibc backtrace_symbols().
        "$<${_GNU_REL_COND}:-fvisibility=hidden>"
    )
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        add_compile_options("$<${_GNU_OPT_COND}:-fno-plt>")
    else()
        # Clang 14 supports -fno-plt on x86_64; avoid unused-arg warnings on older arm64 Clang
        include(utils/arch)
        if(ARCH MATCHES "x86_64")
            add_compile_options("$<${_GNU_OPT_COND}:-fno-plt>")
        endif()
    endif()
    add_link_options(
        "$<${_GNU_OPT_LINK_COND}:-Wl,-Bsymbolic-functions;-Wl,--gc-sections;-Wl,-O1>"
    )
endif()

if(CMAKE_C_COMPILER_ID STREQUAL "GNU" AND IPO_SUPPORTED)
    # Whole-program single-partition LTO (slower link, maximum cross-file inlining)
    add_link_options("$<${_GNU_OPT_LINK_COND}:-flto-partition=one>")
endif()

# This is necessary to hide all symbols unless explicitly exported
# via the Q_EXPORT macro
#add_compile_options(-fvisibility=hidden)

add_compile_options(-Wno-comment)
# Treat no return type as error
add_compile_options(-Werror=return-type)

set(ASM_SOURCES)

execute_process(COMMAND ${CMAKE_C_COMPILER} -v
                OUTPUT_VARIABLE COMPILER_OUTPUT
                ERROR_VARIABLE COMPILER_OUTPUT
                OUTPUT_STRIP_TRAILING_WHITESPACE)

list(APPEND CLIENT_LIBRARIES m)
list(APPEND SERVER_LIBRARIES m)

if(COMPILER_OUTPUT MATCHES "musl|freebsd|openbsd|netbsd")
    list(APPEND CLIENT_LIBRARIES execinfo)
    list(APPEND SERVER_LIBRARIES execinfo)
elseif (NOT APPLE)
    # For when using GLIBC versions older than 2.34
    list(APPEND CLIENT_LIBRARIES rt)
    list(APPEND SERVER_LIBRARIES rt)
endif()