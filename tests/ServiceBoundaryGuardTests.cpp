// SPDX-License-Identifier: MS-PL
/**
 * @file ServiceBoundaryGuardTests.cpp
 * @brief The rule that keeps the shell decomposition from becoming a set of classes that still
 *        talk through globals.
 *
 * `plan.md` STUDIO-02050, STUDIO-02059, and `docs/ARCHITECTURE.md` §10.
 *
 * ### The rule
 *
 * **A service's dependencies are its constructor arguments.** The set of things it can reach is
 * the set visible in its constructor -- which is what makes `StudioPlayService` testable with two
 * doubles and a lambda, and what a service locator destroys one `get<T>()` at a time.
 *
 * That rule is worth exactly as much as its enforcement. `StudioShellPanels` grew to 1421 lines by
 * a sequence of individually reasonable additions, and a locator would arrive the same way: the
 * first `instance()` is always the one that saves a constructor argument in a hurry, and by the
 * time there are six of them the dependency graph is invisible and every test needs the whole
 * application. A locator added later looks exactly like the code around it, so review does not
 * catch it. This does.
 *
 * ### What it looks for, and why those shapes
 *
 * Every locator and every singleton is built from one of four things, and each is a *structural*
 * property rather than a naming convention -- renaming `instance()` to `shared()` evades nothing:
 *
 * 1. **A mutable `static` holding a Studio type.** The storage every singleton needs, whether it
 *    is a function-local Meyers static, a class static data member or a file-scope one.
 * 2. **A `static` function handing out a reference or pointer to a Studio type.** The accessor.
 *    Returning *by value* is a factory (`Uuid::generate`, `Project::createDefault`) and is fine:
 *    the caller gets a thing, not the thing.
 * 3. **A mutable namespace-scope variable of a Studio type.** The same storage without the
 *    keyword, which is what an anonymous namespace in a `.cpp` provides.
 * 4. **A `static` template function returning `T&`.** `Locator::get<T>()` has no other shape, and
 *    rules 1-3 miss it because `T` is not the name of anything.
 *
 * `std::type_index` and `typeid` are checked too: a heterogeneous registry keyed on type is the
 * canonical locator implementation, and Studio has no other use for runtime type identity.
 *
 * ### Why "a Studio type" rather than "anything"
 *
 * A guard that banned every mutable static would fire on a CRC lookup table and a thread-local
 * random engine -- neither of which is shared mutable state that anything depends on, both of
 * which are caches of pure functions. Making those failures would teach people to switch the
 * guard off, which is the only way a guard actually dies. The discriminator is whether the static
 * holds one of *Studio's own types*: a `std::array<std::uint32_t, 256>` of CRC constants is a
 * table, and a `StudioBuildService` reached without being passed one is a dependency nobody
 * declared.
 *
 * ### The check is proved against fixtures
 *
 * An ordering or absence assertion that has never been shown to fail is an assertion about
 * nothing. `TheGuardSeesEveryShapeOfLocatorItClaimsTo` feeds the scanner each prohibited shape and
 * requires it to be flagged, and `TheGuardDoesNotFireOnTheThingsThatLookLikeOne` feeds it the
 * by-value factories, const tables and plain members that surround the real code and requires
 * silence. Neither needs a violation to be committed to prove the guard works.
 */

