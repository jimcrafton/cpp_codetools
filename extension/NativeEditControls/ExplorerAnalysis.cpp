#include "ExplorerAnalysis.h"

#include <cpptools/includeanalysis.h>

#include <algorithm>
#include <map>
#include <cctype>
#include <filesystem>
#include <memory>
#include <set>

namespace CodeToolsVsix
{
    namespace
    {
        using Tone = ExplorerNode::Tone;
        using cpptools::IncludeGraph;

        constexpr std::size_t kMostHeaders = 300;      // a long tail of one-includer headers is not a list to read
        constexpr std::size_t kMostIncluders = 200;
        constexpr std::size_t kMostHints = 300;

        std::string lowered(std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return text;
        }

        std::string relativeTo(std::string root, std::string path)
        {
            std::replace(root.begin(), root.end(), '\\', '/');
            std::replace(path.begin(), path.end(), '\\', '/');
            while (!root.empty() && root.back() == '/') root.pop_back();
            if (!root.empty() && path.size() > root.size() + 1 && path[root.size()] == '/' &&
                lowered(path.substr(0, root.size())) == lowered(root)) {
                return path.substr(root.size() + 1);
            }
            return path;
        }

        std::string countText(std::size_t n)
        {
            std::string digits = std::to_string(n);
            for (int at = static_cast<int>(digits.size()) - 3; at > 0; at -= 3) digits.insert(static_cast<std::size_t>(at), ",");
            return digits;
        }

        std::string filesText(std::size_t n) { return countText(n) + (n == 1 ? " file" : " files"); }

        // A header that rebuilds a quarter of the project is a warning, half of it a problem.
        Tone impactTone(std::size_t reached, std::size_t whole)
        {
            if (whole == 0) return Tone::Normal;
            const double share = static_cast<double>(reached) / static_cast<double>(whole);
            return share >= 0.5 ? Tone::Bad : share >= 0.25 ? Tone::Warn : Tone::Normal;
        }

        ExplorerNode group(const std::string& text, const std::string& detail = std::string())
        {
            ExplorerNode node;
            node.kind = ExplorerNode::Kind::Group;
            node.text = text;
            node.detail = detail;
            return node;
        }

        ExplorerNode note(const std::string& text)
        {
            ExplorerNode node;
            node.kind = ExplorerNode::Kind::Note;
            node.text = text;
            node.search = lowered(text);
            return node;
        }

        ExplorerNode fileRow(const std::string& root, const std::string& path, std::size_t line = 0)
        {
            ExplorerNode row;
            row.kind = ExplorerNode::Kind::File;
            row.text = relativeTo(root, path);
            row.path = path;
            row.line = line;
            row.search = lowered(row.text);
            return row;
        }

        bool matches(const std::string& filterLower, const std::string& text)
        {
            return filterLower.empty() || lowered(text).find(filterLower) != std::string::npos;
        }

        // What a file includes, one level: the rows for its direct includes, each openable further.
        void fillIncludes(ExplorerNode& node, const std::shared_ptr<const IncludeGraph>& graph, const std::string& root,
                          const std::string& path, std::vector<std::string> above)
        {
            above.push_back(lowered(path));
            for (const cpptools::IncludeEdge& edge : graph->includesOf(path)) {
                const std::string key = lowered(edge.included);
                ExplorerNode child = fileRow(root, edge.included, 0);
                child.detail = "line " + std::to_string(edge.line);
                if (edge.external) child.detail += "   system or third-party";
                if (std::find(above.begin(), above.end(), key) != above.end()) {
                    child.detail += "   (includes itself, further up)";
                } else if (!graph->includesOf(edge.included).empty()) {
                    child.loaded = false;
                    child.loader = [graph, root, included = edge.included, above](ExplorerNode& next) {
                        fillIncludes(next, graph, root, included, above);
                    };
                }
                node.children.push_back(std::move(child));
            }
        }
    }

