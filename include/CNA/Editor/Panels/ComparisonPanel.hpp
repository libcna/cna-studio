// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Panels/ComparisonPanel.hpp
 * @brief Running the open scene on every installed backend and showing where they differ.
 *
 * plan.md ED-510. This is the panel that turns ANALYSIS.md finding F-01 -- CNA fixes its graphics
 * backend at compile time -- from a constraint into something useful: a game ships on backends its
 * author cannot all run at once, and "does it look the same on each" is otherwise a question
 * answered by shipping and waiting.
 *
 * The panel owns the run and reports it. The sequencing lives in `BackendComparison`, the pixel
 * arithmetic in `ImageDiff`, and the decoding behind the viewport -- so the only thing here is what
 * a user sees and presses.
 */

#include <string>

#include "CNA/Editor/Panels/EditorPanel.hpp"
#include "CNA/Editor/RuntimeBridge/BackendComparison.hpp"

namespace CNA::Editor
{
    /** @brief Drives a `BackendComparison` and shows its result as a table. */
    class ComparisonPanel final : public EditorPanel
    {
    public:
        using EditorPanel::EditorPanel;

        void draw() override;

        /**
         * @brief Advances a running comparison. Called once per frame, never blocks.
         *
         * @param nowSeconds The editor's monotonic clock, passed in like every other clock here so
         *        the timeout is something a test can drive rather than wait for.
         */
        void poll(double nowSeconds) { comparison_.poll(nowSeconds); }

        /**
         * @brief Starts a run over every discovered player build.
         *
         * Public because `--compare-backends` (ED-511) drives it. A command-line harness that took
         * its own path to the same result would be a second implementation to keep honest.
         */
        void startComparison();

        /** @brief Returns the run, so a caller can report on it. */
        [[nodiscard]] const BackendComparison& getComparison() const { return comparison_; }

        /** @brief Sets the per-channel tolerance the next run uses. */
        void setTolerance(int tolerance) { tolerance_ = tolerance; }

    private:
        /** @brief Draws one backend's row. */
        void drawEntry(const ComparisonEntry& entry);

        BackendComparison comparison_;

        /** @brief The last run's summary line, kept after the run so it can be read. */
        std::string summary_;

        int tolerance_ = kDefaultImageTolerance;
    };
}