#include "SourceScan.hpp"
#include "TestHarness.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using CnaStudioTest::Scan::SourceFile;
    using CnaStudioTest::Scan::collectSources;
    using CnaStudioTest::Scan::lineOf;
    using CnaStudioTest::Scan::stripCommentsAndStrings;

    /** @brief Which rule a violation broke, so the failure message can say what to do instead. */
    enum class LocatorRule
    {
        StaticStore,        ///< A mutable `static` holding a Studio type.
        StaticAccessor,     ///< A `static` function returning a Studio reference or pointer.
        NamespaceGlobal,    ///< A mutable namespace-scope variable of a Studio type.
        TemplateAccessor,   ///< `static T& get()` -- the locator's own signature.
        RuntimeTypeKey      ///< `typeid` / `std::type_index`, which key a heterogeneous registry.
    };

    /** @brief One prohibited construct, with enough detail to fix it without a search. */
    struct LocatorViolation
    {
        LocatorRule rule = LocatorRule::StaticStore;
        int line = 0;
        std::string text;   ///< The declaration as it was read, normalised to single spaces.
    };

    /** @brief What each rule means, and what to write instead. */
    std::string explain(LocatorRule rule)
    {
        switch (rule)
        {
            case LocatorRule::StaticStore:
                return "a mutable 'static' holding a Studio type is the storage a singleton needs. "
                       "Pass the thing in as a constructor argument instead.";
            case LocatorRule::StaticAccessor:
                return "a 'static' function handing out a reference or pointer to a Studio type is "
                       "a singleton accessor. Returning by value would be a factory and is fine; "
                       "handing out *the* instance is not.";
            case LocatorRule::NamespaceGlobal:
                return "a mutable namespace-scope variable of a Studio type is a global. Whatever "
                       "reads it has a dependency its constructor does not declare.";
            case LocatorRule::TemplateAccessor:
                return "a 'static' template function returning 'T&' is a service locator. The set "
                       "of things a service can reach must be the set visible in its constructor.";
            case LocatorRule::RuntimeTypeKey:
                return "runtime type identity keys a heterogeneous registry, which is a locator "
                       "with an extra step. Studio has no other use for it.";
        }
        return {};
    }

    /** @brief Whether @p c can appear inside an identifier. */
    bool isWordChar(char c)
    {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
    }

    /**
     * @brief Blanks preprocessor directives, keeping newlines.
     *
     * A `#define` carrying a brace or a semicolon would desynchronise the scope stack for the rest
     * of the file, and every violation after it would be reported against the wrong scope.
     */
    std::string stripPreprocessor(std::string_view code)
    {
        std::string out;
        out.reserve(code.size());
        std::size_t i = 0;
        while (i < code.size())
        {
            std::size_t lineEnd = code.find('\n', i);
            if (lineEnd == std::string_view::npos) { lineEnd = code.size(); }

            std::size_t firstNonSpace = i;
            while (firstNonSpace < lineEnd
                   && std::isspace(static_cast<unsigned char>(code[firstNonSpace])) != 0)
            {
                ++firstNonSpace;
            }

            bool directive = firstNonSpace < lineEnd && code[firstNonSpace] == '#';
            while (true)
            {
                const std::string_view line = code.substr(i, lineEnd - i);
                out.append(directive ? std::string(line.size(), ' ') : std::string{line});
                if (lineEnd >= code.size()) { i = code.size(); break; }
                out += '\n';
                i = lineEnd + 1;
                // A continued directive swallows the next line too.
                const bool continued = !line.empty() && line.back() == '\\';
                if (!(directive && continued)) { break; }
                lineEnd = code.find('\n', i);
                if (lineEnd == std::string_view::npos) { lineEnd = code.size(); }
            }
        }
        return out;
    }

    /** @brief One lexical token: an identifier, a number, or a single punctuation character. */
    struct Token
    {
        std::string text;
        std::size_t offset = 0;
    };

    /** @brief Splits stripped code into identifiers and single punctuation characters. */
    std::vector<Token> tokenize(std::string_view code)
    {
        std::vector<Token> tokens;
        std::size_t i = 0;
        while (i < code.size())
        {
            const char c = code[i];
            if (std::isspace(static_cast<unsigned char>(c)) != 0) { ++i; continue; }
            if (isWordChar(c))
            {
                const std::size_t start = i;
                while (i < code.size() && isWordChar(code[i])) { ++i; }
                tokens.push_back(Token{std::string{code.substr(start, i - start)}, start});
                continue;
            }
            tokens.push_back(Token{std::string(1, c), i});
            ++i;
        }
        return tokens;
    }

    /** @brief What opened a brace, which is what says whether a declaration is at namespace scope. */
    enum class ScopeKind { Namespace, Class, Function, Control, Init, Block };

    /** @brief An open brace and the statement that was being read when it opened. */
    struct OpenScope
    {
        ScopeKind kind = ScopeKind::Block;
        std::vector<Token> saved;
    };

    /** @brief Keywords that begin a statement no declaration can hide behind. */
    bool isControlKeyword(const std::string& word)
    {
        static const std::set<std::string> kControl{
            "if", "else", "for", "while", "do", "switch", "try", "catch", "return"};
        return kControl.count(word) != 0;
    }

    /** @brief Decides what a `{` is opening from the statement that preceded it. */
    ScopeKind classifyBrace(const std::vector<Token>& statement)
    {
        if (statement.empty()) { return ScopeKind::Block; }

        // `template <...>` prefixes a class or a function and says nothing about which.
        std::size_t first = 0;
        if (statement[first].text == "template")
        {
            int angle = 0;
            std::size_t i = first + 1;
            for (; i < statement.size(); ++i)
            {
                if (statement[i].text == "<") { ++angle; }
                else if (statement[i].text == ">") { if (--angle == 0) { ++i; break; } }
            }
            first = i;
        }
        if (first >= statement.size()) { return ScopeKind::Block; }

        const std::string& head = statement[first].text;
        if (head == "namespace") { return ScopeKind::Namespace; }
        if (head == "class" || head == "struct" || head == "union" || head == "enum")
        {
            return ScopeKind::Class;
        }
        if (isControlKeyword(head)) { return ScopeKind::Control; }

        // A function body, a constructor's initialiser list, or a lambda: the token before the
        // brace closes a parameter list, or is a specifier that may follow one.
        const std::string& last = statement.back().text;
        if (last == ")" || last == "const" || last == "noexcept" || last == "override"
            || last == "final" || last == "&" || last == "&&")
        {
            const bool hasParens = std::any_of(statement.begin(), statement.end(),
                                               [](const Token& t) { return t.text == "("; });
            if (hasParens) { return ScopeKind::Function; }
        }
        if (last == ":") { return ScopeKind::Function; }   // a constructor initialiser list

        return ScopeKind::Init;   // `Foo f{...}` or `= {...}`: the declaration continues after it
    }

    /** @brief Renders a statement back to readable text for the failure message. */
    std::string render(const std::vector<Token>& statement)
    {
        std::string out;
        for (const Token& token : statement)
        {
            if (!out.empty() && isWordChar(out.back()) && isWordChar(token.text.front()))
            {
                out += ' ';
            }
            out += token.text;
            if (out.size() > 160) { out += " ..."; break; }
        }
        return out;
    }

    /**
     * @brief Finds every construct through which one part of Studio could reach another without
     *        being handed it.
     *
     * Pure over its inputs, so the fixtures below exercise exactly the code the tree is scanned
     * with. A guard whose test path differs from its production path proves nothing about the
     * production path.
     *
     * @param rawCode Source text, comments and strings included -- they are stripped here.
     * @param studioTypes The unqualified names of the types Studio declares.
     * @return Every violation, in source order.
     */
    std::vector<LocatorViolation> findLocatorViolations(std::string_view rawCode,
                                                        const std::set<std::string>& studioTypes)
    {
        std::vector<LocatorViolation> found;
        const std::string code = stripPreprocessor(stripCommentsAndStrings(rawCode));
        const std::vector<Token> tokens = tokenize(code);

        std::vector<OpenScope> scopes;
        std::vector<Token> statement;

        const auto atNamespaceScope = [&scopes] {
            return std::all_of(scopes.begin(), scopes.end(),
                               [](const OpenScope& s) { return s.kind == ScopeKind::Namespace; });
        };

        const auto isStudioType = [&studioTypes](const std::string& word) {
            return studioTypes.count(word) != 0;
        };

        /** Classifies one complete `;`-terminated statement. */
        const auto inspect = [&](const std::vector<Token>& s) {
            if (s.empty()) { return; }

            // A class member's access specifier is part of the previous statement, because it
            // has no semicolon of its own: `public : static T & get ( )`. Dropping it is what lets
            // everything below index from the declaration.
            std::size_t declStart = 0;
            while (declStart + 1 < s.size()
                   && (s[declStart].text == "public" || s[declStart].text == "private"
                       || s[declStart].text == "protected")
                   && s[declStart + 1].text == ":")
            {
                declStart += 2;
            }
            if (declStart >= s.size()) { return; }

            // Names introduced by this statement's own `template <...>`, so `T&` is recognisable.
            std::set<std::string> templateParameters;
            if (s[declStart].text == "template")
            {
                int angle = 0;
                std::size_t i = declStart + 1;
                for (; i < s.size(); ++i)
                {
                    if (s[i].text == "<") { ++angle; continue; }
                    if (s[i].text == ">") { if (--angle == 0) { ++i; break; } continue; }
                    if (angle == 1 && (s[i - 1].text == "typename" || s[i - 1].text == "class"))
                    {
                        templateParameters.insert(s[i].text);
                    }
                }
                declStart = i;
            }
            if (declStart >= s.size()) { return; }

            const auto has = [&](const char* word) {
                return std::any_of(s.begin() + static_cast<std::ptrdiff_t>(declStart), s.end(),
                                   [word](const Token& t) { return t.text == word; });
            };

            const std::string& head = s[declStart].text;
            if (head == "using" || head == "typedef" || head == "friend" || head == "namespace"
                || head == "class" || head == "struct" || head == "union" || head == "enum"
                || isControlKeyword(head))
            {
                return;   // a declaration of a name, not of a thing that holds state
            }

            if (has("typeid") || has("type_index"))
            {
                found.push_back({LocatorRule::RuntimeTypeKey, lineOf(code, s[0].offset), render(s)});
                return;
            }

            // Where the declarator's parameter list begins, if this declares a function. Only a
            // `(` that comes before any top-level `=` counts: `static Table t = build();` has a
            // parenthesis and is not a function.
            std::size_t parenAt = s.size();
            bool operatorDeclarator = false;
            {
                int depth = 0;
                for (std::size_t i = declStart; i < s.size(); ++i)
                {
                    const std::string& t = s[i].text;
                    if (t == "[" || t == "{") { ++depth; continue; }
                    if (t == "]" || t == "}") { --depth; continue; }
                    if (depth != 0) { continue; }
                    // `operator=` carries an `=` that is part of its name, not an initialiser, and
                    // its declarator ends in punctuation rather than a word. Read either as an
                    // ordinary declaration, every out-of-line `operator=` in Studio becomes a
                    // namespace-scope global of the type it returns.
                    if (t == "operator")
                    {
                        operatorDeclarator = true;
                        while (i + 1 < s.size() && s[i + 1].text != "(") { ++i; }
                        continue;
                    }
                    if (t == "=") { break; }
                    if (t == "(") { parenAt = i; break; }
                }
            }
            const bool declaresFunction = parenAt < s.size() && parenAt > declStart
                                          && (operatorDeclarator
                                              || isWordChar(s[parenAt - 1].text.front()));

            const bool isStatic = has("static") || has("thread_local");
            const bool isImmutable = has("const") || has("constexpr") || has("consteval");

            // The type portion: everything up to the declarator's own name.
            const std::size_t typeEnd = declaresFunction ? parenAt - 1 : s.size();

            if (declaresFunction && isStatic)
            {
                for (std::size_t i = declStart; i + 1 < typeEnd; ++i)
                {
                    const bool handedOut = s[i + 1].text == "&" || s[i + 1].text == "*";
                    if (!handedOut) { continue; }
                    if (isStudioType(s[i].text))
                    {
                        found.push_back({LocatorRule::StaticAccessor,
                                         lineOf(code, s[0].offset), render(s)});
                        return;
                    }
                    if (templateParameters.count(s[i].text) != 0)
                    {
                        found.push_back({LocatorRule::TemplateAccessor,
                                         lineOf(code, s[0].offset), render(s)});
                        return;
                    }
                }
                return;   // a static function that hands out nothing of Studio's is fine
            }
            if (declaresFunction) { return; }

            if (isImmutable) { return; }   // a constant is not shared *mutable* state

            const bool holdsStudioType =
                std::any_of(s.begin() + static_cast<std::ptrdiff_t>(declStart),
                            s.begin() + static_cast<std::ptrdiff_t>(typeEnd),
                            [&](const Token& t) { return isStudioType(t.text); });
            if (!holdsStudioType) { return; }

            if (isStatic)
            {
                found.push_back({LocatorRule::StaticStore, lineOf(code, s[0].offset), render(s)});
                return;
            }
            if (atNamespaceScope())
            {
                found.push_back({LocatorRule::NamespaceGlobal, lineOf(code, s[0].offset), render(s)});
            }
        };

        for (const Token& token : tokens)
        {
            if (token.text == "{")
            {
                const ScopeKind kind = classifyBrace(statement);
                OpenScope scope;
                scope.kind = kind;
                if (kind == ScopeKind::Init) { scope.saved = statement; }
                scopes.push_back(std::move(scope));
                statement.clear();
                continue;
            }
            if (token.text == "}")
            {
                if (!scopes.empty())
                {
                    // A braced initialiser interrupts a declaration rather than ending it, so the
                    // statement resumes: `static Registry r{...};` must still be seen as static.
                    statement = (scopes.back().kind == ScopeKind::Init) ? scopes.back().saved
                                                                        : std::vector<Token>{};
                    scopes.pop_back();
                }
                else
                {
                    statement.clear();
                }
                continue;
            }
            if (token.text == ";")
            {
                inspect(statement);
                statement.clear();
                continue;
            }
            statement.push_back(token);
        }
        return found;
    }

    /**
     * @brief The unqualified names of every type Studio *publishes* in a header.
     *
     * Read from the source rather than listed, so a service added tomorrow is covered without
     * anyone remembering to add it here -- which is the failure mode of every hand-written
     * vocabulary.
     *
     * **Headers only, deliberately.** A type declared inside one `.cpp`'s anonymous namespace
     * cannot be the thing one part of Studio reaches another through, because nothing outside that
     * file can name it. Including them buys no coverage and costs real false positives: the
     * Winsock initialisation guard in `MessageChannel.cpp` is a file-local RAII type held in a
     * function static, which is process-wide library setup rather than a dependency anybody has.
     * Failing that would teach people to switch the guard off, which is the only way a guard dies.
     */
    const std::set<std::string>& studioTypeNames()
    {
        static const std::set<std::string> names = [] {
            std::set<std::string> collected;
            for (const SourceFile& file : collectSources({"include", "src"}))
            {
                const std::string extension = file.path.extension().string();
                if (extension != ".hpp" && extension != ".h") { continue; }
                const std::string code = stripCommentsAndStrings(file.text);
                for (const char* keyword : {"class ", "struct ", "enum class "})
                {
                    std::size_t at = code.find(keyword);
                    while (at != std::string::npos)
                    {
                        const bool startsWord = at == 0 || !isWordChar(code[at - 1]);
                        std::size_t i = at + std::strlen(keyword);
                        while (i < code.size()
                               && std::isspace(static_cast<unsigned char>(code[i])) != 0)
                        {
                            ++i;
                        }
                        const std::size_t nameStart = i;
                        while (i < code.size() && isWordChar(code[i])) { ++i; }
                        if (startsWord && i > nameStart)
                        {
                            collected.insert(code.substr(nameStart, i - nameStart));
                        }
                        at = code.find(keyword, at + 1);
                    }
                }
            }
            return collected;
        }();
        return names;
    }

    /**
     * @brief A construct that breaks the rule and is staying until the code holding it is deleted.
     *
     * Not a pattern and not a directory: one exact site, with the task that removes it. The list is
     * checked in both directions -- an entry that no longer matches anything fails the suite, so an
     * exception cannot outlive the thing it excused.
     */
    struct AcceptedException
    {
        std::string file;
        int line = 0;
        std::string declaration;
        std::string reason;
    };

    const std::vector<AcceptedException>& acceptedExceptions()
    {
        // Empty: the Dear ImGui clipboard adapter this once excused (src/ui/imgui/ImGuiStudioUi.cpp)
        // was deleted with the rest of the prototype by STUDIO-07030, and no other site in the tree
        // has needed an exception since. Add an entry here only for a specific site that cannot be
        // fixed yet, never for a whole file or directory.
        static const std::vector<AcceptedException> accepted{};
        return accepted;
    }

    /** @brief Whether @p violation in @p file is one of the recorded exceptions. */
    bool isAccepted(const std::string& file, const LocatorViolation& violation)
    {
        return std::any_of(acceptedExceptions().begin(), acceptedExceptions().end(),
                           [&](const AcceptedException& accepted) {
                               return accepted.file == file
                                      && violation.text.find(accepted.declaration) != std::string::npos;
                           });
    }
} // namespace

