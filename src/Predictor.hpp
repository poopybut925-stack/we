#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace safeclick {
constexpr double kTick = 1.0 / 240.0;

enum class Kind : uint8_t { Hazard, Solid };
struct Rect {
    double x, y, w, h;
    double left() const { return x; }
    double right() const { return x + w; }
    double bottom() const { return y; }
    double top() const { return y + h; }
};
struct Obstacle { Rect rect; Kind kind; };
struct State {
    double x = 0, y = 0;       // Player center, GD world coordinates
    double vx = 310, vy = 0;   // World units per second
    double jumpVelocity = 305;
    double gravity = 615;
    double width = 22, height = 22;  // Player collision size
    double groundY = 0;        // Player center at current resting height
    bool grounded = true;
};
struct Config {
    int maxDelay = 4;
    int flightTicks = 235;
    double margin = 0.6;
};
inline bool overlaps(Rect const& a, Rect const& b) {
    return a.left() < b.right() && a.right() > b.left() &&
           a.bottom() < b.top() && a.top() > b.bottom();
}
inline Rect playerBox(State const& s, double margin) {
    return {s.x - s.width * .5 - margin, s.y - s.height * .5 - margin,
            s.width + margin * 2, s.height + margin * 2};
}

// Non-mutating, geometry-only trajectory test. A future trigger, moving solid,
// non-AABB hazard or slope must not be described as engine-perfect by this code.
// Offsets >= 1 represent N complete coasting ticks before the jump.
inline bool survives(State s, std::vector<Obstacle> const& nearby, int jumpOffset,
                     Config cfg = {}) {
    if (!std::isfinite(s.vx) || !std::isfinite(s.vy) ||
        !std::isfinite(s.jumpVelocity) || !std::isfinite(s.gravity) ||
        s.jumpVelocity <= 0 || s.gravity <= 0 || s.width <= 0 || s.height <= 0)
        return false;

    bool jumped = false;
    int postLanding = 0;
    bool hadFlight = false;
    // A short fixed lookahead must still reach the next landing for unusually
    // high jumps; otherwise an unsafe late descent could be falsely called safe.
    int naturalFlightTicks = static_cast<int>(std::ceil(2.0 * s.jumpVelocity /
                                                     (s.gravity * kTick))) + 12;
    int maxSteps = std::clamp(std::max(cfg.flightTicks, naturalFlightTicks), 30, 600);
    for (int step = 0; step < maxSteps + jumpOffset; ++step) {
        double prevBottom = s.y - s.height * .5;

        if (step == jumpOffset && s.grounded) {
            s.vy = s.jumpVelocity;
            s.grounded = false;
            jumped = true;
        }
        if (!s.grounded) {
            // Semi-implicit Euler, aligned to fixed GD physics step.
            s.vy -= s.gravity * kTick;
            s.y += s.vy * kTick;
        }
        s.x += s.vx * kTick;
        bool landedOnSolid = false;
        double highestTop = -1e12;
        for (auto const& object : nearby) {
            Rect box = playerBox(s, cfg.margin);
            if (!overlaps(box, object.rect)) continue;
            if (object.kind == Kind::Hazard) return false;

            // For cube, a top landing is safe only while descending, if the
            // previous bottom was above the surface. Side/head contact kills.
            if (s.vy <= 0 && prevBottom >= object.rect.top() - 0.5 &&
                box.bottom() <= object.rect.top() + cfg.margin &&
                object.rect.top() > highestTop) {
                highestTop = object.rect.top();
                landedOnSolid = true;
            } else {
                return false;
            }
        }
        if (landedOnSolid) {
            s.y = highestTop + s.height * .5;
            s.vy = 0;
            s.grounded = true;
            s.groundY = s.y;
        } else if (!s.grounded && s.vy <= 0 && s.y <= s.groundY) {
            s.y = s.groundY;
            s.vy = 0;
            s.grounded = true;
        }
        if (!jumped && step > jumpOffset + 1) return false;
        if (jumped && !s.grounded) hadFlight = true;
        if (jumped && hadFlight && s.grounded) {
            // Include 8 more ticks of ground clearance after landing.
            if (++postLanding >= 8) return true;
        }
    }
    return jumped; // A still-airborne trajectory did not hit any modeled obstacle.
}

inline int chooseDelay(State const& state, std::vector<Obstacle> const& objects,
                       Config cfg = {}) {
    cfg.maxDelay = std::clamp(cfg.maxDelay, 0, 12);
    cfg.flightTicks = std::clamp(cfg.flightTicks, 30, 350);
    for (int delay = 0; delay <= cfg.maxDelay; ++delay) {
        if (survives(state, objects, delay, cfg)) return delay;
    }
    // Explicit user requirement: if no safe opening exists, jump at limit.
    return cfg.maxDelay;
}
}
