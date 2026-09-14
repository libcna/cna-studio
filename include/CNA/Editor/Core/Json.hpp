// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Core/Json.hpp
 * @brief Minimal, dependency-free JSON document model used by every editor file format.
 *
 * The `.cnaproject`, `.cnascene` and `.cnaasset` formats are all JSON (see docs/FORMATS.md for
 * why). This is deliberately a small, self-contained implementation rather than a third-party
 * library so that cna-editor-core has **no external dependencies at all** and stays buildable and
 * unit-testable on its own -- see ANALYSIS.md decision D-12. Object member order is preserved on
 * both read and write, which is what keeps saved scenes diff-stable in git.
 */

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace CNA::Editor
{
    class JsonValue;

    /** @brief Discriminates the alternatives a JsonValue can hold. */
    enum class JsonType
    {
        Null,
        Boolean,
        Number,
        String,
        Array,
        Object
    };

    /**
     * @brief An immutable-by-convention JSON value.
     *
     * Accessors never throw: asking for the wrong alternative returns the supplied fallback. That
     * suits a document loader far better than exceptions, because a scene file with one bad field
     * should still load with that one field defaulted rather than failing wholesale.
     */
    class JsonValue
    {
    public:
        using Array = std::vector<JsonValue>;
        /** @brief Ordered object members. Insertion order is preserved so saves stay diff-stable. */
        using Object = std::vector<std::pair<std::string, JsonValue>>;

        JsonValue() = default;
        JsonValue(std::nullptr_t) {}
        JsonValue(bool value) : type_(JsonType::Boolean), boolean_(value) {}
        JsonValue(double value) : type_(JsonType::Number), number_(value) {}
        JsonValue(int value) : type_(JsonType::Number), number_(static_cast<double>(value)) {}
        JsonValue(std::int64_t value) : type_(JsonType::Number), number_(static_cast<double>(value)) {}
        JsonValue(std::string value) : type_(JsonType::String), string_(std::move(value)) {}
        JsonValue(const char* value) : type_(JsonType::String), string_(value ? value : "") {}
        JsonValue(Array value) : type_(JsonType::Array), array_(std::move(value)) {}
        JsonValue(Object value) : type_(JsonType::Object), object_(std::move(value)) {}

        [[nodiscard]] JsonType getType() const { return type_; }
        [[nodiscard]] bool isNull() const { return type_ == JsonType::Null; }
        [[nodiscard]] bool isArray() const { return type_ == JsonType::Array; }
        [[nodiscard]] bool isObject() const { return type_ == JsonType::Object; }

        [[nodiscard]] bool asBoolean(bool fallback = false) const;
        [[nodiscard]] double asNumber(double fallback = 0.0) const;
        [[nodiscard]] float asFloat(float fallback = 0.0f) const;
        [[nodiscard]] int asInt(int fallback = 0) const;
        [[nodiscard]] std::string asString(std::string fallback = {}) const;

        /** @brief Returns the elements when this is an array, otherwise an empty span. */
        [[nodiscard]] const Array& getElements() const;
        /** @brief Returns the members when this is an object, otherwise an empty span. */
        [[nodiscard]] const Object& getMembers() const;

        /**
         * @brief Looks up an object member by name.
         * @return The member value, or a null JsonValue when absent or when this is not an object.
         */
        [[nodiscard]] const JsonValue& operator[](std::string_view name) const;

        /** @brief Returns true when this is an object that has a member named @p name. */
        [[nodiscard]] bool contains(std::string_view name) const;

        /** @brief Appends or replaces an object member, converting this value to an object first. */
        void set(std::string name, JsonValue value);

        /**
         * @brief Removes @p name from an object. Returns false when it was not there.
         *
         * Needed so that undoing a setting the user added can take it back out, rather than
         * writing a default in its place -- a file that accumulated every field anyone ever
         * glanced at would make its every diff noise.
         */
        bool remove(std::string_view name);

        /** @brief Appends an array element, converting this value to an array first. */
        void append(JsonValue value);

        /** @brief Constructs an empty object. */
        static JsonValue makeObject() { return JsonValue{Object{}}; }
        /** @brief Constructs an empty array. */
        static JsonValue makeArray() { return JsonValue{Array{}}; }

    private:
        JsonType type_ = JsonType::Null;
        bool boolean_ = false;
        double number_ = 0.0;
        std::string string_;
        Array array_;
        Object object_;
    };

    /** @brief Outcome of Json::parse -- either a value, or a message and the offending offset. */
    struct JsonParseResult
    {
        bool succeeded = false;
        JsonValue value;
        std::string errorMessage;
        std::size_t errorOffset = 0;
    };

    namespace Json
    {
        /**
         * @brief Parses a complete JSON document.
         *
         * Accepts the strict JSON grammar plus two editor-friendly relaxations that make
         * hand-edited project files less annoying: `//` line comments and trailing commas.
         *
         * @param text The document text.
         * @return The parsed value, or a failure result carrying a human-readable message.
         */
        JsonParseResult parse(std::string_view text);

        /**
         * @brief Serialises a value.
         * @param value The value to write.
         * @param pretty When true, emits two-space indentation and one member per line -- the form
         *        every editor-written file uses, because these files are meant to be diffed.
         */
        std::string write(const JsonValue& value, bool pretty = true);
    }
}
