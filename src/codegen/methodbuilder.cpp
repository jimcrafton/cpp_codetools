#include "cpptools_codegen/methodbuilder.h"

#include <sstream>

namespace cpptools_codegen {

Argument Argument::raw(std::string declarator) {
    Argument argument;
    argument.declarator = std::move(declarator);
    return argument;
}

std::string Argument::toString() const {
    if (!declarator.empty()) {
        return declarator;
    }

    std::string out = type;
    if (!name.empty()) {
        out += " " + name;
    }
    if (!defaultValue.empty()) {
        out += " = " + defaultValue;
    }
    return out;
}

MethodBuilder::MethodBuilder(std::string name) : name_(std::move(name)) {}

MethodBuilder& MethodBuilder::returnType(std::string type) {
    returnType_ = std::move(type);
    return *this;
}

MethodBuilder& MethodBuilder::addTemplateParam(std::string param) {
    templateParams_.push_back(std::move(param));
    return *this;
}

MethodBuilder& MethodBuilder::addArgument(Argument argument) {
    arguments_.push_back(std::move(argument));
    return *this;
}

MethodBuilder& MethodBuilder::addArgument(std::string type, std::string name, std::string defaultValue) {
    Argument argument;
    argument.type = std::move(type);
    argument.name = std::move(name);
    argument.defaultValue = std::move(defaultValue);
    return addArgument(std::move(argument));
}

MethodBuilder& MethodBuilder::addBodyLine(std::string line) {
    bodyLines_.push_back(std::move(line));
    return *this;
}

MethodBuilder& MethodBuilder::makeConst(bool value) {
    isConst_ = value;
    return *this;
}

MethodBuilder& MethodBuilder::makeNoexcept(bool value) {
    isNoexcept_ = value;
    return *this;
}

MethodBuilder& MethodBuilder::makeVirtual(bool value) {
    isVirtual_ = value;
    return *this;
}

MethodBuilder& MethodBuilder::makeOverride(bool value) {
    isOverride_ = value;
    return *this;
}

MethodBuilder& MethodBuilder::makeStatic(bool value) {
    isStatic_ = value;
    return *this;
}

MethodBuilder& MethodBuilder::makeInline(bool value) {
    isInline_ = value;
    return *this;
}

std::string MethodBuilder::toString(std::size_t indentLevel) const {
    std::ostringstream out;
    const std::string baseIndent(indentLevel * 4, ' ');
    const std::string bodyIndent((indentLevel + 1) * 4, ' ');

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

    out << baseIndent;
    if (isStatic_) {
        out << "static ";
    }
    if (isVirtual_) {
        out << "virtual ";
    }
    if (isInline_) {
        out << "inline ";
    }
    out << returnType_ << " " << name_ << "(";

    for (std::size_t i = 0; i < arguments_.size(); ++i) {
        out << arguments_[i].toString();
        if (i + 1 < arguments_.size()) {
            out << ", ";
        }
    }
    out << ")";

    if (isConst_) {
        out << " const";
    }
    if (isNoexcept_) {
        out << " noexcept";
    }
    if (isOverride_) {
        out << " override";
    }

    out << " {\n";
    for (const std::string& line : bodyLines_) {
        out << bodyIndent << line << "\n";
    }
    out << baseIndent << "}\n";

    return out.str();
}

} // namespace cpptools_codegen
