# Apple Clang compiler specific settings

if(NOT CMAKE_C_COMPILER_ID STREQUAL "AppleClang")
    return()
endif()

set(CMAKE_INSTALL_RPATH "@executable_path")
set(CMAKE_BUILD_WITH_INSTALL_RPATH ON)

if(CMAKE_C_COMPILER_ID MATCHES "Clang" OR CMAKE_C_COMPILER_ID STREQUAL "GNU")
	# Set the visibility to hidden on macOS to prevent shared libraries from
	# using functions in the executable
	# it's ok to hide them because the backtrace on macOS will still print the name of the functions
	add_compile_options(-fvisibility=hidden)
	# Per-architecture tuning for universal arm64;x86_64 binaries (macOS >= 10.15 requires Ivy Bridge+ / M1+)
	add_compile_options(
		"SHELL:-Xarch_arm64 -mcpu=apple-m1"
		"SHELL:-Xarch_x86_64 -march=ivybridge"
	)
	if(IPO_SUPPORTED)
		set(CMAKE_C_COMPILE_OPTIONS_IPO "-flto=full")
		set(CMAKE_CXX_COMPILE_OPTIONS_IPO "-flto=full" "-fwhole-program-vtables")
		set(CMAKE_OBJC_COMPILE_OPTIONS_IPO "-flto=full")
	endif()
	add_link_options("$<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:-Wl,-dead_strip>")
endif()
