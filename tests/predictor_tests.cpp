#include "Predictor.hpp"
#include <cassert>
#include <iostream>
using namespace safeclick;

int main() {
    Config c; c.maxDelay = 4; c.flightTicks = 235; c.margin = 0;
    State base;
    base.x = 0; base.y = 0; base.vx = 312;
    base.jumpVelocity = 305; base.gravity = 615; base.width = 20; base.height = 20;
    assert(chooseDelay(base, {}, c) == 0 && "no hazards: immediate jump");
    // A deadly slab covering the entire available jump path: all 5 options fail.
    std::vector<Obstacle> unavoidable = {{{25,-40,500,300}, Kind::Hazard}};
    assert(!survives(base, unavoidable, 0, c));
    assert(chooseDelay(base, unavoidable, c) == 4 && "no opening: force at limit");
    // Find a narrow custom obstacle in the region where a few-tick delay matters.
    bool foundOneTick = false;
    for (int xi = 24; xi < 280 && !foundOneTick; xi += 1) {
        for (int yi = 5; yi < 65 && !foundOneTick; yi += 1) {
            std::vector<Obstacle> obstacles = {{{static_cast<double>(xi),static_cast<double>(yi), 1.5, 1.5}, Kind::Hazard}};
            if (!survives(base, obstacles, 0, c) && survives(base, obstacles, 1, c)) {
                assert(chooseDelay(base, obstacles, c) == 1);
                foundOneTick = true;
            }
        }
    }
    assert(foundOneTick && "one tick opening must exist for miniature test hazard");
    c.maxDelay = 0;
    assert(chooseDelay(base, unavoidable, c) == 0 && "zero bound is respected");
    std::cout << "PASS: normal click, exact first safe tick, forced deadline, zero limit\n";
}
