find_package(Threads REQUIRED)

# cmakemodel_tests - the File API reader, against a small hand-written reply and (when it has one) this
# repo's own build tree. A separate executable: it needs neither libclang nor newui.
add_executable(cmakemodel_tests test_cmakemodel.cpp)
target_link_libraries(cmakemodel_tests PRIVATE cmakemodel GTest::gtest_main)
target_compile_definitions(cmakemodel_tests PRIVATE
    CMAKEMODEL_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/cmake_fileapi"
    CMAKEMODEL_REPO_BUILD_DIR="${CMAKE_BINARY_DIR}")

add_executable(cpptools_tests
    test_parser.cpp
    test_compileflags.cpp
    test_projectindex.cpp
    test_includeanalysis.cpp
)

target_compile_definitions(cpptools_tests PRIVATE CPPTOOLS_TEST_REPO_ROOT="${CMAKE_SOURCE_DIR}")

target_link_libraries(cpptools_tests PRIVATE cpptools GTest::gtest_main Threads::Threads)

cpptools_copy_libclang_dll(cpptools_tests)

include(GoogleTest)
gtest_discover_tests(cpptools_tests)

# cpptools_codegen's own tests - a separate executable, not folded into cpptools_tests above,
# since it links Clang's much heavier LibTooling/AST/ASTMatchers libraries (see the root
# CMakeLists.txt's own "Clang LibTooling - Dependency" section) that cpptools_tests has no need
# of and that meaningfully slow down its link. Also links plain cpptools (libclang) - the
# ClassBuilder round-trip test feeds generated text back through cpptools::Parser to prove it
# actually parses, per bluesky/cpp-codegen-plan.md's own "Testing approach" section, not just
# trusting the builder's own string-building logic.
add_executable(cpptools_codegen_tests
    test_codegen.cpp
    test_methodbuilder.cpp
    test_classbuilder.cpp
    test_methodinsertion.cpp
    test_accessmerge.cpp
    test_delegatewiring.cpp
    test_fieldinsertion.cpp
    test_controllerwiring.cpp
    test_controllerwiring_newui.cpp
    test_delegatebindings.cpp
)

target_link_libraries(cpptools_codegen_tests PRIVATE cpptools_codegen cpptools GTest::gtest_main Threads::Threads)

# test_controllerwiring_newui.cpp reparses generated controllers against the REAL newui headers
# (libclang with these -I paths), '|'-separated so one definition carries them all.
target_compile_definitions(cpptools_codegen_tests PRIVATE
    CODEGEN_TEST_NEWUI_INCLUDES="${newui_SOURCE_DIR}/include|${newui_BINARY_DIR}/include|${newui_SOURCE_DIR}/3rdparty/json5/include|${newui_SOURCE_DIR}/3rdparty/blend2d")
cpptools_copy_libclang_dll(cpptools_codegen_tests)

# cpptools_analysis's tests: macros and templates, on in-memory files.
add_executable(cpptools_analysis_tests
    test_macroanalysis.cpp
    test_templateanalysis.cpp
)
target_link_libraries(cpptools_analysis_tests PRIVATE cpptools_analysis GTest::gtest_main Threads::Threads)

# The lex library's checks (from lexer-base) - their CHECK macro reports through GoogleTest, see
# lex/test_util.h.
add_executable(lex_tests
    lex/lex_tests.cpp
    lex/lexer_base_tests.cpp
    lex/json5_lexer_tests.cpp
    lex/cpp_lexer_tests.cpp
    lex/json5_parser_tests.cpp
    lex/json5_diff_tests.cpp
    lex/json5_presentation_tests.cpp
    lex/highlight_tests.cpp
    lex/cmake_lexer_tests.cpp
    lex/cmake_parser_tests.cpp)
target_link_libraries(lex_tests PRIVATE lex GTest::gtest_main Threads::Threads)
# The CMake tests read this repo's own CMakeLists.txt.
target_compile_definitions(lex_tests PRIVATE CMAKE_LEXER_TEST_ROOT="${CMAKE_SOURCE_DIR}")
if(MSVC)
    target_compile_options(lex_tests PRIVATE /utf-8)
endif()

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
    test_gradient_editor_dialog.cpp
    test_color_editor_dialog.cpp
    test_color_picker.cpp
    test_component_editor.cpp
    test_designer_clipboard.cpp
    test_designer_arrange.cpp
    test_designer_editor.cpp
    test_document_tabs.cpp
    test_delegate_chips.cpp
    test_issue_peek.cpp
    test_include_graph_layout.cpp
    test_font_catalog.cpp
    test_fonts_model.cpp
    test_host_location_opener.cpp
    test_settings.cpp
    test_explorer_model.cpp
    test_project_explorer.cpp
    test_header_flags.cpp
    test_editor_file_kinds.cpp
    test_host_services.cpp
    test_problem_list.cpp
    test_cpp_editor.cpp
    test_find_engine.cpp
    test_document_edit_service.cpp
    test_host_editor_bridge.cpp
    test_codegen_edit_adapter.cpp
    test_wiring_request_builder.cpp
    test_controller_ref.cpp
    test_controller_bindings.cpp
    test_controller_wiring_action.cpp
    test_new_controller_dialog.cpp
    test_include_manager.cpp
    test_include_planner.cpp
    test_delegate_rows.cpp
    test_find_replace_controller.cpp
    test_workspace.cpp
    test_canvas_well.cpp
    test_toolbox_registry.cpp
    test_toolbox.cpp
    test_view_style_registry.cpp
    test_callout_placement.cpp
    test_color_choices.cpp
    test_view_designer_controller.cpp
    test_view_designer_model.cpp
    test_properties_model.cpp
    test_property_item.cpp
    test_properties_grid.cpp
    test_properties_panel.cpp
    test_document_outline.cpp
    test_selection_overlay.cpp
    test_layout_editing_policy.cpp
)

target_link_libraries(nativeeditcontrols_tests PRIVATE nativeeditcontrols_core GTest::gtest_main Threads::Threads)
# The explorer tests copy the hand-written CMake File API reply.
target_compile_definitions(nativeeditcontrols_tests PRIVATE EXPLORER_TEST_FIXTURES="${CMAKE_SOURCE_DIR}/unittests/fixtures/cmake_fileapi")

cpptools_copy_libclang_dll(nativeeditcontrols_tests)

# Same Resources/Images/icons/... this binary's own tests resolve real
# icon paths against (ToolboxRegistry/ToolboxController tests, e.g.) -
# see the root CMakeLists.txt's own newui_add_resources() calls for
# NativeEditControls/testharness for the fuller comment.
newui_add_resources(nativeeditcontrols_tests "${CMAKE_SOURCE_DIR}/extension/NativeEditControls/Resources")
