#pragma once

// Reads CMake's File API (https://cmake.org/cmake/help/latest/manual/cmake-file-api.7.html): the JSON
// CMake writes under <build>/.cmake/api/v1/reply after a configure, for the queries asked of it under
// .../query. It works with every generator, including Visual Studio's, which writes no
// compile_commands.json.

#include <string>

#include "model.h"

namespace cmakemodel
{
    // Asks CMake to write the codemodel on its next configure: creates the query file. False (with
    // `error`) if the build directory doesn't exist or the file can't be written.
    bool requestQueries(const std::string& buildDir, std::string* error = nullptr);

    struct LoadResult
    {
        Model model;
        std::string error;   // empty on success
        int skipped = 0;     // targets whose reply file was missing or unreadable
        std::string cmakeExe;   // the cmake that wrote the reply, to configure again with
        bool ok() const { return error.empty(); }
    };

    // Reads the newest reply under buildDir for one configuration. `configuration` may be empty, or
    // not found when the generator has just one: then that one is used. An error says what is missing
    // ("no File API reply: configure the project once after requestQueries()").
    LoadResult loadFileApi(const std::string& buildDir, const std::string& configuration);
}
