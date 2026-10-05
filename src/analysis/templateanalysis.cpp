#include "cpptools_analysis/templateanalysis.h"

#include <clang/AST/ASTConsumer.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendAction.h>

#include <algorithm>
#include <cctype>
#include <map>

#include "analysistool.h"

using namespace clang;

namespace cpptools_analysis {

namespace {

std::string normalized(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    return path;
}

std::string lowered(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

bool isUnder(const std::string& path, const std::string& folderLower) {
    if (folderLower.empty()) return true;
    const std::string lowerPath = lowered(normalized(path));
    return lowerPath.size() > folderLower.size() && lowerPath.compare(0, folderLower.size(), folderLower) == 0 &&
           (folderLower.back() == '/' || lowerPath[folderLower.size()] == '/');
}

TemplateInstantiationInfo::Kind kindOf(TemplateSpecializationKind kind) {
    switch (kind) {
        case TSK_ExplicitSpecialization: return TemplateInstantiationInfo::Kind::ExplicitSpecialization;
        case TSK_ExplicitInstantiationDeclaration:
        case TSK_ExplicitInstantiationDefinition: return TemplateInstantiationInfo::Kind::ExplicitInstantiation;
        default: return TemplateInstantiationInfo::Kind::Implicit;
    }
}

class Visitor : public RecursiveASTVisitor<Visitor> {
public:
    Visitor(ASTContext& context, const std::string& declaredUnder, TemplateAnalysis& out)
        : context_(context), sm_(context.getSourceManager()), under_(lowered(normalized(declaredUnder))), out_(out) {
        while (!under_.empty() && under_.back() == '/') under_.pop_back();
        policy_ = context.getPrintingPolicy();
        policy_.SuppressTagKeyword = true;
    }

    bool shouldVisitTemplateInstantiations() const { return true; }

    bool VisitClassTemplateSpecializationDecl(ClassTemplateSpecializationDecl* decl) {
        if (isa<ClassTemplatePartialSpecializationDecl>(decl)) return true;
        const ClassTemplateDecl* tmpl = decl->getSpecializedTemplate();
        if (tmpl == nullptr) return true;
        TemplateInstantiationInfo info;
        info.type = QualType(context_.getCanonicalTagType(decl)).getAsString(policy_);
        info.kind = kindOf(decl->getSpecializationKind());
        info.complete = decl->isCompleteDefinition();
        record(tmpl->getQualifiedNameAsString(), TemplateInfo::Kind::Class, tmpl->getLocation(),
               decl->getPointOfInstantiation().isValid() ? decl->getPointOfInstantiation() : decl->getLocation(), std::move(info));
        return true;
    }

    bool VisitFunctionDecl(FunctionDecl* decl) {
        const FunctionTemplateDecl* tmpl = decl->getPrimaryTemplate();
        const TemplateArgumentList* args = decl->getTemplateSpecializationArgs();
        if (tmpl == nullptr || args == nullptr) return true;
        TemplateInstantiationInfo info;
        llvm::raw_string_ostream text(info.type);
        printTemplateArgumentList(text, args->asArray(), policy_);
        text.flush();
        info.kind = kindOf(decl->getTemplateSpecializationKind());
        info.complete = decl->isDefined();
        record(tmpl->getQualifiedNameAsString(), TemplateInfo::Kind::Function, tmpl->getLocation(),
               decl->getPointOfInstantiation().isValid() ? decl->getPointOfInstantiation() : decl->getLocation(), std::move(info));
        return true;
    }

private:
    void record(const std::string& name, TemplateInfo::Kind kind, SourceLocation declared, SourceLocation needed,
                TemplateInstantiationInfo info) {
        const SourceLocation declaredAt = sm_.getExpansionLoc(declared);
        const std::string declaredFile = normalized(sm_.getFilename(declaredAt).str());
        if (!isUnder(declaredFile, under_)) return;

        const SourceLocation neededAt = sm_.getExpansionLoc(needed);
        info.file = neededAt.isValid() ? normalized(sm_.getFilename(neededAt).str()) : declaredFile;
        info.line = neededAt.isValid() ? sm_.getSpellingLineNumber(neededAt) : 0;

        const std::string key = (kind == TemplateInfo::Kind::Class ? "c:" : "f:") + name;
        auto found = index_.find(key);
        if (found == index_.end()) {
            TemplateInfo entry;
            entry.name = name;
            entry.kind = kind;
            entry.file = declaredFile;
            entry.line = sm_.getSpellingLineNumber(declaredAt);
            found = index_.emplace(key, out_.templates.size()).first;
            out_.templates.push_back(std::move(entry));
        }
        TemplateInfo& entry = out_.templates[found->second];
        auto same = std::find_if(entry.instantiations.begin(), entry.instantiations.end(),
                                 [&](const TemplateInstantiationInfo& other) { return other.type == info.type; });
        if (same == entry.instantiations.end()) entry.instantiations.push_back(std::move(info));
        else if (info.complete && !same->complete) same->complete = true;
    }

    ASTContext& context_;
    SourceManager& sm_;
    std::string under_;
    TemplateAnalysis& out_;
    PrintingPolicy policy_{ LangOptions() };
    std::map<std::string, std::size_t> index_;
};

class Consumer : public ASTConsumer {
public:
    Consumer(const std::string& declaredUnder, TemplateAnalysis& out) : declaredUnder_(declaredUnder), out_(out) {}

    void HandleTranslationUnit(ASTContext& context) override {
        Visitor visitor(context, declaredUnder_, out_);
        visitor.TraverseDecl(context.getTranslationUnitDecl());
    }

private:
    std::string declaredUnder_;
    TemplateAnalysis& out_;
};

class TemplateAction : public ASTFrontendAction {
public:
    TemplateAction(const std::string& declaredUnder, TemplateAnalysis& out) : declaredUnder_(declaredUnder), out_(out) {}

protected:
    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance&, StringRef) override {
        return std::make_unique<Consumer>(declaredUnder_, out_);
    }

private:
    std::string declaredUnder_;
    TemplateAnalysis& out_;
};

}  // namespace

TemplateAnalysis analyzeTemplates(const std::string& content, const std::string& path, const std::vector<std::string>& args,
                                  const std::string& declaredUnder) {
    TemplateAnalysis result;
    result.ok = runAnalysisTool(std::make_unique<TemplateAction>(declaredUnder, result), content, path, args);
    return result;
}

}  // namespace cpptools_analysis
