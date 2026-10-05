#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace cpptools_analysis {

struct TemplateInstantiationInfo {
    std::string type;                      // "Box<int>" for a class, "<int>" for a function's arguments
    enum class Kind { Implicit, ExplicitInstantiation, ExplicitSpecialization } kind = Kind::Implicit;
    // A class instantiated only as a name (Box<int>* p) is not complete; one whose members are used is.
    bool complete = false;
    std::string file;                      // where it is first needed (normalized, forward slashes); the template's own when unknown
    std::size_t line = 0;
};

struct TemplateInfo {
    std::string name;                      // qualified: "shapes::Box"
    enum class Kind { Class, Function } kind = Kind::Class;
    std::string file;                      // where the template is declared
    std::size_t line = 0;
    std::vector<TemplateInstantiationInfo> instantiations;   // each distinct one once, in the order met
};

struct TemplateAnalysis {
    bool ok = false;
    std::vector<TemplateInfo> templates;   // in the order first instantiated
};

// Parses `content` as the file `path` with the compile arguments `args` and lists the class and function
// templates instantiated in it. Only templates declared under `declaredUnder` are kept (a folder; empty: all, which
// includes everything in the standard library). As analyzeMacros, `content` stands in for the file on disk.
TemplateAnalysis analyzeTemplates(const std::string& content, const std::string& path, const std::vector<std::string>& args = {},
                                  const std::string& declaredUnder = std::string());

}  // namespace cpptools_analysis
