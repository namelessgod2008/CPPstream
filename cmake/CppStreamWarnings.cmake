include_guard(GLOBAL)

# Attaches a conservative warning set to an INTERFACE target.
function(cppstream_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} INTERFACE
            /W4
            /permissive-
            /utf-8
        )
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} INTERFACE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wshadow
            -Wnon-virtual-dtor
            -Wold-style-cast
            -Wcast-align
            -Woverloaded-virtual
            -Wnull-dereference
            -Wdouble-promotion
            -Wformat=2
            -Wunused
        )
    else()
        message(WARNING
            "Unknown compiler '${CMAKE_CXX_COMPILER_ID}': no warning flags applied to '${target}'")
    endif()
endfunction()

function(cppstream_warnings_as_errors target)
    if(MSVC)
        target_compile_options(${target} INTERFACE /WX)
    else()
        target_compile_options(${target} INTERFACE -Werror)
    endif()
endfunction()

# Attaches ASan + UBSan to a concrete target (executables only). PRIVATE
# because sanitizer flags must not leak into the INTERFACE of the library.
function(cppstream_enable_sanitizers target)
    if(MSVC)
        message(WARNING "cppstream_enable_sanitizers: MSVC is not supported, skipping")
        return()
    endif()

    target_compile_options(${target} PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
        -fno-sanitize-recover=all
    )
    target_link_options(${target} PRIVATE -fsanitize=address,undefined)
endfunction()