    namespace
    {
        // What the detail pane says about one file's place in the include graph.
        std::shared_ptr<const ExplorerCard> includeCard(const IncludeGraph& graph, const std::string& root, const cpptools::HeaderImpact& impact)
        {
            const std::string br = "\r\n";
            auto card = std::make_shared<ExplorerCard>();
            card->title = relativeTo(root, impact.path);
            card->graphFile = impact.path;
            std::string& body = card->body;
            body = countText(impact.transitiveSources) + " translation units rebuild when this changes." + br +
                   countText(impact.directIncluders) + " files include it directly, " + countText(impact.transitiveIncluders) +
                   " reach it in all." + br + "It pulls in " + countText(impact.transitiveIncludes) + " files itself.";
            const std::vector<std::string> includers = graph.includersOf(impact.path);
            if (!includers.empty()) body += br + br + "Included by:";
            for (std::size_t i = 0; i < includers.size() && i < 6; ++i) body += br + "  " + relativeTo(root, includers[i]);
            if (includers.size() > 6) body += br + "  and " + countText(includers.size() - 6) + " more";
            return card;
        }
    }

    ExplorerNode buildIncludeImpactTree(const std::shared_ptr<const IncludeGraph>& graph, const std::string& root, const std::string& filter)
    {
        ExplorerNode top;
        if (graph == nullptr || graph->fileCount() == 0) {
            top.children.push_back(note("Nothing is indexed yet."));
            return top;
        }
        const std::string wanted = lowered(filter);

        // Files that can be rebuilt at all: the denominator of "how much of the project".
        const std::vector<cpptools::HeaderImpact> ranked = graph->ranked(false);

        std::vector<const cpptools::HeaderImpact*> kept;
        for (const cpptools::HeaderImpact& impact : ranked) {
            if (matches(wanted, relativeTo(root, impact.path))) kept.push_back(&impact);
        }
        const std::size_t sourceCount = graph->sourceFileCount();
        std::size_t most = 1;
        for (const cpptools::HeaderImpact* impact : kept) most = std::max(most, impact->transitiveSources);
        ExplorerNode headers = group("Most expensive headers", "(" + countText(kept.size()) + ") by translation units reached");
        for (std::size_t i = 0; i < kept.size() && i < kMostHeaders; ++i) {
            const cpptools::HeaderImpact& impact = *kept[i];
            ExplorerNode row = fileRow(root, impact.path);
            row.text = std::filesystem::path(impact.path).filename().string();   // the full path is in `search` and the card
            row.search = lowered(relativeTo(root, impact.path));
            row.bar = static_cast<float>(impact.transitiveSources) / static_cast<float>(most);
            row.barTone = row.bar > 0.7f ? Tone::Bad : row.bar > 0.35f ? Tone::Warn : Tone::Accent;
            row.cells = { countText(impact.transitiveSources) };
            row.cellTones = { impactTone(impact.transitiveSources, sourceCount) };
            row.cellWidth = 44.0f;
            row.card = includeCard(*graph, root, impact);
            const std::vector<std::string> includers = graph->includersOf(impact.path);
            for (std::size_t j = 0; j < includers.size() && j < kMostIncluders; ++j) {
                ExplorerNode includer = fileRow(root, includers[j]);
                for (const cpptools::IncludeEdge& edge : graph->includesOf(includers[j])) {
                    if (lowered(edge.included) == lowered(impact.path)) {
                        includer.line = edge.line;
                        includer.detail = "line " + std::to_string(edge.line);
                        break;
                    }
                }
                row.children.push_back(std::move(includer));
            }
            if (includers.size() > kMostIncluders) row.children.push_back(note("and " + countText(includers.size() - kMostIncluders) + " more"));
            headers.children.push_back(std::move(row));
        }
        if (kept.size() > kMostHeaders) headers.children.push_back(note("and " + countText(kept.size() - kMostHeaders) + " more: filter to find one"));
        if (kept.empty()) headers.children.push_back(note(wanted.empty() ? "No project header is included by another file." : "No header matches the filter."));
        top.children.push_back(std::move(headers));

        std::vector<cpptools::PruneHint> hints = graph->pruneHints();
        std::vector<const cpptools::PruneHint*> shown;
        for (const cpptools::PruneHint& hint : hints) {
            if (matches(wanted, relativeTo(root, hint.includer) + " " + relativeTo(root, hint.header))) shown.push_back(&hint);
        }
        ExplorerNode prune = group("Includes that may be unneeded", "(" + countText(shown.size()) + ")");
        for (std::size_t i = 0; i < shown.size() && i < kMostHints; ++i) {
            ExplorerNode row = fileRow(root, shown[i]->includer, shown[i]->line);
            row.detail = "line " + std::to_string(shown[i]->line) + ": " + relativeTo(root, shown[i]->header);
            row.detailTone = Tone::Warn;
            row.search = lowered(row.text + " " + relativeTo(root, shown[i]->header));
            prune.children.push_back(std::move(row));
        }
        if (shown.size() > kMostHints) prune.children.push_back(note("and " + countText(shown.size() - kMostHints) + " more"));
        if (shown.empty()) prune.children.push_back(note("None found. Nothing the file uses is checked against macros or templates, so this is a hint."));
        top.children.push_back(std::move(prune));
        return top;
    }

