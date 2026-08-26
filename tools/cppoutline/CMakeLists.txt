add_executable(cppoutline main.cpp)

target_link_libraries(cppoutline PRIVATE cpptools)

cpptools_copy_libclang_dll(cppoutline)
