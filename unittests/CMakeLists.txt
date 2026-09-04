find_package(Threads REQUIRED)

add_executable(cpptools_tests
    test_parser.cpp
)

target_link_libraries(cpptools_tests PRIVATE cpptools GTest::gtest_main Threads::Threads)

cpptools_copy_libclang_dll(cpptools_tests)

include(GoogleTest)
gtest_discover_tests(cpptools_tests)

# PropertyEditor (extension/NativeEditControls/) needs real newui::reflection
# types - a separate executable from cpptools_tests (which doesn't link
# newui at all) rather than adding that dependency there.
add_executable(nativeeditcontrols_tests
    test_property_editor.cpp
    test_component_editor.cpp
    "${CMAKE_SOURCE_DIR}/extension/NativeEditControls/PropertyEditor.cpp"
    "${CMAKE_SOURCE_DIR}/extension/NativeEditControls/ComponentEditor.cpp"
)

target_link_libraries(nativeeditcontrols_tests PRIVATE cpptools newui GTest::gtest_main Threads::Threads)

cpptools_copy_libclang_dll(nativeeditcontrols_tests)