CNA_STUDIO_TEST(TheServiceBoundaryScanCanActuallySeeTheSourceTree)
{
    // Every check below passes by finding nothing, so the scan finding nothing because it was
    // pointed at an empty directory would make all of them vacuous.
    CNA_STUDIO_EXPECT(collectSources({"src"}).size() > 40);
    CNA_STUDIO_EXPECT(studioTypeNames().count("StudioPlayService") == 1);
    CNA_STUDIO_EXPECT(studioTypeNames().count("StudioBuildService") == 1);
    CNA_STUDIO_EXPECT(studioTypeNames().count("StudioShell") == 1);
    CNA_STUDIO_EXPECT(studioTypeNames().size() > 150);
}

CNA_STUDIO_TEST(NoServiceReachesAnotherThroughALocatorOrASingleton)
{
    std::size_t violations = 0;
    for (const SourceFile& file : collectSources({"src", "include"}))
    {
        for (const LocatorViolation& violation : findLocatorViolations(file.text, studioTypeNames()))
        {
            if (isAccepted(file.relativePath, violation)) { continue; }
            ++violations;
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                file.relativePath + ":" + std::to_string(violation.line) + ": '" + violation.text
                + "' -- " + explain(violation.rule)
                + " (plan.md STUDIO-02059; docs/ARCHITECTURE.md §10)");
        }
    }
    CNA_STUDIO_EXPECT_EQ(violations, std::size_t{0});
}

