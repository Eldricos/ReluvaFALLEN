// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/idtech3/collision.h"

#include <cstdint>
#include <map>
#include <utility>
#include <vector>

namespace reluva::idtech3::botlib {

// The bots' map of a level (maps/<name>.aas): the space a player can be
// in, cut into convex areas, and how to get from each area to its
// neighbours (walking, jumping, teleporting...), with how long that takes.
// The format and what routing answers were read from the GPL ioquake3 bot
// library (aasfile.h, be_aas_file.c, be_aas_sample.c, be_aas_route.c);
// docs/formats/idtech3/aas.md describes them. This is new code from that
// description.

// How a bot gets from one area to the next.
namespace travel {
constexpr int Invalid = 1, Walk = 2, Crouch = 3, BarrierJump = 4, Jump = 5, Ladder = 6, WalkOffLedge = 7, Swim = 8,
              WaterJump = 9, Teleport = 10, Elevator = 11, RocketJump = 12, BfgJump = 13, GrappleHook = 14,
              DoubleJump = 15, RampJump = 16, StrafeJump = 17, JumpPad = 18, FuncBob = 19;
constexpr int TypeMask = 0xffffff;
} // namespace travel

// Travel flags: which kinds of travel and which areas a route may use.
namespace tfl {
constexpr int Invalid = 0x1, Walk = 0x2, Crouch = 0x4, BarrierJump = 0x8, Jump = 0x10, Ladder = 0x20,
              WalkOffLedge = 0x80, Swim = 0x100, WaterJump = 0x200, Teleport = 0x400, Elevator = 0x800,
              RocketJump = 0x1000, BfgJump = 0x2000, GrappleHook = 0x4000, DoubleJump = 0x8000, RampJump = 0x10000,
              StrafeJump = 0x20000, JumpPad = 0x40000, Air = 0x80000, Water = 0x100000, Slime = 0x200000,
              Lava = 0x400000, DoNotEnter = 0x800000, FuncBob = 0x1000000, Flight = 0x2000000, Bridge = 0x4000000,
              NotTeam1 = 0x8000000, NotTeam2 = 0x10000000;
constexpr int Default = Walk | Crouch | BarrierJump | Jump | Ladder | WalkOffLedge | Swim | WaterJump | Teleport |
                        Elevator | Air | Water | JumpPad | FuncBob;
} // namespace tfl

// What an area holds (its settings' contents) and its flags.
namespace areacontents {
constexpr int Water = 1, Lava = 2, Slime = 4, ClusterPortal = 8, TelePortal = 16, RoutePortal = 32, Teleporter = 64,
              JumpPad = 128, DoNotEnter = 256, ViewPortal = 512, Mover = 1024, NotTeam1 = 2048, NotTeam2 = 4096;
constexpr int ModelNumShift = 24;
} // namespace areacontents
namespace areaflag {
constexpr int Grounded = 1, Ladder = 2, Liquid = 4, Disabled = 8, Bridge = 16;
} // namespace areaflag
namespace presence {
constexpr int None = 1, Normal = 2, Crouch = 4;
} // namespace presence
namespace faceflag {
constexpr int Solid = 1, Ladder = 2, Ground = 4, Gap = 8, Liquid = 16, LiquidSurface = 32, Bridge = 64;
} // namespace faceflag

struct AasPlane {
    Vec3f normal{};
    float dist = 0;
    int type = 0;
};
struct AasFace {
    int plane = 0, flags = 0, numEdges = 0, firstEdge = 0, frontArea = 0, backArea = 0;
};
struct AasArea {
    int numFaces = 0, firstFace = 0;
    Vec3f mins{}, maxs{}, center{};
};
struct AasAreaSettings {
    int contents = 0, flags = 0, presence = 0, cluster = 0, clusterArea = 0;
    int numReach = 0, firstReach = 0; // its reachabilities
};
// A way from one area into another: from `start` (in this area) to
// `end` (in `area`), of a kind (travel::), taking `time` hundredths of a
// second.
struct AasReach {
    int area = 0, face = 0, edge = 0;
    Vec3f start{}, end{};
    int travelType = 0;
    int time = 0;
};
struct AasNode {
    int plane = 0;
    int children[2] = {}; // > 0 a node, < 0 minus an area, 0 solid
};

class AasWorld {
public:
    // Reads a .aas file (version 4, or 5 with its header scrambled).
    Error load(const std::vector<uint8_t>& data);
    bool loaded() const { return loaded_; }
    int32_t bspChecksum() const { return bspChecksum_; }