    ExplorerNode buildIncludeFileTree(const std::shared_ptr<const IncludeGraph>& graph, const std::string& root, const std::string& filter)
    {
        ExplorerNode top;
        if (graph == nullptr || graph->fileCount() == 0) {
            top.children.push_back(note("Nothing is indexed yet."));
            return top;
        }
        const std::string wanted = lowered(filter);
        std::vector<cpptools::HeaderImpact> files = graph->projectFiles();
        std::sort(files.begin(), files.end(), [&](const cpptools::HeaderImpact& a, const cpptools::HeaderImpact& b) {
            return lowered(relativeTo(root, a.path)) < lowered(relativeTo(root, b.path));
        });
        for (const cpptools::HeaderImpact& file : files) {
            ExplorerNode row = fileRow(root, file.path);
            if (!matches(wanted, row.text)) continue;
            row.cells = { countText(graph->includesOf(file.path).size()) + " includes", filesText(file.transitiveIncludes) + " in all" };
            row.cellTones = { Tone::Normal, Tone::Muted };
            row.cellWidth = 96.0f;
            row.card = includeCard(*graph, root, file);
            if (!graph->includesOf(file.path).empty()) {
                row.loaded = false;
                row.loader = [graph, root, path = file.path](ExplorerNode& node) { fillIncludes(node, graph, root, path, {}); };
            }
            top.children.push_back(std::move(row));
        }
        if (top.children.empty()) top.children.push_back(note(wanted.empty() ? "No project file is indexed." : "No file matches the filter."));
        return top;
    }

    namespace
    {
        using cpptools_analysis::MacroOrigin;

        std::string originText(MacroOrigin origin, const std::string& definedIn, std::size_t definedLine, bool folderScope)
        {
            switch (origin) {
                case MacroOrigin::CommandLine: return "defined by /D";
                case MacroOrigin::BuiltIn: return "built in";
                case MacroOrigin::Header: return "from " + std::filesystem::path(definedIn).filename().string();
                case MacroOrigin::File: return folderScope || definedLine == 0 ? "defined here" : "defined at line " + std::to_string(definedLine);
            }
            return std::string();
        }

        // The colors of a use's macros, in the order the tree meets them (the use's own macro first).
        void numberMacros(const cpptools_analysis::MacroTreeNode& node, std::map<std::string, int>& numbers)
        {
            if (!node.name.empty() && numbers.find(node.name) == numbers.end()) {
                const int next = static_cast<int>(numbers.size());
                numbers[node.name] = next;
            }
            for (const cpptools_analysis::MacroTreeNode& child : node.children) numberMacros(child, numbers);
        }

