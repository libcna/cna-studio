// SPDX-License-Identifier: MS-PL
#include "CNA/Editor/Core/Json.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace CNA::Editor
{
    namespace
    {
        const JsonValue& nullValue()
        {
            static const JsonValue value{};
            return value;
        }

        const JsonValue::Array& emptyArray()
        {
            static const JsonValue::Array value{};
            return value;
        }

        const JsonValue::Object& emptyObject()
        {
            static const JsonValue::Object value{};
            return value;
        }
    }

    bool JsonValue::asBoolean(bool fallback) const
    {
        return type_ == JsonType::Boolean ? boolean_ : fallback;
    }

    double JsonValue::asNumber(double fallback) const
    {
        return type_ == JsonType::Number ? number_ : fallback;
    }

    float JsonValue::asFloat(float fallback) const
    {
        return type_ == JsonType::Number ? static_cast<float>(number_) : fallback;
    }

    int JsonValue::asInt(int fallback) const
    {
        return type_ == JsonType::Number ? static_cast<int>(number_) : fallback;
    }

    std::string JsonValue::asString(std::string fallback) const
    {
        return type_ == JsonType::String ? string_ : std::move(fallback);
    }

    const JsonValue::Array& JsonValue::getElements() const
    {
        return type_ == JsonType::Array ? array_ : emptyArray();
    }

    const JsonValue::Object& JsonValue::getMembers() const
    {
        return type_ == JsonType::Object ? object_ : emptyObject();
    }

    const JsonValue& JsonValue::operator[](std::string_view name) const
    {
        if (type_ != JsonType::Object) { return nullValue(); }
        const auto found = std::find_if(object_.begin(), object_.end(),
                                        [&](const auto& member) { return member.first == name; });
        return found == object_.end() ? nullValue() : found->second;
    }

    bool JsonValue::contains(std::string_view name) const
    {
        if (type_ != JsonType::Object) { return false; }
        return std::any_of(object_.begin(), object_.end(),
                           [&](const auto& member) { return member.first == name; });
    }

    bool JsonValue::remove(std::string_view name)
    {
        if (type_ != JsonType::Object) { return false; }

        const auto found = std::find_if(object_.begin(), object_.end(),
                                        [&](const auto& member) { return member.first == name; });
        if (found == object_.end()) { return false; }

        object_.erase(found);
        return true;
    }

    void JsonValue::set(std::string name, JsonValue value)
    {
        if (type_ != JsonType::Object)
        {
            type_ = JsonType::Object;
            object_.clear();
        }
        const auto found = std::find_if(object_.begin(), object_.end(),
                                        [&](const auto& member) { return member.first == name; });
        if (found == object_.end())
        {
            object_.emplace_back(std::move(name), std::move(value));
        }
        else
        {
            found->second = std::move(value);
        }
    }

    void JsonValue::append(JsonValue value)
    {
        if (type_ != JsonType::Array)
        {
            type_ = JsonType::Array;
            array_.clear();
        }
        array_.push_back(std::move(value));
    }

    namespace
    {
        /** @brief Recursive-descent reader over the whole document text. */
        class Parser
        {
        public:
            explicit Parser(std::string_view text) : text_(text) {}

            JsonParseResult run()
            {
                skipTrivia();
                JsonValue value;
                if (!parseValue(value)) { return failure(); }
                skipTrivia();
                if (offset_ != text_.size())
                {
                    error_ = "trailing characters after the top-level value";
                    return failure();
                }
                JsonParseResult result;
                result.succeeded = true;
                result.value = std::move(value);
                return result;
            }

        private:
            JsonParseResult failure() const
            {
                JsonParseResult result;
                result.succeeded = false;
                result.errorMessage = error_.empty() ? "malformed JSON" : error_;
                result.errorOffset = offset_;
                return result;
            }

            [[nodiscard]] bool atEnd() const { return offset_ >= text_.size(); }
            [[nodiscard]] char peek() const { return atEnd() ? '\0' : text_[offset_]; }

            void skipTrivia()
            {
                while (!atEnd())
                {
                    const char character = text_[offset_];
                    if (character == ' ' || character == '\t' || character == '\r' || character == '\n')
                    {
                        ++offset_;
                        continue;
                    }
                    // Editor-friendly relaxation: `//` line comments (see Json::parse docs).
                    if (character == '/' && offset_ + 1 < text_.size() && text_[offset_ + 1] == '/')
                    {
                        while (!atEnd() && text_[offset_] != '\n') { ++offset_; }
                        continue;
                    }
                    break;
                }
            }

            bool expect(char character)
            {
                if (peek() != character)
                {
                    error_ = std::string{"expected '"} + character + "'";
                    return false;
                }
                ++offset_;
                return true;
            }

            bool parseValue(JsonValue& out)
            {
                skipTrivia();
                switch (peek())
                {
                    case '{': return parseObject(out);
                    case '[': return parseArray(out);
                    case '"': {
                        std::string text;
                        if (!parseString(text)) { return false; }
                        out = JsonValue{std::move(text)};
                        return true;
                    }
                    case 't': return parseLiteral("true", JsonValue{true}, out);
                    case 'f': return parseLiteral("false", JsonValue{false}, out);
                    case 'n': return parseLiteral("null", JsonValue{}, out);
                    default: return parseNumber(out);
                }
            }

            bool parseLiteral(std::string_view literal, JsonValue value, JsonValue& out)
            {
                if (text_.substr(offset_, literal.size()) != literal)
                {
                    error_ = "malformed literal";
                    return false;
                }
                offset_ += literal.size();
                out = std::move(value);
                return true;
            }

            bool parseNumber(JsonValue& out)
            {
                const std::size_t start = offset_;
                if (peek() == '-' || peek() == '+') { ++offset_; }
                while (!atEnd() && (std::isdigit(static_cast<unsigned char>(text_[offset_])) != 0)) { ++offset_; }
                if (peek() == '.')
                {
                    ++offset_;
                    while (!atEnd() && (std::isdigit(static_cast<unsigned char>(text_[offset_])) != 0)) { ++offset_; }
                }
                if (peek() == 'e' || peek() == 'E')
                {
                    ++offset_;
                    if (peek() == '-' || peek() == '+') { ++offset_; }
                    while (!atEnd() && (std::isdigit(static_cast<unsigned char>(text_[offset_])) != 0)) { ++offset_; }
                }
                if (offset_ == start)
                {
                    error_ = "expected a value";
                    return false;
                }
                const std::string digits{text_.substr(start, offset_ - start)};
                out = JsonValue{std::strtod(digits.c_str(), nullptr)};
                return true;
            }

            bool parseString(std::string& out)
            {
                if (!expect('"')) { return false; }
                out.clear();
                while (true)
                {
                    if (atEnd())
                    {
                        error_ = "unterminated string";
                        return false;
                    }
                    const char character = text_[offset_++];
                    if (character == '"') { return true; }
                    if (character != '\\')
                    {
                        out.push_back(character);
                        continue;
                    }
                    if (atEnd())
                    {
                        error_ = "unterminated escape sequence";
                        return false;
                    }
                    const char escape = text_[offset_++];
                    switch (escape)
                    {
                        case '"': out.push_back('"'); break;
                        case '\\': out.push_back('\\'); break;
                        case '/': out.push_back('/'); break;
                        case 'b': out.push_back('\b'); break;
                        case 'f': out.push_back('\f'); break;
                        case 'n': out.push_back('\n'); break;
                        case 'r': out.push_back('\r'); break;
                        case 't': out.push_back('\t'); break;
                        case 'u': {
                            if (offset_ + 4 > text_.size())
                            {
                                error_ = "truncated \\u escape";
                                return false;
                            }
                            unsigned int codePoint = 0;
                            for (int index = 0; index < 4; ++index)
                            {
                                const char digit = text_[offset_++];
                                codePoint <<= 4u;
                                if (digit >= '0' && digit <= '9') { codePoint |= static_cast<unsigned>(digit - '0'); }
                                else if (digit >= 'a' && digit <= 'f') { codePoint |= static_cast<unsigned>(digit - 'a' + 10); }
                                else if (digit >= 'A' && digit <= 'F') { codePoint |= static_cast<unsigned>(digit - 'A' + 10); }
                                else
                                {
                                    error_ = "malformed \\u escape";
                                    return false;
                                }
                            }
                            appendUtf8(out, codePoint);
                            break;
                        }
                        default:
                            error_ = "unknown escape sequence";
                            return false;
                    }
                }
            }

            static void appendUtf8(std::string& out, unsigned int codePoint)
            {
                // Surrogate halves are emitted as the replacement character: the editor never
                // writes them, and a lone half in a hand-edited file is not worth failing over.
                if (codePoint >= 0xD800u && codePoint <= 0xDFFFu) { codePoint = 0xFFFDu; }

                if (codePoint < 0x80u)
                {
                    out.push_back(static_cast<char>(codePoint));
                }
                else if (codePoint < 0x800u)
                {
                    out.push_back(static_cast<char>(0xC0u | (codePoint >> 6u)));
                    out.push_back(static_cast<char>(0x80u | (codePoint & 0x3Fu)));
                }
                else
                {
                    out.push_back(static_cast<char>(0xE0u | (codePoint >> 12u)));
                    out.push_back(static_cast<char>(0x80u | ((codePoint >> 6u) & 0x3Fu)));
                    out.push_back(static_cast<char>(0x80u | (codePoint & 0x3Fu)));
                }
            }

            bool parseArray(JsonValue& out)
            {
                if (!expect('[')) { return false; }
                JsonValue result = JsonValue::makeArray();
                skipTrivia();
                if (peek() == ']')
                {
                    ++offset_;
                    out = std::move(result);
                    return true;
                }
                while (true)
                {
                    JsonValue element;
                    if (!parseValue(element)) { return false; }
                    result.append(std::move(element));
                    skipTrivia();
                    if (peek() == ',')
                    {
                        ++offset_;
                        skipTrivia();
                        // Editor-friendly relaxation: a trailing comma before ']' is accepted.
                        if (peek() == ']') { ++offset_; break; }
                        continue;
                    }
                    if (!expect(']')) { return false; }
                    break;
                }
                out = std::move(result);
                return true;
            }

            bool parseObject(JsonValue& out)
            {
                if (!expect('{')) { return false; }
                JsonValue result = JsonValue::makeObject();
                skipTrivia();
                if (peek() == '}')
                {
                    ++offset_;
                    out = std::move(result);
                    return true;
                }
                while (true)
                {
                    skipTrivia();
                    std::string name;
                    if (!parseString(name)) { return false; }
                    skipTrivia();
                    if (!expect(':')) { return false; }
                    JsonValue value;
                    if (!parseValue(value)) { return false; }
                    result.set(std::move(name), std::move(value));
                    skipTrivia();
                    if (peek() == ',')
                    {
                        ++offset_;
                        skipTrivia();
                        // Editor-friendly relaxation: a trailing comma before '}' is accepted.
                        if (peek() == '}') { ++offset_; break; }
                        continue;
                    }
                    if (!expect('}')) { return false; }
                    break;
                }
                out = std::move(result);
                return true;
            }

            std::string_view text_;
            std::size_t offset_ = 0;
            std::string error_;
        };

        void writeEscaped(std::string& out, std::string_view text)
        {
            out.push_back('"');
            for (const char character : text)
            {
                switch (character)
                {
                    case '"': out += "\\\""; break;
                    case '\\': out += "\\\\"; break;
                    case '\b': out += "\\b"; break;
                    case '\f': out += "\\f"; break;
                    case '\n': out += "\\n"; break;
                    case '\r': out += "\\r"; break;
                    case '\t': out += "\\t"; break;
                    default:
                        if (static_cast<unsigned char>(character) < 0x20u)
                        {
                            char buffer[7];
                            std::snprintf(buffer, sizeof(buffer), "\\u%04x",
                                          static_cast<unsigned>(static_cast<unsigned char>(character)));
                            out += buffer;
                        }
                        else
                        {
                            out.push_back(character);
                        }
                        break;
                }
            }
            out.push_back('"');
        }

        void writeNumber(std::string& out, double value)
        {
            // Integral values are written without a decimal point so that a scene's entity
            // counts, layer indices and pixel coordinates stay readable in a diff.
            if (std::isfinite(value) && value == std::floor(value) && std::abs(value) < 1e15)
            {
                out += std::to_string(static_cast<long long>(value));
                return;
            }
            char buffer[40];
            std::snprintf(buffer, sizeof(buffer), "%.9g", value);
            out += buffer;
        }

        void writeValue(std::string& out, const JsonValue& value, bool pretty, int depth)
        {
            const std::string indent = pretty ? std::string(static_cast<std::size_t>(depth + 1) * 2, ' ') : std::string{};
            const std::string closingIndent = pretty ? std::string(static_cast<std::size_t>(depth) * 2, ' ') : std::string{};
            const char* separator = pretty ? "\n" : "";
            const char* colon = pretty ? ": " : ":";

            switch (value.getType())
            {
                case JsonType::Null: out += "null"; break;
                case JsonType::Boolean: out += value.asBoolean() ? "true" : "false"; break;
                case JsonType::Number: writeNumber(out, value.asNumber()); break;
                case JsonType::String: writeEscaped(out, value.asString()); break;
                case JsonType::Array: {
                    const auto& elements = value.getElements();
                    if (elements.empty()) { out += "[]"; break; }
                    out += '[';
                    out += separator;
                    for (std::size_t index = 0; index < elements.size(); ++index)
                    {
                        out += indent;
                        writeValue(out, elements[index], pretty, depth + 1);
                        if (index + 1 < elements.size()) { out += ','; }
                        out += separator;
                    }
                    out += closingIndent;
                    out += ']';
                    break;
                }
                case JsonType::Object: {
                    const auto& members = value.getMembers();
                    if (members.empty()) { out += "{}"; break; }
                    out += '{';
                    out += separator;
                    for (std::size_t index = 0; index < members.size(); ++index)
                    {
                        out += indent;
                        writeEscaped(out, members[index].first);
                        out += colon;
                        writeValue(out, members[index].second, pretty, depth + 1);
                        if (index + 1 < members.size()) { out += ','; }
                        out += separator;
                    }
                    out += closingIndent;
                    out += '}';
                    break;
                }
            }
        }
    }

    namespace Json
    {
        JsonParseResult parse(std::string_view text)
        {
            Parser parser{text};
            return parser.run();
        }

        std::string write(const JsonValue& value, bool pretty)
        {
            std::string out;
            writeValue(out, value, pretty, 0);
            if (pretty) { out.push_back('\n'); }
            return out;
        }
    }
}
