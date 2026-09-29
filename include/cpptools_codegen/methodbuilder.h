#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace cpptools_codegen {

// One parameter in a generated method's argument list. declarator, when non-empty, is used
// verbatim in place of "type name[ = defaultValue]" - needed for a parameter whose name sits
// *inside* its own type rather than after it (a pointer-to-member-function parameter, e.g.
// "SyncReturn (T::*method)(SenderRefT, Args...)" - see MethodBuilder's own test for a real one).
struct Argument {
    std::string type;
    std::string name;
    std::string defaultValue;
    std::string declarator;

    static Argument raw(std::string declarator);

    std::string toString() const;
};

// A fluent builder for one method/function's text - the single source of truth for C++'s own
// rigid modifier ordering (template -> static/virtual/inline -> return type -> name(args) ->
// const/noexcept/override -> body), so a generation call site's own property-setting order never
// affects the output. See bluesky/cpp-codegen-plan.md for why this exists instead of hand-rolled
// string concatenation at each call site, and why it's a plain object model rather than an
// external template engine.
class MethodBuilder {
public:
    explicit MethodBuilder(std::string name);

    MethodBuilder& returnType(std::string type);
    MethodBuilder& addTemplateParam(std::string param);
    MethodBuilder& addArgument(Argument argument);
    MethodBuilder& addArgument(std::string type, std::string name, std::string defaultValue = "");
    MethodBuilder& addBodyLine(std::string line);

    MethodBuilder& makeConst(bool value = true);
    MethodBuilder& makeNoexcept(bool value = true);
    MethodBuilder& makeVirtual(bool value = true);
    MethodBuilder& makeOverride(bool value = true);
    MethodBuilder& makeStatic(bool value = true);
    MethodBuilder& makeInline(bool value = true);

    const std::string& name() const { return name_; }

    // indentLevel counts 4-space steps for the signature itself; the body is indented one step
    // deeper. A caller embedding this inside a ClassBuilder never sets this directly - see that
    // class's own toString().
    std::string toString(std::size_t indentLevel = 0) const;

private:
    std::string name_;
    std::string returnType_ = "void";
    std::vector<std::string> templateParams_;
    std::vector<Argument> arguments_;
    std::vector<std::string> bodyLines_;

    bool isConst_ = false;
    bool isNoexcept_ = false;
    bool isVirtual_ = false;
    bool isOverride_ = false;
    bool isStatic_ = false;
    bool isInline_ = false;
};

} // namespace cpptools_codegen