CNA_STUDIO_TEST(EveryRecordedExceptionIsStillARealViolation)
{
    // An exception outliving the code it excused is how an allowlist becomes a licence. When
    // STUDIO-07030 deletes the Dear ImGui path this fails, which is the point: the entry has to go
    // with it rather than sit here excusing a file that no longer exists.
    for (const AcceptedException& accepted : acceptedExceptions())
    {
        bool matched = false;
        for (const SourceFile& file : collectSources({"src", "include"}))
        {
            if (file.relativePath != accepted.file) { continue; }
            for (const LocatorViolation& violation :
                 findLocatorViolations(file.text, studioTypeNames()))
            {
                if (violation.text.find(accepted.declaration) != std::string::npos) { matched = true; }
            }
        }
        if (!matched)
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                "the recorded exception for '" + accepted.declaration + "' in " + accepted.file
                + " no longer matches anything. Delete the entry: " + accepted.reason);
        }
        CNA_STUDIO_EXPECT(matched);
    }
}

CNA_STUDIO_TEST(TheGuardSeesEveryShapeOfLocatorItClaimsTo)
{
    // The guard is pointed at source that does not exist in the tree, so each prohibited shape is
    // proved detectable without one being committed. An absence assertion never shown to fail is
    // an assertion about nothing -- which is the lesson STUDIO-35063 was written from.
    const std::set<std::string> types{"StudioBuildService", "StudioShell", "StudioContext"};

    const auto onlyRule = [&types](std::string_view source, LocatorRule expected) {
        const std::vector<LocatorViolation> found = findLocatorViolations(source, types);
        CNA_STUDIO_EXPECT_EQ(found.size(), std::size_t{1});
        if (found.size() == 1) { CNA_STUDIO_EXPECT(found.front().rule == expected); }
    };

    // The Meyers singleton, which is what `instance()` is made of whatever it is called.
    onlyRule(R"(namespace CNA::Studio {
        StudioBuildService& builds() { static StudioBuildService service; return service; }
    })", LocatorRule::StaticStore);

    // The same thing with the storage moved out of the accessor.
    onlyRule(R"(namespace CNA::Studio {
        class Locator { public: static StudioShell& shell(); };
    })", LocatorRule::StaticAccessor);

    // A pointer rather than a reference changes nothing about the dependency it hides.
    onlyRule(R"(namespace CNA::Studio {
        class Locator { public: static StudioContext* context(); };
    })", LocatorRule::StaticAccessor);

    // A file-scope global in an anonymous namespace: the storage without the keyword.
    onlyRule(R"(namespace { CNA::Studio::StudioShell* gShell = nullptr; })",
             LocatorRule::NamespaceGlobal);

    // The generic locator, whose return type names nothing the scan could otherwise recognise.
    onlyRule(R"(namespace CNA::Studio {
        class Services { public: template <typename T> static T& get(); };
    })", LocatorRule::TemplateAccessor);

    // A registry keyed on runtime type identity is a locator with an extra step.
    onlyRule(R"(namespace CNA::Studio {
        class Services { std::map<std::type_index, void*> entries; };
    })", LocatorRule::RuntimeTypeKey);

    // A class static data member is storage with a different spelling.
    onlyRule(R"(namespace CNA::Studio {
        class Shell { static StudioBuildService sInstance; };
    })", LocatorRule::StaticStore);

    // A braced initialiser must not hide the `static` in front of it.
    onlyRule(R"(namespace CNA::Studio {
        void f() { static StudioBuildService service{nullptr, nullptr}; }
    })", LocatorRule::StaticStore);
}

