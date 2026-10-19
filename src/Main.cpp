#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/utils/cocos.hpp>
#include "Predictor.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace geode::prelude;

namespace {
using safeclick::State;
using safeclick::Obstacle;
using safeclick::Kind;
using safeclick::Rect;

struct Calibration {
    bool initialized = false;
    bool prevGrounded = true;
    double lastX = 0, lastY = 0;
    double vx = 310, jumpVelocity = 305, gravity = 615;
    double prevDy = 0;
    bool havePrevDy = false;

    void sample(PlayerObject* p, double dt) {
        if (!p || dt < 0.003 || dt > 0.03) return;
        auto pos = p->getPosition();
        if (initialized) {
            auto dx = (static_cast<double>(pos.x) - lastX) / dt;
            if (std::isfinite(dx) && dx > 50 && dx < 1500)
                vx = vx * .6 + dx * .4;
            auto dy = static_cast<double>(pos.y) - lastY;
            if (!p->m_isOnGround && prevGrounded && dy > .02) {
                double measured = dy / dt + gravity * dt;
                if (measured > 90 && measured < 1200)
                    jumpVelocity = jumpVelocity * .3 + measured * .7;
                havePrevDy = false;
            } else if (!p->m_isOnGround && !prevGrounded && havePrevDy) {
                double measuredGravity = (prevDy - dy) / (dt * dt);
                if (measuredGravity > 200 && measuredGravity < 3000)
                    gravity = gravity * .8 + measuredGravity * .2;
            }
            if (!p->m_isOnGround && !prevGrounded) {
                prevDy = dy;
                havePrevDy = true;
            } else if (p->m_isOnGround) havePrevDy = false;
        }
        lastX = pos.x;
        lastY = pos.y;
        prevGrounded = p->m_isOnGround;
        initialized = true;
    }
};

struct Pending {
    bool active = false;
    bool physicalHeld = false;
    bool injectReleaseNextTick = false;
    int remaining = 0;
};
struct Runtime {
    PlayLayer* owner = nullptr;
    Pending pending;
    Calibration calibration;
    bool injecting = false;
    uint64_t processed = 0, delayed = 0, forced = 0;
    void reset(PlayLayer* layer) {
        owner = layer;
        pending = {};
        calibration = {};
    }
};
Runtime g;

bool supported(PlayLayer* layer, PlayerObject* p) {
    return layer && p && !p->m_isDead && p->m_isOnGround &&
           !p->m_isShip && !p->m_isBird && !p->m_isBall &&
           !p->m_isDart && !p->m_isRobot && !p->m_isSpider &&
           !p->m_isSwing && !p->m_isPlatformer && !p->m_isDashing &&
           !p->m_isUpsideDown && !p->m_isOnSlope &&
           !p->m_controlsDisabled && g.calibration.initialized;
}

std::vector<Obstacle> collectObjects(PlayLayer* layer, State const& s, int horizonTicks) {
    std::vector<Obstacle> output;
    if (!layer->m_objects) return output;
    // No object is mutated; moving objects keep their current bounds.
    double travel = std::abs(s.vx) * safeclick::kTick * horizonTicks;
    double minX = s.x - 60;
    double maxX = s.x + travel + 90;
    if (s.vx < 0) { minX = s.x - travel - 90; maxX = s.x + 60; }
    // Keep vertical headroom for the entire cube flight.
    double peak = (s.jumpVelocity * s.jumpVelocity) / (2 * s.gravity);
    double minY = s.y - 100, maxY = s.y + peak + 100;
    for (auto* obj : CCArrayExt<GameObject*>(layer->m_objects)) {
        if (!obj || obj->m_isDisabled) continue;
        Kind kind;
        if (obj->m_objectType == GameObjectType::Hazard) kind = Kind::Hazard;
        else if (obj->m_objectType == GameObjectType::Solid) kind = Kind::Solid;
        else continue;
        auto p = obj->getPosition();
        if (p.x < minX - 200 || p.x > maxX + 200 ||
            p.y < minY - 200 || p.y > maxY + 200) continue;
        auto r = obj->getObjectRect(1.0f, 1.0f);
        if (r.size.width <= .01 || r.size.height <= .01) continue;
        if (r.getMaxX() < minX || r.getMinX() > maxX ||
            r.getMaxY() < minY || r.getMinY() > maxY) continue;
        output.push_back({{r.origin.x, r.origin.y, r.size.width, r.size.height}, kind});
    }
    return output;
}

int evaluate(PlayLayer* layer, PlayerObject* p) {
    auto r = p->getObjectRect(1.0f, 1.0f);
    auto pos = p->getPosition();
    State s;
    s.x = pos.x;
    s.y = pos.y;
    s.vx = g.calibration.vx;
    s.vy = 0;
    s.width = r.size.width;
    s.height = r.size.height;
    s.groundY = pos.y;
    s.grounded = true;
    s.gravity = g.calibration.gravity;
    s.jumpVelocity = g.calibration.jumpVelocity;
    auto* mod = Mod::get();
    safeclick::Config cfg;
    cfg.maxDelay = std::clamp(static_cast<int>(mod->getSettingValue<int64_t>("max-delay")), 0, 12);
    cfg.flightTicks = std::clamp(std::max(260, static_cast<int>(std::ceil(2.0 *
        s.jumpVelocity / (s.gravity * safeclick::kTick))) + 12), 30, 600);
    cfg.margin = mod->getSettingValue<double>("margin");
    auto objs = collectObjects(layer, s, cfg.flightTicks + cfg.maxDelay);
    return safeclick::chooseDelay(s, objs, cfg);
}

// Only send internal injections through this guard. Never use raw Windows detours.
void inject(PlayLayer* layer, bool down) {
    if (!layer) return;
    g.injecting = true;
    layer->handleButton(down, 1, true);
    g.injecting = false;
}
}

