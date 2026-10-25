// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Fragment shader for gfx::Renderer3D draw calls: texture times colour,
// with an optional alpha test; or (params.y) the light the dynamic lights
// cast on the pixel, fading linearly to nothing at each one's radius. Resource sets follow SDL_gpu.h: a fragment
// shader's textures are set 2, its uniform buffers set 3.
#version 450

layout(location = 0) in vec2 inUv;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec3 inWorld;

layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D tex;

layout(set = 3, binding = 0) uniform Fragment {
    vec4 params; // x: alpha test (0 none, 1 > 0, 2 < 0.5, 3 >= 0.5), y: dynamic lights instead (1)
                 // or the brightness pass (2, z: 1 / gamma)
    vec4 lightCount; // x
    vec4 lightPos[16]; // xyz, w: radius
    vec4 lightColor[16];
};

void main() {
    if (params.y > 1.5) {
        // The brightness pass: the frame through the curve (params.z = 1 / gamma).
        outColor = vec4(pow(texture(tex, inUv).rgb, vec3(params.z)), 1.0);
        return;
    }
    if (params.y > 0.5) {
        vec3 sum = vec3(0.0);
        int n = int(lightCount.x + 0.5);
        for (int i = 0; i < n; ++i) {
            float d = distance(inWorld, lightPos[i].xyz);
            sum += lightColor[i].rgb * clamp(1.0 - d / lightPos[i].w, 0.0, 1.0);
        }
        outColor = vec4(sum, 1.0);
        return;
    }
    vec4 c = texture(tex, inUv) * inColor;
    int test = int(params.x + 0.5);
    if ((test == 1 && c.a <= 0.0) || (test == 2 && c.a >= 0.5) || (test == 3 && c.a < 0.5))
        discard;
    outColor = c;
}