        // `node`'s macros as rows, each opening its #define when clicked.
        void addDependencyRows(ExplorerNode& parent, const cpptools_analysis::MacroTreeNode& node, const std::map<std::string, int>& numbers,
                               const std::string& file, bool folderScope, const std::shared_ptr<const ExplorerCard>& card)
        {
            for (const cpptools_analysis::MacroTreeNode& child : node.children) {
                ExplorerNode row;
                row.kind = ExplorerNode::Kind::Note;
                row.text = child.name;
                row.detail = originText(child.origin, child.definedIn, child.definedLine, folderScope);
                row.search = lowered(child.name);
                auto number = numbers.find(child.name);
                row.swatch = number != numbers.end() ? number->second : -1;
                if (child.origin == MacroOrigin::File) row.path = file;
                else if (child.origin == MacroOrigin::Header) row.path = child.definedIn;
                if (!row.path.empty()) {
                    row.line = child.definedLine;
                    row.openOnSelect = true;
                }
                row.card = card;
                addDependencyRows(row, child, numbers, file, folderScope, card);
                parent.children.push_back(std::move(row));
            }
        }

        // What one use shows in the detail pane: what it became, each part tinted like the macro that wrote it.
        std::shared_ptr<const ExplorerCard> cardForUse(const cpptools_analysis::MacroUseInfo& use,
                                                       const std::vector<cpptools_analysis::MacroWarning>& warnings,
                                                       const std::map<std::string, int>& numbers)
        {
            auto card = std::make_shared<ExplorerCard>();
            card->title = use.written + " at line " + std::to_string(use.line) + " becomes:";
            for (const cpptools_analysis::MacroSpan& span : use.spans) {
                auto number = numbers.find(span.macro);
                card->spans.push_back({ span.text, number != numbers.end() ? number->second : -1 });
            }
            if (card->spans.empty()) card->spans.push_back({ use.written, -1 });
            for (std::size_t i = 0; i < use.steps.size(); ++i) {
                card->steps.emplace_back(std::to_string(i) + " " + (i == 0 ? std::string("Source") : use.steps[i].macro), use.steps[i].text);
            }
            for (const cpptools_analysis::MacroWarning& warning : warnings) {
                if (warning.line == use.line && warning.macro == use.name) {
                    card->warnings.push_back("Argument \"" + warning.argument + "\" (" + warning.param + ") " + warning.reason +
                                             " and is evaluated more than once in the expansion.");
                }
            }
            return card;
        }

        struct MacroGroup
        {
            std::string name;
            cpptools_analysis::MacroUseInfo sample;   // origin and definition of the first use
            std::size_t levels = 0;
            ExplorerNode node;
        };
    }

