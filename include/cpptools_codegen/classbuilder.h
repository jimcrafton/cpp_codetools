#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "cpptools_codegen/methodbuilder.h"

namespace cpptools_codegen {

// A fluent builder for one class/struct's full text - members are emitted grouped by access
// specifier (public, then protected, then private, matching this codebase's own convention - see
// e.g. newui::View/SubView/Component), in the order they were added within each group. See
// MethodBuilder for the same "one toString() enforces the language's own ordering" reasoning, one
// level up in the grammar.
class ClassBuilder {
public:
    explicit ClassBuilder(std::string name);

    ClassBuilder& makeStruct(bool value = true);
    ClassBuilder& addTemplateParam(std::string param);
    ClassBuilder& addBaseClass(std::string baseClass, std::string accessSpecifier = "public");

    ClassBuilder& addPublicMethod(MethodBuilder method);
    ClassBuilder& addProtectedMethod(MethodBuilder method);
    ClassBuilder& addPrivateMethod(MethodBuilder method);

    ClassBuilder& addPublicField(std::string declaration);
    ClassBuilder& addProtectedField(std::string declaration);
    ClassBuilder& addPrivateField(std::string declaration);

    const std::string& name() const { return name_; }

    std::string toString(std::size_t indentLevel = 0) const;

private:
    struct Members {
        std::vector<MethodBuilder> methods;
        std::vector<std::string> fields;
        bool empty() const { return methods.empty() && fields.empty(); }
    };

    std::string name_;
    bool isStruct_ = false;
    std::vector<std::string> templateParams_;
    std::vector<std::string> baseClasses_;

    Members publicMembers_;
    Members protectedMembers_;
    Members privateMembers_;
};

} // namespace cpptools_codegen
