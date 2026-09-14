// SPDX-License-Identifier: MS-PL
/**
 * @file TestMain.cpp
 * @brief Runs every registered test case. See TestHarness.hpp for why this is not GoogleTest.
 */

#include "TestHarness.hpp"

int main()
{
    std::cout << "cna-editor tests\n\n";
    return CnaEditorTest::runAll();
}