CNA_STUDIO_TEST(TheGuardDoesNotFireOnTheThingsThatLookLikeOne)
{
    // A guard that cries wolf gets switched off, and the shapes below are the ones that surround
    // the real code: by-value factories, constants, ordinary members and parameters.
    const std::set<std::string> types{"StudioBuildService", "StudioShell", "StudioContext",
                                      "StudioMessage", "StudioColor", "Uuid"};

    const auto silent = [&types](std::string_view source) {
        const std::vector<LocatorViolation> found = findLocatorViolations(source, types);
        CNA_STUDIO_EXPECT_EQ(found.size(), std::size_t{0});
        for (const LocatorViolation& violation : found)
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                "false positive on line " + std::to_string(violation.line) + ": " + violation.text);
        }
    };

    // A factory hands out a value. The caller gets a thing, not *the* thing.
    silent(R"(namespace CNA::Studio { class Uuid { public: static Uuid generate(); }; })");
    silent(R"(namespace CNA::Studio { class M { public: static StudioMessage makeHello(const std::string& r); }; })");

    // A static function taking a Studio reference is being handed one, which is the rule working.
    silent(R"(namespace CNA::Studio { class P { static void draw(StudioShell& shell, StudioContext* c); }; })");

    // Constants are not shared mutable state.
    silent(R"(namespace CNA::Studio { constexpr StudioColor kAccent{1, 2, 3}; })");
    silent(R"(namespace CNA::Studio { void f() { static const StudioColor table[2]{}; } })");

    // A member is reached through the object that owns it.
    silent(R"(namespace CNA::Studio { class Panel { StudioContext& context_; StudioShell* shell_; }; })");

    // A local is reached through the frame that made it.
    silent(R"(namespace CNA::Studio { void f() { StudioContext context; StudioShell shell{context}; } })");

    // A constructor argument is the whole point.
    silent(R"(namespace CNA::Studio {
        class Service { public: Service(StudioContext& context) : context_(context) {} private: StudioContext& context_; };
    })");

    // Caches of pure functions: neither holds a Studio type, and banning them is what makes a
    // guard get switched off.
    silent(R"(namespace { std::uint32_t crc(const std::uint8_t* d) { static std::array<std::uint32_t, 256> table{}; static bool built = false; return 0; } })");
    silent(R"(namespace { std::mt19937_64& engine() { static thread_local std::mt19937_64 e; return e; } })");

    // A member function returning the service it owns is a forwarder, not an accessor: it is
    // reached through an instance somebody was given.
    silent(R"(namespace CNA::Studio { class Panels { public: StudioBuildService& build() { return build_; } private: StudioBuildService build_; }; })");

    // An out-of-line `operator=` declares a function whose name ends in punctuation and carries an
    // `=` of its own. Both trip the scan for where a declaration's type ends.
    silent(R"(namespace CNA::Studio { StudioShell& StudioShell::operator=(StudioShell&&) noexcept = default; })");
    silent(R"(namespace CNA::Studio { StudioContext* StudioShell::operator->() const; })");
}
