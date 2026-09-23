# Examples are built as separate executables so that the library target itself
# stays header-only and dependency-free.
set(CPPSTREAM_EXAMPLES
    pipelineTour
    wordFrequency
    collectionTour
    parallelTour)

foreach(example IN LISTS CPPSTREAM_EXAMPLES)
    add_executable(cppstream_example_${example} ${example}.cpp)
    target_link_libraries(cppstream_example_${example} PRIVATE
        cppstream::cppstream
        cppstream_warnings)
    if(CPPSTREAM_ENABLE_SANITIZERS)
        cppstream_enable_sanitizers(cppstream_example_${example})
    endif()
endforeach()
