// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/eliteforce/scene.h"

#include <algorithm>
#include <cmath>

namespace reluva::idtech3::eliteforce {

void Scene::clear() {
    pending = entities.size();
    pendingPolys = polys.size();
    pendingLights = lights.size();
}

void Scene::addEntity(Qvm& vm, int32_t address, int32_t clearedSize) {
    SceneEntity e;
    const int32_t n = std::max<int32_t>(0, std::min<int32_t>(kRefEntityKept, int32_t(vm.memory().size()) - address));
    if (address > 0 && vm.valid(address, n))
        e.bytes.assign(&vm.memory()[size_t(address)], &vm.memory()[size_t(address)] + n);
    e.clearedSize = clearedSize;
    entities.push_back(std::move(e));
}

void Scene::addPoly(Qvm& vm, int32_t shader, int32_t count, int32_t address) {
    Poly p;
    p.shader = shader;
    p.vertexCount = count;
    const int32_t bytes = std::clamp(count, 0, 256) * 24;
    if (bytes > 0 && vm.valid(address, bytes))
        p.vertices.assign(&vm.memory()[size_t(address)], &vm.memory()[size_t(address + bytes)]);
    polys.push_back(std::move(p));
}

void Scene::addLight(Qvm& vm, int32_t origin, float intensity, float r, float g, float b) {
    Light l;
    if (vm.valid(origin, 12))
        for (int i = 0; i < 3; ++i)
            l.origin[size_t(i)] = Qvm::asFloat(vm.readInt(origin + 4 * i));
    l.radius = intensity;
    l.color = {r, g, b};
    lights.push_back(l);
}

void Scene::render(Qvm& vm, int32_t refdef, size_t picturesSoFar) {
    if (vm.valid(refdef, kRefdefSize))
        view.assign(&vm.memory()[size_t(refdef)], &vm.memory()[size_t(refdef)] + kRefdefSize);
    ++renders;
    views.push_back({view, pending, entities.size() - pending, pendingPolys, polys.size() - pendingPolys,
                     pendingLights, lights.size() - pendingLights, picturesSoFar});
    clear();
}

int32_t writeLerpTag(Qvm& vm, int32_t address, const Md3Model* m, int32_t startFrame, int32_t endFrame, float frac,
                     const std::string& tagName) {
    if (!vm.checkWrite(address, 48))
        return 0;
    const Md3Tag* t0 = m ? m->tag(std::clamp(startFrame, 0, int32_t(m->frames.size()) - 1), tagName) : nullptr;
    const Md3Tag* t1 = m ? m->tag(std::clamp(endFrame, 0, int32_t(m->frames.size()) - 1), tagName) : nullptr;
    if (!t0 || !t1) {
        for (int i = 0; i < 12; ++i)
            vm.writeInt(address + 4 * i, Qvm::fromFloat(i >= 3 && (i - 3) % 4 == 0 ? 1.0f : 0.0f));
        return 0;
    }
    for (int i = 0; i < 3; ++i)
        vm.writeInt(address + 4 * i,
                    Qvm::fromFloat(t0->origin[size_t(i)] + frac * (t1->origin[size_t(i)] - t0->origin[size_t(i)])));
    for (int j = 0; j < 3; ++j) {
        std::array<float, 3> axis{};
        float length = 0;
        for (int i = 0; i < 3; ++i) {
            axis[size_t(i)] = t0->axis[size_t(j)][size_t(i)] +
                              frac * (t1->axis[size_t(j)][size_t(i)] - t0->axis[size_t(j)][size_t(i)]);
            length += axis[size_t(i)] * axis[size_t(i)];
        }
        length = length > 0 ? std::sqrt(length) : 1;
        for (int i = 0; i < 3; ++i)
            vm.writeInt(address + 12 + 12 * j + 4 * i, Qvm::fromFloat(axis[size_t(i)] / length));
    }
    return 1;
}

} // namespace reluva::idtech3::eliteforce