class $modify(SafeClickInput, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool player1) {
        if (g.injecting || !Mod::get()->getSettingValue<bool>("enabled") ||
            button != 1 || !player1 || static_cast<GJBaseGameLayer*>(this) != static_cast<GJBaseGameLayer*>(PlayLayer::get())) {
            GJBaseGameLayer::handleButton(down, button, player1);
            return;
        }
        auto* layer = PlayLayer::get();
        if (g.owner != layer) g.reset(layer);
        auto& pending = g.pending;
        if (!down) {
            if (pending.active) {
                pending.physicalHeld = false; // hold release until injected tap
                return;
            }
            GJBaseGameLayer::handleButton(down, button, player1);
            return;
        }
        if (pending.active) {
            pending.physicalHeld = true;
            return; // repeated down during one buffered click
        }
        auto* p = layer->m_player1;
        if (!supported(layer, p)) {
            GJBaseGameLayer::handleButton(down, button, player1);
            return;
        }
        ++g.processed;
        int delay = evaluate(layer, p);
        if (delay == 0) {
            GJBaseGameLayer::handleButton(down, button, player1);
            return;
        }
        pending.active = true;
        pending.physicalHeld = true;
        // The prediction suggests when an opening will appear. We still
        // verify each intermediate live tick and never exceed the full cap.
        pending.remaining = static_cast<int>(Mod::get()->getSettingValue<int64_t>("max-delay"));
        ++g.delayed;
        if (Mod::get()->getSettingValue<bool>("diagnostics"))
            log::info("SafeClick: buffered press; predicted offset {} tick(s), cap {}", delay, pending.remaining);
        // Do not forward input yet; future physics ticks call inject().
    }
};

class $modify(SafeClickTick, PlayLayer) {
    void postUpdate(float dt) {
        if (g.owner != this) g.reset(this);
        if (g.pending.injectReleaseNextTick) {
            g.pending.injectReleaseNextTick = false;
            inject(this, false);
        }
        if (g.pending.active) {
            if (!m_player1 || m_player1->m_isDead) {
                g.pending = {};
            } else {
                --g.pending.remaining;
                const bool deadline = g.pending.remaining <= 0;
                const bool safe = supported(this, m_player1) &&
                                  evaluate(this, m_player1) == 0;
                if (safe || deadline) {
                    bool held = g.pending.physicalHeld;
                    g.pending.active = false;
                    if (deadline && !safe) ++g.forced;
                    if (Mod::get()->getSettingValue<bool>("diagnostics"))
                        log::info("SafeClick: injected click; remaining={} forced={}",
                                  g.pending.remaining, deadline && !safe);
                    inject(this, true);
                    if (!held) g.pending.injectReleaseNextTick = true;
                }
            }
        }
        PlayLayer::postUpdate(dt);
        g.calibration.sample(m_player1, dt);
    }
    void resetLevel() {
        g.reset(this);
        PlayLayer::resetLevel();
    }
    void onExit() {
        g.reset(nullptr);
        PlayLayer::onExit();
    }
};
