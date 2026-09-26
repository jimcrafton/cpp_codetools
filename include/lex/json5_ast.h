#pragma once

// Lossless JSON5 syntax tree (see ui-json5-diff-visualizer-plan.md, section 2).
//
// "Lossless" here means comments survive: they are ordinary nodes interleaved
// with properties / elements in their container's child list, in source order.
// Offsets and lines let the UI slice the original text for display.
//
// Comment placement rules (all comments end up in a container's child list):
//   * before a member / element                -> immediately before it
//   * after a member / element (same line or not, before or after the comma)
//                                              -> immediately after it
//   * between a key and its value ("a /*x*/ : /*y*/ 1")
//                                              -> immediately AFTER the Property
//   * inside "{ }" / "[ ]" with no members     -> the container's only children
//   * before / after the whole document        -> ParseResult::leading/trailingComments

#include <cstddef>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace lex {
namespace json5 {

enum class ASTNodeType {
    Object,
    Array,
    Property,     // one child: the value node (see valueNode())
    Primitive,    // string, number, boolean or null; see PrimitiveKind
    CommentLine,  // // ...
    CommentBlock, // /* ... */
};

enum class PrimitiveKind { None, String, Number, Boolean, Null };

struct ASTNode;
using ASTNodePtr = std::shared_ptr<ASTNode>;
using ASTChildren = std::vector<ASTNodePtr>;

struct ASTNode {
    ASTNode() = default;
    ASTNode(const ASTNode&) = default;
    ASTNode& operator=(const ASTNode&) = default;
    ASTNode(ASTNode&&) = default;
    ASTNode& operator=(ASTNode&&) = default;

    // Tear the subtree down iteratively. The default destructor recurses once per
    // nesting level (shared_ptr -> vector -> variant -> ~ASTNode, ~25 frames per
    // level in Debug builds), which overflows a 1 MB stack well inside maxDepth.
    // Note the same caution applies to any recursive walk over a deep tree.
    ~ASTNode() {
        ASTChildren* kids = std::get_if<ASTChildren>(&value);
        if (!kids || kids->empty()) return;

        ASTChildren work = std::move(*kids);
        kids->clear();
        while (!work.empty()) {
            ASTNodePtr n = std::move(work.back());
            work.pop_back();
            if (!n || n.use_count() != 1) continue;  // still shared elsewhere: not ours to tear down
            if (ASTChildren* sub = std::get_if<ASTChildren>(&n->value)) {
                for (ASTNodePtr& c : *sub) work.push_back(std::move(c));
                sub->clear();
            }
            // n dies here with no children, so its destructor returns immediately.
        }
    }

    ASTNodeType type = ASTNodeType::Primitive;
    PrimitiveKind primitive = PrimitiveKind::None;  // Primitive only

    // Property only: the decoded name ("a", 'a' and a all give L"a").
    std::wstring key;

    // Object / Array / Property -> ASTChildren; Primitive / comments -> raw source
    // text (quotes and comment delimiters included, e.g. L"'b'", L"0xFF", L"// hi").
    std::variant<std::monostate, std::wstring, ASTChildren> value;

    // 0-based; endLine / endOffset describe the last character (end is exclusive
    // for offsets, like Token::end()).
    std::size_t startLine = 0;
    std::size_t endLine = 0;
    std::size_t startOffset = 0;
    std::size_t endOffset = 0;

    std::size_t structuralHash = 0;  // filled in by the diff engine (Phase 2)

    bool isComment() const noexcept {
        return type == ASTNodeType::CommentLine || type == ASTNodeType::CommentBlock;
    }
    bool isContainer() const noexcept {
        return type == ASTNodeType::Object || type == ASTNodeType::Array;
    }

    const std::wstring& text() const noexcept {
        static const std::wstring empty;
        const std::wstring* s = std::get_if<std::wstring>(&value);
        return s ? *s : empty;
    }

    const ASTChildren& children() const noexcept {
        static const ASTChildren empty;
        const ASTChildren* c = std::get_if<ASTChildren>(&value);
        return c ? *c : empty;
    }

    // A Property's value, or nullptr if the source was malformed ("{ a: }").
    // Only a document with parse errors can produce a Property without a value.
    const ASTNode* valueNode() const noexcept {
        if (type != ASTNodeType::Property) return nullptr;
        const ASTChildren& c = children();
        return c.empty() ? nullptr : c.front().get();
    }
};

}  // namespace json5
}  // namespace lex