    int areaCount() const { return int(areas_.size()); }
    const AasArea& area(int n) const { return areas_[size_t(n)]; }
    const AasAreaSettings& settings(int n) const { return settings_[size_t(n)]; }
    const std::vector<AasReach>& reachabilities() const { return reach_; }
    const std::vector<AasFace>& faces() const { return faces_; }
    const std::vector<int>& faceIndex() const { return faceIndex_; }
    const std::vector<AasPlane>& planes() const { return planes_; }
    bool validArea(int n) const { return n > 0 && n < areaCount(); }

    // The area a point is in, 0 if in solid.
    int pointArea(const Vec3f& point) const;
    // The areas a line passes through, in order, and where it enters
    // each (if `points`), at most `max`.
    int traceAreas(const Vec3f& start, const Vec3f& end, std::vector<int>& areas, std::vector<Vec3f>* points,
                   int max) const;
    // The box of a player standing (presence::Normal) or crouching.
    static void presenceBox(int presenceType, Vec3f& mins, Vec3f& maxs);

    bool grounded(int a) const { return validArea(a) && (settings(a).flags & areaflag::Grounded); }
    bool swim(int a) const { return validArea(a) && (settings(a).flags & areaflag::Liquid); }
    bool crouchOnly(int a) const { return validArea(a) && !(settings(a).presence & presence::Normal); }
    bool ladder(int a) const { return validArea(a) && (settings(a).flags & areaflag::Ladder); }

    static int travelFlagForType(int travelType);
    // The travel flags entering an area needs (its liquid or air, do not
    // enter, a bridge...).
    int contentsTravelFlags(int area) const;
    // Hundredths of a second to cross an area from one point to another
    // (walking, crouching or swimming as the area asks), at least 1.
    int areaTravelTime(int area, const Vec3f& from, const Vec3f& to) const;

    // The quickest way from an area (from `origin` in it, if given) to a
    // goal area using only `travelFlags`: its time and the first
    // reachability to take (an index into reachabilities()). False if
    // there is none. In the goal area already: time 1, reachability 0.
    bool route(int area, const Vec3f* origin, int goalArea, int travelFlags, int& time, int& reach);
    int travelTime(int area, const Vec3f& origin, int goalArea, int travelFlags);
    // Turns an area on or off for routing (doors closing...); returns
    // whether it was on.
    bool enableArea(int area, bool enable);
    // The area's reachabilities after `reach` (an index; 0 for its
    // first), 0 when there are no more.
    int nextReachability(int area, int reach) const;

private:
    // The quickest time from every area to one goal, and the reachability
    // each takes first.
    struct RouteTable {
        std::vector<int> time; // 0: no way
        std::vector<int> reach;
    };
    const RouteTable& table(int goalArea, int travelFlags);
    bool usable(const AasReach& r, int travelFlags) const;

    bool loaded_ = false;
    int32_t bspChecksum_ = 0;
    std::vector<Vec3f> vertexes_;
    std::vector<AasPlane> planes_;
    std::vector<std::pair<int, int>> edges_;
    std::vector<int> edgeIndex_, faceIndex_;
    std::vector<AasFace> faces_;
    std::vector<AasArea> areas_;
    std::vector<AasAreaSettings> settings_;
    std::vector<AasReach> reach_;
    std::vector<AasNode> nodes_;
    std::vector<int> reachArea_;               // the area each reachability leaves from
    std::vector<std::vector<int>> into_;        // per area, the reachabilities leading into it
    std::map<std::pair<int, int>, RouteTable> tables_; // by goal area and travel flags
};

} // namespace reluva::idtech3::botlib
