#include "ControllerWiringAction.h"

#include <cctype>
#include <fstream>
#include <iterator>
#include <vector>

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
                      const std::string& handlerName, std::function<void(WireResult)> done)
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
}
