#include "ControllerWiringAction.h"

#include <cctype>
#include <fstream>
#include <iterator>
#include <optional>
#include <regex>
#include <vector>

#include <cpptools/parser.h>
#include <cpptools_codegen/classbuilder.h>
#include <cpptools_codegen/controllerwiring.h>

#include "CodegenEditAdapter.h"
#include "IncludeManager.h"
#include "IncludePlanner.h"
#include "TextEncoding.h"
#include "WiringRequestBuilder.h"

namespace CodeToolsVsix
{
    namespace
    {
        bool isIdentifier(const std::string& name)
        {
            if (name.empty() || std::isdigit(static_cast<unsigned char>(name[0])))
            {
                return false;
            }
            for (char c : name)
            {
                if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_')
                {
                    return false;
                }
            }
            return true;
        }

        std::string readFile(const std::filesystem::path& path)
        {
            std::ifstream in(path, std::ios::binary);
            return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        }

        // "class Name" followed by something that isn't part of an identifier (so "NameTwo" doesn't count).
        bool definesClass(const std::string& text, const std::string& className)
        {
            for (const char* keyword : { "class ", "struct " })
            {
                const std::string needle = keyword + className;
                for (std::size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1))
                {
                    const std::size_t end = at + needle.size();
                    if (end >= text.size() || !(std::isalnum(static_cast<unsigned char>(text[end])) || text[end] == '_'))
                    {
                        return true;
                    }
                }
            }
            return false;
        }
    }

    std::string defaultControllerClassName(const std::filesystem::path& documentPath)
    {
        std::string base;
        for (char c : documentPath.stem().string())
        {
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_')
            {
                base += c;
            }
        }
        if (base.empty())
        {
            base = "Document";
        }
        if (std::isdigit(static_cast<unsigned char>(base[0])))
        {
            base = "_" + base;
        }
        base[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(base[0])));
        return base + "Controller";
    }

    std::string newControllerHeaderText(const std::string& className)
    {
        cpptools_codegen::ClassBuilder builder(className);
        builder.addBaseClass("newui::RootController").addPublicField("using RootController::RootController;");
        // The base class's own header; the control types the fields and handlers name are added by
        // wireDelegate() as it wires them (planIncludes), each from its own recorded header.
        std::string rootController = IncludeManager::headerOf(newui::reflection::classinfo(std::string("RootController")));
        if (rootController.empty())
        {
            rootController = "<newui/rootcontroller.h>";
        }
        return "#pragma once\n\n#include " + rootController + "\n\n" + builder.toString();
    }

    namespace
    {
        HeaderLookup& headerLookup()
        {
            static HeaderLookup lookup = [](const std::vector<const newui::reflection::Class*>& classes) {
                return IncludeManager::headersOf(classes);
            };
            return lookup;
        }
    }

    void setWiringHeaderLookup(HeaderLookup lookup)
    {
        if (lookup)
        {
            headerLookup() = std::move(lookup);
        }
        else
        {
            headerLookup() = [](const std::vector<const newui::reflection::Class*>& classes) {
                return IncludeManager::headersOf(classes);
            };
        }
    }

    NewControllerCheck checkNewController(const std::filesystem::path& documentPath, const std::string& className,
                                          const std::string& header)
    {
        if (!isIdentifier(className))
        {
            return { false, "The class name must be a C++ identifier (letters, digits and underscores)" };
        }
        if (header.empty())
        {
            return { false, "Enter a header file name" };
        }

        const std::filesystem::path relative = std::filesystem::u8path(header);
        bool escapes = relative.is_absolute() || relative.has_root_name() || relative.has_root_directory();
        for (const std::filesystem::path& part : relative)
        {
            escapes = escapes || part == "..";
        }
        if (escapes)
        {
            return { false, "The header must be a relative path inside the document's folder" };
        }

        std::string extension = relative.extension().string();
        for (char& c : extension)
        {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (extension != ".h" && extension != ".hpp" && extension != ".hh" && extension != ".hxx")
        {
            return { false, "The header should end in .h or .hpp" };
        }

        const std::filesystem::path full = documentPath.parent_path() / relative;
        std::error_code ec;
        if (std::filesystem::exists(full, ec))
        {
            return definesClass(readFile(full), className)
                ? NewControllerCheck{ true, header + " already defines " + className + " - it will be used" }
                : NewControllerCheck{ false, header + " already exists but doesn't define " + className };
        }
        if (!std::filesystem::is_directory(full.parent_path(), ec))
        {
            return { false, "The folder " + full.parent_path().u8string() + " doesn't exist" };
        }
        return { true, "Will create " + header + " next to the document" };
    }

    CreateControllerStatus createController(const std::filesystem::path& documentPath, const std::string& className,
                                            const std::string& header)
    {
        if (!isIdentifier(className) || header.empty())
        {
            return CreateControllerStatus::InvalidName;
        }
        const std::filesystem::path path = documentPath.parent_path() / std::filesystem::u8path(header);

        std::error_code ec;
        if (std::filesystem::exists(path, ec))
        {
            return definesClass(readFile(path), className) ? CreateControllerStatus::Adopted
                                                           : CreateControllerStatus::ExistsWithout;
        }

        std::ofstream out(path, std::ios::binary);
        out << newControllerHeaderText(className);
        out.flush();
        return out ? CreateControllerStatus::Created : CreateControllerStatus::WriteFailed;
    }

    void wireDelegate(DocumentEditService& service, const std::filesystem::path& documentPath,
                      const ControllerRef& controller, newui::View& view, const std::string& viewName,
                      const newui::reflection::Class& viewClass, const std::string& delegateName,
                      const std::string& handlerName, std::function<void(WireResult)> done, bool reuseExisting)
    {
        WireResult result;

        cpptools_codegen::ControllerWiringRequest request;
        if (!buildControllerWiringRequest(viewClass, delegateName, viewName, controller.className, request))
        {
            result.status = WireStatus::UnknownDelegate;
            done(std::move(result));
            return;
        }
        request.handlerName = handlerName;
        request.reuseExistingHandler = reuseExisting;

        const std::filesystem::path headerPath =
            documentPath.parent_path() / std::filesystem::u8path(controller.header);

        service.getText(headerPath,
            [&service, headerPath, controller, &view, &viewClass, delegateName, request = std::move(request),
             done = std::move(done)](EditStatus status, DocumentSnapshot snapshot) mutable
            {
                WireResult result;
                if (status != EditStatus::Ok)
                {
                    result.status = WireStatus::HeaderUnreadable;
                    result.editStatus = status;
                    done(std::move(result));
                    return;
                }

                // The planner reads the header as UTF-8 and answers in UTF-8 byte offsets.
                const cpptools_codegen::ControllerWiringPlan plan =
                    cpptools_codegen::planControllerDelegateWiring(planningText(snapshot.text), request);
                result.handlerName = plan.handlerName;
                if (plan.status == cpptools_codegen::ControllerWiringStatus::ClassNotFound)
                {
                    result.status = WireStatus::ControllerClassMissing;
                    done(std::move(result));
                    return;
                }
                if (plan.status == cpptools_codegen::ControllerWiringStatus::HandlerExists)
                {
                    result.status = WireStatus::HandlerExists;
                    done(std::move(result));
                    return;
                }
                if (plan.status == cpptools_codegen::ControllerWiringStatus::HandlerNotFound)
                {
                    result.status = WireStatus::HandlerNotFound;
                    done(std::move(result));
                    return;
                }

                FileEdits file;
                file.path = headerPath;
                file.expectedVersion = snapshot.version;
                if (!toTextEdits(snapshot.text, plan.edits, file.edits))
                {
                    result.status = WireStatus::EditFailed;
                    result.editStatus = EditStatus::InvalidEdit;
                    done(std::move(result));
                    return;
                }

                // The controller now names the control's class, the sender's and any class-typed
                // arguments': include each one's own header (the same edit group, so all or nothing).
                const IncludePlan includes = planIncludes(snapshot.text, headerLookup()(classesNamedBy(viewClass, delegateName)));
                file.edits.insert(file.edits.end(), includes.edits.begin(), includes.edits.end());

                const std::string descriptor = "this@" + controller.className + "." + plan.handlerName;
                std::vector<FileEdits> group;
                group.push_back(std::move(file));
                service.applyGroup(std::move(group),
                    [&view, &viewClass, delegateName, descriptor, result = std::move(result), done = std::move(done)]
                    (EditStatus applied, std::size_t /*appliedCount*/) mutable
                    {
                        if (applied != EditStatus::Ok)
                        {
                            result.status = WireStatus::EditFailed;
                            result.editStatus = applied;
                            done(std::move(result));
                            return;
                        }

                        // The C++ now says it; record the same on the control so the document keeps it.
                        std::vector<const newui::reflection::Delegate*> delegates;
                        viewClass.allDelegates(delegates);
                        for (const newui::reflection::Delegate* delegate : delegates)
                        {
                            if (delegate->name() == delegateName)
                            {
                                delegate->addDescriptorListener(&view, descriptor);
                                break;
                            }
                        }
                        result.descriptor = descriptor;
                        done(std::move(result));
                    });
            });
    }

    namespace
    {
        void dropDescriptor(newui::View& view, const newui::reflection::Class& viewClass,
                            const std::string& delegateName, const std::string& descriptor)
        {
            std::vector<const newui::reflection::Delegate*> delegates;
            viewClass.allDelegates(delegates);
            for (const newui::reflection::Delegate* delegate : delegates)
            {
                if (delegate->name() == delegateName)
                {
                    delegate->removeDescriptorListener(&view, descriptor);
                    break;
                }
            }
        }

        // The byte range to delete for the first `field->event.add(this, &Class::handler);` in text: the
        // whole line when nothing else is on it, else just the statement. Spacing is free. Empty if absent.
        std::optional<TextEdit> removalOf(const std::wstring& text, const std::string& field, const std::string& delegateName,
                                          const std::string& className, const std::string& handler)
        {
            const std::wstring pattern = utf8ToWide(field) + L"\\s*->\\s*" + utf8ToWide(delegateName) +
                L"\\s*\\.\\s*add\\s*\\(\\s*this\\s*,\\s*&\\s*" + utf8ToWide(className) + L"\\s*::\\s*" +
                utf8ToWide(handler) + L"\\s*\\)\\s*;";
            std::wsmatch match;
            if (!std::regex_search(text, match, std::wregex(pattern)))
            {
                return std::nullopt;
            }
            std::size_t begin = static_cast<std::size_t>(match.position(0));
            std::size_t end = begin + static_cast<std::size_t>(match.length(0));

            std::size_t lineBegin = begin;
            while (lineBegin > 0 && (text[lineBegin - 1] == L' ' || text[lineBegin - 1] == L'\t'))
            {
                --lineBegin;
            }
            std::size_t lineEnd = end;
            while (lineEnd < text.size() && (text[lineEnd] == L' ' || text[lineEnd] == L'\t'))
            {
                ++lineEnd;
            }
            const bool startsLine = lineBegin == 0 || text[lineBegin - 1] == L'\n';
            const bool endsLine = lineEnd >= text.size() || text[lineEnd] == L'\n' || text[lineEnd] == L'\r';
            if (startsLine && endsLine)
            {
                if (lineEnd < text.size() && text[lineEnd] == L'\r')
                {
                    ++lineEnd;
                }
                if (lineEnd < text.size() && text[lineEnd] == L'\n')
                {
                    ++lineEnd;
                }
                begin = lineBegin;
                end = lineEnd;
            }
            return TextEdit{ begin, end - begin, std::wstring() };
        }
    }

    void unwireDelegate(DocumentEditService& service, const std::filesystem::path& documentPath,
                        const ControllerRef& controller, newui::View& view, const std::string& viewName,
                        const newui::reflection::Class& viewClass, const std::string& delegateName,
                        const std::string& descriptor, std::function<void(UnwireStatus)> done)
    {
        // Only "this@<controller>.<handler>" is something the controller's header says.
        const std::string prefix = "this@" + controller.className + ".";
        if (descriptor.compare(0, prefix.size(), prefix) != 0)
        {
            dropDescriptor(view, viewClass, delegateName, descriptor);
            done(UnwireStatus::DocumentOnly);
            return;
        }
        const std::string handler = descriptor.substr(prefix.size());
        const std::string field = cpptools_codegen::controllerFieldName(viewName);
        const std::filesystem::path headerPath = documentPath.parent_path() / std::filesystem::u8path(controller.header);

        service.getText(headerPath,
            [&service, headerPath, &view, &viewClass, delegateName, descriptor, handler, field,
             className = controller.className, done = std::move(done)](EditStatus status, DocumentSnapshot snapshot) mutable
            {
                if (status != EditStatus::Ok)
                {
                    done(UnwireStatus::HeaderUnreadable);
                    return;
                }
                const std::optional<TextEdit> removal = removalOf(snapshot.text, field, delegateName, className, handler);
                if (!removal)
                {
                    dropDescriptor(view, viewClass, delegateName, descriptor);
                    done(UnwireStatus::DocumentOnly);
                    return;
                }

                FileEdits file;
                file.path = headerPath;
                file.expectedVersion = snapshot.version;
                file.edits.push_back(*removal);
                std::vector<FileEdits> group;
                group.push_back(std::move(file));
                service.applyGroup(std::move(group),
                    [&view, &viewClass, delegateName, descriptor, done = std::move(done)]
                    (EditStatus applied, std::size_t /*appliedCount*/) mutable
                    {
                        if (applied != EditStatus::Ok)
                        {
                            done(UnwireStatus::EditFailed);
                            return;
                        }
                        dropDescriptor(view, viewClass, delegateName, descriptor);
                        done(UnwireStatus::Ok);
                    });
            });
    }

    namespace
    {
        const cpptools::Symbol* findClassSymbol(const std::vector<cpptools::Symbol>& symbols, const std::string& className)
        {
            for (const cpptools::Symbol& symbol : symbols)
            {
                if ((symbol.kind == cpptools::SymbolKind::Class || symbol.kind == cpptools::SymbolKind::Struct) &&
                    symbol.name == className)
                {
                    return &symbol;
                }
                if (symbol.kind == cpptools::SymbolKind::Namespace)
                {
                    if (const cpptools::Symbol* inner = findClassSymbol(symbol.children, className))
                    {
                        return inner;
                    }
                }
            }
            return nullptr;
        }
    }

    std::vector<ControllerMethod> controllerMethods(const std::string& headerText, const std::string& className)
    {
        std::vector<ControllerMethod> methods;
        cpptools::Parser parser;
        const cpptools::ParseResult parsed = parser.parseBuffer("controller.h", headerText);
        const cpptools::Symbol* controller = findClassSymbol(parsed.symbols, className);
        if (controller == nullptr)
        {
            return methods;
        }
        for (const cpptools::Symbol& child : controller->children)
        {
            if (child.kind == cpptools::SymbolKind::Method && child.name != "internal_init")
            {
                methods.push_back({ child.name, child.location.line, child.location.column });
            }
        }
        return methods;
    }
}
