#include "cpptools_codegen/classbuilder.h"

#include <sstream>

namespace cpptools_codegen {

ClassBuilder::ClassBuilder(std::string name) : name_(std::move(name)) {}

ClassBuilder& ClassBuilder::makeStruct(bool value) {
    isStruct_ = value;
    return *this;
}

ClassBuilder& ClassBuilder::addTemplateParam(std::string param) {
    templateParams_.push_back(std::move(param));
    return *this;
}

ClassBuilder& ClassBuilder::addBaseClass(std::string baseClass, std::string accessSpecifier) {
    baseClasses_.push_back(std::move(accessSpecifier) + " " + std::move(baseClass));
    return *this;
}

ClassBuilder& ClassBuilder::addPublicMethod(MethodBuilder method) {
    publicMembers_.methods.push_back(std::move(method));
    return *this;
}

ClassBuilder& ClassBuilder::addProtectedMethod(MethodBuilder method) {
    protectedMembers_.methods.push_back(std::move(method));
    return *this;
}

ClassBuilder& ClassBuilder::addPrivateMethod(MethodBuilder method) {
    privateMembers_.methods.push_back(std::move(method));
    return *this;
}

ClassBuilder& ClassBuilder::addPublicField(std::string declaration) {
    publicMembers_.fields.push_back(std::move(declaration));
    return *this;
}

ClassBuilder& ClassBuilder::addProtectedField(std::string declaration) {
    protectedMembers_.fields.push_back(std::move(declaration));
    return *this;
}

ClassBuilder& ClassBuilder::addPrivateField(std::string declaration) {
    privateMembers_.fields.push_back(std::move(declaration));
    return *this;
}

std::string ClassBuilder::toString(std::size_t indentLevel) const {
    std::ostringstream out;
    const std::string baseIndent(indentLevel * 4, ' ');
    const std::string memberIndent((indentLevel + 1) * 4, ' ');

    if (!templateParams_.empty()) {
        out << baseIndent << "template <";
        for (std::size_t i = 0; i < templateParams_.size(); ++i) {
            out << templateParams_[i];
            if (i + 1 < templateParams_.size()) {
                out << ", ";
            }
        }
        out << ">\n";
    }

    out << baseIndent << (isStruct_ ? "struct " : "class ") << name_;
    if (!baseClasses_.empty()) {
        out << " : ";
        for (std::size_t i = 0; i < baseClasses_.size(); ++i) {
            out << baseClasses_[i];
            if (i + 1 < baseClasses_.size()) {
                out << ", ";
            }
        }
    }
    out << " {\n";

    const Members* groups[] = {&publicMembers_, &protectedMembers_, &privateMembers_};
    const char* labels[] = {"public", "protected", "private"};
    for (std::size_t g = 0; g < 3; ++g) {
        const Members& members = *groups[g];
        if (members.empty()) {
            continue;
        }
        out << baseIndent << labels[g] << ":\n";
        for (const std::string& field : members.fields) {
            out << memberIndent << field << "\n";
        }
        if (!members.fields.empty() && !members.methods.empty()) {
            out << "\n";
        }
        for (const MethodBuilder& method : members.methods) {
            out << method.toString(indentLevel + 1);
        }
    }

    out << baseIndent << "};\n";
    return out.str();
}

} // namespace cpptools_codegen
