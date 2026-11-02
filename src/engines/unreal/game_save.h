// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// The Fallen's own saved games: a level written out as a package (.dsa),
// as the engine saves it. What's read here is what a map doesn't have:
// the Level object's ghosts, dialogue table counts and the sounds playing,
// and the C3D mesh states (each actor's animation groups and face
// channels). See docs/formats/unreal/save.md.

#include "common/types.h"
#include "engines/unreal/package.h"

#include <string>
#include <vector>

namespace reluva::unreal {

// The Level object past its actor list: its URL, the ghosts (actors out of
// the level for now), the dialogue table as said so far (each line's key
// and how often it's been said) and the sounds playing when it was saved.
struct LevelExtras {
    std::vector<int32_t> actors; // object references (0: none)
    std::string urlMap, urlPortal;
    std::vector<int32_t> ghosts;
    struct Line {
        std::string key; // TYPE SPEAKER LISTENER ID MISSION
        int said = 0;
    };
    std::vector<Line> lines;
    struct Sound {
        int32_t actor = 0; // object reference
        int32_t id = 0;    // the audio driver's: actor x 16 + slot x 2 + no-override; below 0 slot none
        std::string path;  // Package.Group.Name
        float volume = 1, radius = 0, pitch = 1, start = 0; // start: in the level's seconds
    };
    std::vector<Sound> sounds;
};
Error readLevelExtras(const Package& package, LevelExtras& out);

// A C3DMESHSTATE: an actor's animation groups (its mesh's own, FullBody,
// LowerTorso, UpperTorso, then its face channels sk3...) as the engine
// keeps them.
struct MeshStateGroup {
    std::string group, sequence;
    int32_t source = 0;
    float frame = 0;  // seconds into the sequence (a face channel's), or the body's 0-1
    int32_t type = 0; // 1 playing, 2, 4 a face channel
    int32_t state = 0; // a face channel's: 0 held, 5 tweening
    float rate = 1, weight = 1;
    float fromFrame = 0, fromWeight = 0, elapsed = 0, duration = 1;
    std::string pendingSequence;
    float pendingFrame = 0, pendingWeight = 0, pendingTween = 0;
    std::string bone; // the bone a layer is played from
};
struct MeshState {
    std::string mesh;
    std::vector<MeshStateGroup> groups;
};
Error readMeshState(const Package& package, int exportIndex, MeshState& out);

// Where a C3D mesh state's animation groups are in its export's data
// (from the start of its count to the end of the last).
bool meshStateGroupSpan(const std::vector<uint8_t>& data, size_t& begin, size_t& end);
// Where, in a Level object's data, the model reference starts and the
// reach list ends (copied as they are into a saved game's).
bool levelModelSpan(const std::vector<uint8_t>& data, int version, size_t& begin, size_t& end);

// Whether a file is a package (a saved game of the original's, not Reluva's own).
bool isPackageFile(const std::string& path);

} // namespace reluva::unreal
