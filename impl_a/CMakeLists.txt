# The same implementation source is compiled into two artifacts:
# - impl_a_static: code already present in the process and selected by Registry;
# - impl_a: an external plugin selected by HiggsIS::ClassLoader at runtime.
add_library(impl_a_static STATIC impl_a.cpp)
target_include_directories(impl_a_static PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(impl_a_static PUBLIC ${INTERFACING_HIGGS_LIBRARIES})
set_target_properties(impl_a_static PROPERTIES
    OUTPUT_NAME "impl_a_static"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/lib"
    ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/lib"
    ARCHIVE_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/lib"
    ARCHIVE_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/lib")

# plugin_version.cpp is deliberately excluded from impl_a_static so multiple
# built-ins do not bring duplicate HCL_DynamicLibVersion symbols into main.out.
add_library(impl_a SHARED impl_a.cpp plugin_version.cpp)
target_compile_definitions(impl_a PRIVATE
    INTERFACING_IMPL_A_PLUGIN_VERSION="${INTERFACING_IMPL_A_PLUGIN_VERSION}")
target_include_directories(impl_a PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(impl_a PRIVATE ${INTERFACING_HIGGS_LIBRARIES})
set_target_properties(impl_a PROPERTIES
    OUTPUT_NAME "impl_a"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/lib"
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/lib"
    LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/lib"
    LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/lib")

install(TARGETS impl_a impl_a_static
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib)
