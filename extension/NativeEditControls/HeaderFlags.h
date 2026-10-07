#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace cmakemodel { class CompileSettingsIndex; }
namespace cpptools { class ProjectIndex; }

namespace CodeToolsVsix
{
    // A header no CMake target lists, and no target's include path reaches, is parsed with no include folders
    // and no definitions, so its own #includes fail. A header is compiled as part of whatever includes it, so
    // this gives each such header the settings of the nearest file that includes it (directly or through other
    // headers) and that a target does compile - "nearest" by include distance, then by shared folders. It reads
    // the include edges the index already has and records the choice in `settings` (CompileSettingsIndex::borrow),
    // so the index's own parses and the editors agree. Returns how many headers were given settings; `borrowed`,
    // when given, receives their paths.
    std::size_t borrowHeaderSettings(const cpptools::ProjectIndex& index, cmakemodel::CompileSettingsIndex& settings,
                                     std::vector<std::string>* borrowed = nullptr);
}
