// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file TestHarness.hpp
 * @brief A tiny assertion harness, so the test suite needs no third-party dependency.
 *
 * The editor core deliberately has zero external dependencies (ANALYSIS.md decision D-12), and a
 * test framework would be the first one to sneak back in. This is about eighty lines and covers
 * what these tests need: named cases, assertions with file and line, and a non-zero exit code on
 * failure so CTest notices.
 *
 * When the suite outgrows this -- parameterised cases, fixtures, death tests -- adopting GoogleTest
 * is a contained change, because nothing outside this header knows how assertions are spelled.
 */

#include <cstdlib>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace CnaEditorTest
{
    /** @brief One registered test case. */
    struct TestCase
    {
        std::string name;
        std::function<void()> body;
    };

    /** @brief Returns the mutable global registry. */
    inline std::vector<TestCase>& registry()
    {
        static std::vector<TestCase> cases;
        return cases;
    }

    /** @brief Returns the failure count for the case currently running. */
    inline int& currentFailures()
    {
        static int failures = 0;
        return failures;
    }

    /** @brief Registers a case at static-initialisation time. */
    struct Registrar
    {
        Registrar(std::string name, std::function<void()> body)
        {
            registry().push_back(TestCase{std::move(name), std::move(body)});
        }
    };

    /** @brief Records a failure with its source location. */
    inline void reportFailure(const char* file, int line, const std::string& message)
    {
        ++currentFailures();
        std::cerr << "    FAIL " << file << ":" << line << "  " << message << "\n";
    }

    /** @brief Runs every registered case; returns 0 when all pass. */
    inline int runAll()
    {
        int failedCases = 0;
        for (const TestCase& testCase : registry())
        {
            currentFailures() = 0;
            std::cout << "  " << testCase.name << "\n";
            try
            {
                testCase.body();
            }
            catch (const std::exception& exception)
            {
                reportFailure(__FILE__, __LINE__, std::string{"threw: "} + exception.what());
            }
            catch (...)
            {
                reportFailure(__FILE__, __LINE__, "threw an unknown exception");
            }
            if (currentFailures() > 0) { ++failedCases; }
        }

        std::cout << "\n"
                  << registry().size() - static_cast<std::size_t>(failedCases) << " passed, "
                  << failedCases << " failed, " << registry().size() << " total\n";
        return failedCases == 0 ? 0 : 1;
    }
}

/** @brief Defines and registers a test case. */
#define CNA_EDITOR_TEST(name)                                                                      \
    static void name();                                                                            \
    static const ::CnaEditorTest::Registrar registrar_##name{#name, name};                         \
    static void name()

/** @brief Fails the current case unless @p condition holds. */
#define CNA_EDITOR_EXPECT(condition)                                                               \
    do {                                                                                           \
        if (!(condition))                                                                          \
        {                                                                                          \
            ::CnaEditorTest::reportFailure(__FILE__, __LINE__, "expected: " #condition);           \
        }                                                                                          \
    } while (false)

/** @brief Fails the current case unless @p actual equals @p expected, printing both. */
#define CNA_EDITOR_EXPECT_EQ(actual, expected)                                                     \
    do {                                                                                           \
        const auto& actualValue = (actual);                                                        \
        const auto& expectedValue = (expected);                                                    \
        if (!(actualValue == expectedValue))                                                       \
        {                                                                                          \
            std::ostringstream message;                                                            \
            message << #actual " == " #expected " -- got " << actualValue                          \
                    << ", expected " << expectedValue;                                             \
            ::CnaEditorTest::reportFailure(__FILE__, __LINE__, message.str());                     \
        }                                                                                          \
    } while (false)
