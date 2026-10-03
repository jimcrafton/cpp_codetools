#include "ControllerRef.h"

#include <lex/json5_parser.h>

#include "TextEncoding.h"

namespace CodeToolsVsix
{
    namespace
    {
        const lex::json5::ASTNode* rootObject(const lex::json5::ParseResult& parsed)
        {
            return parsed.root && parsed.root->type == lex::json5::ASTNodeType::Object ? parsed.root.get() : nullptr;
        }

        const lex::json5::ASTNode* propertyNamed(const lex::json5::ASTNode& object, const wchar_t* key)
        {
            for (const lex::json5::ASTNodePtr& child : object.children())
            {
                if (child->type == lex::json5::ASTNodeType::Property && child->key == key)
                {
                    return child.get();
                }
            }
            return nullptr;
        }

        bool stringValue(const lex::json5::ASTNode* property, std::string& out)
        {
            const lex::json5::ASTNode* value = property != nullptr ? property->valueNode() : nullptr;
            if (value == nullptr || value->type != lex::json5::ASTNodeType::Primitive ||
                value->primitive != lex::json5::PrimitiveKind::String)
            {
                return false;
            }
            out = wideToUtf8(lex::json5::decodeString(value->text()));
            return true;
        }

        std::wstring quoted(const std::string& utf8)
        {
            std::wstring result = L"\"";
            for (wchar_t c : utf8ToWide(utf8))
            {
                if (c == L'"' || c == L'\\')
                {
                    result += L'\\';
                }
                result += c;
            }
            return result + L"\"";
        }

        std::wstring propertyText(const ControllerRef& ref)
        {
            return L"controller: { class: " + quoted(ref.className) + L", header: " + quoted(ref.header) + L" }";
        }
    }

    bool readControllerRef(const std::wstring& newuiText, ControllerRef& out)
    {
        const lex::json5::ParseResult parsed = lex::json5::parse(newuiText);
        const lex::json5::ASTNode* root = rootObject(parsed);
        const lex::json5::ASTNode* controller = root != nullptr ? propertyNamed(*root, L"controller") : nullptr;
        const lex::json5::ASTNode* value = controller != nullptr ? controller->valueNode() : nullptr;
        if (value == nullptr || value->type != lex::json5::ASTNodeType::Object)
        {
            return false;
        }

        ControllerRef ref;
        if (!stringValue(propertyNamed(*value, L"class"), ref.className) || ref.className.empty() ||
            !stringValue(propertyNamed(*value, L"header"), ref.header))
        {
            return false;
        }
        out = std::move(ref);
        return true;
    }

    bool planSetControllerRef(const std::wstring& newuiText, const ControllerRef& ref, std::vector<TextEdit>& out)
    {
        const lex::json5::ParseResult parsed = lex::json5::parse(newuiText);
        const lex::json5::ASTNode* root = rootObject(parsed);
        if (root == nullptr || !parsed.errors.empty())
        {
            return false;  // the parser recovers from garbage; editing a broken file would only add to it
        }

        if (const lex::json5::ASTNode* existing = propertyNamed(*root, L"controller"))
        {
            out = { { existing->startOffset, existing->endOffset - existing->startOffset, propertyText(ref) } };
            return true;
        }

        // First thing inside the top-level braces. A trailing comma is legal JSON5, so it is
        // added even when the object was empty.
        out = { { root->startOffset + 1, 0, L"\n  " + propertyText(ref) + L"," } };
        return true;
    }
}
