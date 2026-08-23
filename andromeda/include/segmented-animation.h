#pragma once

#include <functional>
#include <vector>

#include "animation-frame-base.h"

// Concrete AbstractFrameAnimation that lets an animation be written as an
// ordered list of time-bounded phases - "do this, then this, then this" -
// instead of a blocking loop. This is the one place that tracks "which phase
// am I in": animation authors just call addSegment() once per phase (usually
// from their constructor, in the same order they'd previously have called a
// blocking helper method) and never touch that bookkeeping themselves.
//
// Each segment's function receives the ms elapsed *within that segment*
// (0..duration), not the animation's overall localT, so the per-frame math
// can stay identical to what a time-based blocking loop body already looked
// like (e.g. BaseSweep::colorSweep's `t = millis() - start`).
class SegmentedAnimation : public AbstractFrameAnimation
{
   protected:
    using SegmentFn = std::function<void(milliseconds_t segmentT)>;

    void addSegment(milliseconds_t duration, SegmentFn fn)
    {
        segments.push_back({duration, std::move(fn)});
        totalDuration += duration;
    }

   public:
    bool renderFrame(milliseconds_t localT) override
    {
        if (segments.empty()) return true;

        milliseconds_t elapsed = 0;
        for (size_t i = 0; i < segments.size(); i++)
        {
            const Segment& segment = segments[i];
            bool isLastSegment = (i + 1 == segments.size());
            milliseconds_t segmentEnd = elapsed + segment.duration;

            // Stay on this segment until localT reaches its end, except the
            // last segment, which absorbs everything past the animation's
            // total duration (so a slightly-late call still renders the
            // animation's final frame instead of silently doing nothing).
            if (localT < segmentEnd || isLastSegment)
            {
                milliseconds_t segmentT = (localT > elapsed) ? (localT - elapsed) : 0;
                if (segmentT > segment.duration) segmentT = segment.duration;

                segment.fn(segmentT);
                return localT >= totalDuration;
            }

            elapsed = segmentEnd;
        }

        return true;  // unreachable: the last-segment fallback above always matches
    }

   private:
    struct Segment
    {
        milliseconds_t duration;
        SegmentFn fn;
    };

    std::vector<Segment> segments;
    milliseconds_t totalDuration = 0;
};
