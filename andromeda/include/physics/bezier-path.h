#pragma once

// Independent cubic-Bezier motion for one emitter: not a differential-equation
// problem, so no integrator is used here. Position is a closed-form function of a
// curve parameter s in [0,1]; the "velocity/acceleration" the caller asked to control
// is just an eased profile for how s advances with elapsed time (quintic
// smootherstep), not something requiring RK4 or any other stepper.
//
// When a segment finishes, the next one is generated so its start point/tangent
// exactly matches the previous segment's end point/tangent (C1 continuity), keeping
// the whole indefinite trajectory smooth across joins.

#include <math.h>

#include "geometry/geometry.h"
#include "physics/physics-random.h"
#include "physics/vec2f.h"
#include "utils.h"

class BezierPath
{
   public:
    Vec2f p0, p1, p2, p3;
    milliseconds_t segmentDurationMs;
    milliseconds_t elapsedMs = 0;

    // Keeps curves comfortably inside the render area instead of hugging the edge.
    static constexpr float BOUNDS_MARGIN = 0.85f;
    static constexpr milliseconds_t SEGMENT_MIN_MS = 900;
    static constexpr milliseconds_t SEGMENT_MAX_MS = 2600;

    BezierPath() { resetRandom(); }

    // First segment: no incoming tangent to match, so all 4 control points are free.
    void resetRandom()
    {
        float halfW = GEOMETRY.getScreenHalfWidth() * BOUNDS_MARGIN;
        float halfH = GEOMETRY.getScreenHalfHeight() * BOUNDS_MARGIN;
        p0 = randomPointInBounds(halfW, halfH);
        p1 = randomPointInBounds(halfW, halfH);
        p2 = randomPointInBounds(halfW, halfH);
        p3 = randomPointInBounds(halfW, halfH);
        segmentDurationMs = randomSegmentDuration();
        elapsedMs = 0;
    }

    // Advances elapsed time, rolling over to freshly-generated segments as needed. A
    // `while` (not `if`) correctly handles a large dt (frame hitch) spanning several
    // segments without ever leaving elapsedMs stuck past the end of a segment.
    void step(milliseconds_t dt)
    {
        elapsedMs += dt;
        while (elapsedMs >= segmentDurationMs)
        {
            elapsedMs -= segmentDurationMs;
            generateNextSegment();
        }
    }

    Vec2f position() const { return evaluateCubic(p0, p1, p2, p3, easeQuintic(progress())); }

    // Cubic Bezier in polynomial form (cheaper than de Casteljau, same curve).
    static Vec2f evaluateCubic(Vec2f p0, Vec2f p1, Vec2f p2, Vec2f p3, float s)
    {
        float u = 1.0f - s;
        float uu = u * u;
        float ss = s * s;
        return p0 * (uu * u) + p1 * (3.0f * uu * s) + p2 * (3.0f * u * ss) + p3 * (ss * s);
    }

    // Quintic smootherstep: s(0)=0, s(1)=1, and both the first and second derivatives
    // vanish at both ends, so segment joins are smooth in position, velocity, and
    // acceleration - not just position - independent of the underlying curve geometry.
    static float easeQuintic(float t)
    {
        if (t <= 0.0f) return 0.0f;
        if (t >= 1.0f) return 1.0f;
        return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
    }

   private:
    float progress() const
    {
        if (segmentDurationMs == 0) return 1.0f;
        float p = (float)elapsedMs / (float)segmentDurationMs;
        if (p < 0.0f) return 0.0f;
        if (p > 1.0f) return 1.0f;
        return p;
    }

    // B'(1) = 3(p3-p2): the curve's exact tangent direction at its end point.
    Vec2f endTangent() const { return (p3 - p2) * 3.0f; }

    static milliseconds_t randomSegmentDuration()
    {
        return (milliseconds_t)random(SEGMENT_MIN_MS, SEGMENT_MAX_MS + 1);
    }

    static Vec2f randomPointInBounds(float halfW, float halfH)
    {
        return Vec2f(randomFloat(-halfW, halfW), randomFloat(-halfH, halfH));
    }

    static Vec2f clampToBounds(Vec2f p, float halfW, float halfH)
    {
        return Vec2f(constrain(p.x, -halfW, halfW), constrain(p.y, -halfH, halfH));
    }

    // How far `origin` can move along unit direction `dir` before leaving the box
    // [-halfW,halfW] x [-halfH,halfH] - a simple ray/AABB distance. Returns 0 for the
    // degenerate case where `dir` never reaches a boundary (shouldn't happen for a
    // real unit vector from inside the box, but stays safe rather than returning a
    // negative or huge step).
    static float maxStepToBoundary(Vec2f origin, Vec2f dir, float halfW, float halfH)
    {
        float best = 1e9f;
        if (fabsf(dir.x) > 1e-6f)
        {
            float boundX = dir.x > 0 ? halfW : -halfW;
            float tx = (boundX - origin.x) / dir.x;
            if (tx >= 0.0f) best = fminf(best, tx);
        }
        if (fabsf(dir.y) > 1e-6f)
        {
            float boundY = dir.y > 0 ? halfH : -halfH;
            float ty = (boundY - origin.y) / dir.y;
            if (ty >= 0.0f) best = fminf(best, ty);
        }
        if (best > 1e8f) return 0.0f;
        return best < 0.0f ? 0.0f : best;
    }

    // Generates a new segment whose start point/tangent exactly continue the old
    // curve's end point/tangent (C1 continuity). Clamping the point p1 directly (as
    // opposed to clamping the step length along the tangent ray) would change its
    // direction and break continuity - so the bounds constraint is applied to the
    // step length instead. p2/p3 have no continuity constraint, so they're free.
    // Since a cubic Bezier always lies within the convex hull of its 4 control
    // points, keeping all 4 points in-bounds keeps the whole curve in-bounds.
    void generateNextSegment()
    {
        float halfW = GEOMETRY.getScreenHalfWidth() * BOUNDS_MARGIN;
        float halfH = GEOMETRY.getScreenHalfHeight() * BOUNDS_MARGIN;
        float minDim = fminf(halfW, halfH);

        Vec2f newP0 = p3;
        Vec2f tangentDir = endTangent().normalized();
        if (tangentDir.lengthSquared() < 1e-6f)
        {
            float angle = randomFloat(0.0f, 2.0f * PI);
            tangentDir = Vec2f(cosf(angle), sinf(angle));
        }

        float maxH1 = maxStepToBoundary(newP0, tangentDir, halfW, halfH);
        float h1 = fminf(randomFloat(0.15f, 0.40f) * minDim, maxH1);
        Vec2f newP1 = newP0 + tangentDir * h1;

        Vec2f newP3 = randomPointInBounds(halfW, halfH);

        float angle2 = randomFloat(0.0f, 2.0f * PI);
        float h2 = randomFloat(0.15f, 0.40f) * minDim;
        Vec2f newP2 = clampToBounds(newP3 + Vec2f(cosf(angle2), sinf(angle2)) * h2, halfW, halfH);

        p0 = newP0;
        p1 = newP1;
        p2 = newP2;
        p3 = newP3;
        segmentDurationMs = randomSegmentDuration();
    }
};
