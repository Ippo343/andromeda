#pragma once

// Lightweight float 2D vector, used by every physics simulation module.
// float-only throughout: the Xtensa FPU on ESP32/ESP32-S3 is single-precision only, and
// ESP32-C3 (RISC-V) has no hardware FPU at all, so double buys no speed anywhere here.

#include <math.h>

#include "geometry/geometry.h"

struct Vec2f
{
    float x = 0, y = 0;

    Vec2f() = default;
    Vec2f(float x_, float y_) : x(x_), y(y_) {}

    Vec2f operator+(const Vec2f& o) const { return Vec2f(x + o.x, y + o.y); }
    Vec2f operator-(const Vec2f& o) const { return Vec2f(x - o.x, y - o.y); }
    Vec2f operator*(float s) const { return Vec2f(x * s, y * s); }

    Vec2f& operator+=(const Vec2f& o)
    {
        x += o.x;
        y += o.y;
        return *this;
    }

    Vec2f& operator-=(const Vec2f& o)
    {
        x -= o.x;
        y -= o.y;
        return *this;
    }

    float dot(const Vec2f& o) const { return x * o.x + y * o.y; }
    float lengthSquared() const { return dot(*this); }
    float length() const { return sqrtf(lengthSquared()); }

    // Never returns NaN: a near-zero vector normalizes to (0,0) instead of dividing by
    // a near-zero length, since callers (tangent directions, constraint corrections)
    // treat "no defined direction" as a legitimate, safely-ignorable case.
    Vec2f normalized() const
    {
        float len = length();
        if (len < 1e-6f) return Vec2f(0, 0);
        return Vec2f(x / len, y / len);
    }

    // Rounds to the nearest mm; precision loss vs the source float is negligible at LED
    // spacing scale.
    CartesianCoordinates toCartesian() const
    {
        CartesianCoordinates c;
        c.x = (int16_t)lroundf(x);
        c.y = (int16_t)lroundf(y);
        return c;
    }

    static Vec2f fromCartesian(CartesianCoordinates c) { return Vec2f((float)c.x, (float)c.y); }
};

inline Vec2f operator*(float s, const Vec2f& v) { return v * s; }