    ExplorerNode buildMacroTree(const std::vector<MacroFile>& files, const std::string& root, bool folderScope, const std::string& filter)
    {
        ExplorerNode top;
        const std::string wanted = lowered(filter);
        std::vector<MacroGroup> macros;                          // direct uses, in the order first met
        std::size_t inactiveCount = 0;
        std::size_t warningCount = 0;
        ExplorerNode inactive = group("Inactive regions");
        ExplorerNode warnings = group("Evaluated more than once");
        bool anyFailed = false;

        for (const MacroFile& file : files) {
            if (!file.analysis.ok) { anyFailed = true; continue; }
            const std::string where = relativeTo(root, file.path);
            for (const cpptools_analysis::MacroUseInfo& use : file.analysis.uses) {
                auto found = std::find_if(macros.begin(), macros.end(), [&](const MacroGroup& m) { return m.name == use.name; });
                if (found == macros.end()) {
                    MacroGroup created;
                    created.name = use.name;
                    created.sample = use;
                    created.node.kind = ExplorerNode::Kind::Macro;
                    created.node.text = use.name;
                    created.node.search = lowered(use.name);
                    macros.push_back(std::move(created));
                    found = macros.end() - 1;
                }
                ExplorerNode row;
                row.kind = ExplorerNode::Kind::Note;
                row.text = (folderScope ? where + ":" : std::string("line ")) + std::to_string(use.line) + "   " + use.written;
                row.search = lowered(row.text + " " + use.name);
                row.path = file.path;
                row.line = use.line;
                row.openOnSelect = true;
                std::map<std::string, int> numbers;
                numberMacros(use.tree, numbers);
                row.card = cardForUse(use, file.analysis.warnings, numbers);
                auto own = numbers.find(use.name);
                row.swatch = own != numbers.end() ? own->second : -1;
                addDependencyRows(row, use.tree, numbers, file.path, folderScope, row.card);
                found->levels = std::max(found->levels, use.steps.empty() ? std::size_t(0) : use.steps.size() - 1);
                found->node.children.push_back(std::move(row));
            }
            for (const cpptools_analysis::InactiveRegion& region : file.analysis.inactive) {
                const std::string lines = region.startLine == region.endLine
                    ? "line " + std::to_string(region.startLine)
                    : "lines " + std::to_string(region.startLine) + "-" + std::to_string(region.endLine);
                ExplorerNode row;
                row.kind = ExplorerNode::Kind::Note;
                row.text = region.directive.empty() ? std::string("skipped") : region.directive;
                row.detail = (folderScope ? where + " " : std::string()) + lines;
                for (std::size_t i = 0; i < region.macros.size(); ++i) row.detail += (i == 0 ? " · " : ", ") + region.macros[i];
                row.detailTone = Tone::Muted;
                row.path = file.path;
                row.line = region.startLine;
                row.openOnSelect = true;
                row.search = lowered(row.text + " " + row.detail);
                if (!matches(wanted, row.search)) continue;
                inactive.children.push_back(std::move(row));
                ++inactiveCount;
            }
            for (const cpptools_analysis::MacroWarning& warning : file.analysis.warnings) {
                ExplorerNode row;
                row.kind = ExplorerNode::Kind::Note;
                row.text = warning.macro + ": " + warning.param + " = " + warning.argument;
                row.detail = (folderScope ? where + " " : std::string()) + "line " + std::to_string(warning.line) + "  " + warning.reason;
                row.detailTone = Tone::Warn;
                row.badge = ExplorerNode::Badge::Warning;
                row.path = file.path;
                row.line = warning.line;
                row.openOnSelect = true;
                row.search = lowered(row.text + " " + row.detail);
                if (!matches(wanted, row.search)) continue;
                warnings.children.push_back(std::move(row));
                ++warningCount;
            }
        }

        std::stable_sort(macros.begin(), macros.end(), [](const MacroGroup& a, const MacroGroup& b) {
            return a.node.children.size() > b.node.children.size();
        });
        std::string title = "Macros used in this folder";
        if (!folderScope) {
            title = files.empty() ? "Macros used in this file" : "Macros used in " + std::filesystem::path(files.front().path).filename().string();
        }
        ExplorerNode list = group(title);
        for (MacroGroup& macro : macros) {
            if (!matches(wanted, macro.name)) continue;
            const std::size_t uses = macro.node.children.size();
            macro.node.detail = originText(macro.sample.origin, macro.sample.definedIn, macro.sample.definedLine, folderScope) + " · " + std::to_string(uses) +
                                (uses == 1 ? " use" : " uses") +
                                (macro.levels > 1 ? " · " + std::to_string(macro.levels) + " levels" : std::string());
            macro.node.card = macro.node.children.front().card;
            // a click opens the #define; a macro with none to show (built in, from /D) opens its first use
            macro.node.path = macro.node.children.front().path;
            macro.node.line = macro.node.children.front().line;
            const cpptools_analysis::MacroTreeNode& own = macro.sample.tree;
            if (own.definedLine > 0 && own.origin == MacroOrigin::File) {
                macro.node.line = own.definedLine;
                macro.node.openOnSelect = true;
            } else if (own.definedLine > 0 && own.origin == MacroOrigin::Header && !own.definedIn.empty()) {
                macro.node.path = own.definedIn;
                macro.node.line = own.definedLine;
                macro.node.openOnSelect = true;
            }
            list.children.push_back(std::move(macro.node));
        }
        list.detail = "(" + std::to_string(list.children.size()) + ")";
        if (list.children.empty()) {
            list.children.push_back(note(anyFailed ? "The file could not be analyzed." : wanted.empty() ? "No macros are used here." : "No macro matches the filter."));
        }
        top.children.push_back(std::move(list));

        if (inactiveCount > 0) {
            inactive.detail = "(" + std::to_string(inactiveCount) + ")";
            top.children.push_back(std::move(inactive));
        }
        if (warningCount > 0) {
            warnings.detail = "(" + std::to_string(warningCount) + ")";
            top.children.push_back(std::move(warnings));
        }
        return top;
    }
}
