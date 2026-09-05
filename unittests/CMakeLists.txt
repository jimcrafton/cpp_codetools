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
#
# test_designer_editor.cpp additionally pulls in NativeEditor.h (via
# DesignerEditor.h), which needs the VS SDK headers (vsshell.h/wil/com.h) -
# same VSEXT_INCLUDES/compile definitions/link libs testharness's own
# NativeEditor.cpp/DesignerEditor.cpp/TextEncoding.cpp/Logging.cpp compile
# uses (root CMakeLists.txt), not just cpptools+newui like the plain
# PropertyEditor/ComponentEditor tests need.
add_executable(nativeeditcontrols_tests
    test_property_editor.cpp
    test_component_editor.cpp
    test_designer_editor.cpp
    test_workspace.cpp
    test_toolbox_registry.cpp
    test_toolbox.cpp
    "${CMAKE_SOURCE_DIR}/extension/NativeEditControls/PropertyEditor.cpp"
    "${CMAKE_SOURCE_DIR}/extension/NativeEditControls/ComponentEditor.cpp"
    "${CMAKE_SOURCE_DIR}/extension/NativeEditControls/ToolboxRegistry.cpp"
    "${CMAKE_SOURCE_DIR}/extension/NativeEditControls/Toolbox.cpp"
    "${CMAKE_SOURCE_DIR}/extension/NativeEditControls/DesignerEditor.cpp"
    "${CMAKE_SOURCE_DIR}/extension/NativeEditControls/CppEditor.cpp"
    "${CMAKE_SOURCE_DIR}/extension/NativeEditControls/NativeEditor.cpp"
    "${CMAKE_SOURCE_DIR}/extension/NativeEditControls/TextEncoding.cpp"
    "${CMAKE_SOURCE_DIR}/extension/NativeEditControls/Logging.cpp"
    "${CMAKE_SOURCE_DIR}/extension/NativeEditControls/Workspace.cpp"
)

target_compile_definitions(nativeeditcontrols_tests PRIVATE
    WIN32_LEAN_AND_MEAN
    UNICODE
    _UNICODE
)

target_include_directories(nativeeditcontrols_tests PRIVATE "${CMAKE_SOURCE_DIR}/include" ${VSEXT_INCLUDES})

target_link_libraries(nativeeditcontrols_tests PRIVATE cpptools newui user32.lib gdi32.lib comdlg32.lib GTest::gtest_main Threads::Threads)

cpptools_copy_libclang_dll(nativeeditcontrols_tests)
