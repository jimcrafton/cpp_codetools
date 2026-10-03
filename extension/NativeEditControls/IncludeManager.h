#pragma once

#include <string>
#include <vector>

#include <newui/reflection.h>

namespace CodeToolsVsix
{
    // Which headers a file needs to name a reflected class - from Class::header() (recorded by
    // reflectgen), so it is exact for every reflected class and free (compiled in). Each string is an
    // include target with its delimiters - "<newui/controls.h>" or "\"S1Controller.h\"" - so
    // "#include " + s is a valid line. Results are remembered per class; the registry's Class objects
    // outlive any caller, so a pointer is a safe key. Thread-safe.
    //
    // Two forms, for two needs:
    //  - getHeaders(): the class's whole dependency list - every ancestor's header first (base to
    //    derived), then its own, duplicates dropped keeping the first. What a file needs to *derive
    //    from* or fully use the class.
    //  - headerOf()/headersOf(): only the header that defines the class. What a file needs to *name* it
    //    (a pointer field, a parameter type): include what you use, directly - not its bases' headers,
    //    and not whatever happens to be included through something else. Headers are self-contained
    //    (newui checks that), so the defining header alone is enough.
    // Empty for a null class or one with no recorded header (a hand-registered class): the caller then
    // leaves the file alone rather than guess.
    class IncludeManager
    {
    public:
        static std::vector<std::string> getHeaders(const newui::reflection::Class* clazz);
        static std::vector<std::string> getHeaders(const std::vector<const newui::reflection::Class*>& classes);

        static std::string headerOf(const newui::reflection::Class* clazz);
        static std::vector<std::string> headersOf(const std::vector<const newui::reflection::Class*>& classes);

        // Forgets everything remembered (a class registry that changed under us, tests).
        static void clearCache();
    };
}
