// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Vertex shader for gfx::Renderer3D draw calls (see common/gfx3d.h for what
// each field means). Resource sets follow SDL_gpu.h: uniform buffers of a
// vertex shader are set 1.
#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec2 inUv2;
layout(location = 3) in vec3 inNormal;
layout(location = 4) in vec4 inColor;
layout(location = 5) in vec3 inOldPos;    // another animation frame's, blended towards by mode2.y
layout(location = 6) in vec3 inOldNormal;

layout(location = 0) out vec2 outUv;
layout(location = 1) out vec4 outColor;
layout(location = 2) out vec3 outWorld; // the position in the world, for dynamic lights

struct DeformData {
    vec4 a; // x: type (0 none, 1 wave, 2 bulge, 3 move), y: wave function, z: base, w: amplitude
    vec4 b; // x: phase, y: frequency, z: spread, w: unused
    vec4 c; // move: the vector; bulge: x width, y height, z speed
};

layout(set = 1, binding = 0) uniform Draw {
    mat4 viewProj;
    mat4 model;
    vec4 eye;      // xyz: camera position, w: time in seconds
    vec4 preRow0;  // xyz: texture transform before turbulence
    vec4 preRow1;
    vec4 postRow0; // xyz: texture transform after it
    vec4 postRow1;
    vec4 turb;     // x: amplitude, y: phase
    vec4 texVec0;  // xyz: tcGen vector for u
    vec4 texVec1;  // xyz: for v
    vec4 color;
    vec4 mode;     // x: texGen (0 uv, 1 uv2, 2 environment, 3 vector), y: rgb source, z: alpha source
                   // (0 constant, 1 vertex, 2 one minus vertex, 3 lighting), w: fog distance (0 off)
    vec4 mode2;    // x: follow the camera, y: how far towards the old frame's positions
    vec4 ambient;  // xyz: ambient light for the lighting colour source
    vec4 directed; // xyz: directed light
    vec4 lightDir; // xyz: towards the light, world space
    DeformData deforms[2];
};

const float TAU = 6.28318530718;

float noise1(float x) {
    float i = floor(x), f = x - i;
    float a = fract(sin(i * 12.9898) * 43758.5453) * 2.0 - 1.0;
    float b = fract(sin((i + 1.0) * 12.9898) * 43758.5453) * 2.0 - 1.0;
    return mix(a, b, f * f * (3.0 - 2.0 * f));
}

// Must match gfx::evaluate().
float wave(float func, float base, float amp, float phase, float freq, float t) {
    float x = phase + t * freq;
    float f = fract(x);
    float v;
    int fn = int(func + 0.5);
    if (fn == 0)
        v = sin(f * TAU);
    else if (fn == 1)
        v = f < 0.25 ? 4.0 * f : (f < 0.75 ? 2.0 - 4.0 * f : 4.0 * f - 4.0);
    else if (fn == 2)
        v = f < 0.5 ? 1.0 : -1.0;
    else if (fn == 3)
        v = f;
    else if (fn == 4)
        v = 1.0 - f;
    else
        v = noise1(x);
    return base + amp * v;
}

vec4 source(float which, vec4 vertexColor, vec3 normal) {
    int w = int(which + 0.5);
    if (w == 3)
        return vec4(clamp(ambient.rgb + directed.rgb * max(dot(normal, lightDir.xyz), 0.0), 0.0, 1.0), 1.0);
    return w == 1 ? vertexColor : (w == 2 ? vec4(1.0) - vertexColor : vec4(1.0));
}

void main() {
    float t = eye.w;
    vec3 pos = mix(inPos, inOldPos, mode2.y);
    vec3 baseNormal = mode2.y != 0.0 ? normalize(mix(inNormal, inOldNormal, mode2.y)) : inNormal;
    for (int i = 0; i < 2; ++i) {
        int type = int(deforms[i].a.x + 0.5);
        if (type == 1) {
            float off = (pos.x + pos.y + pos.z) * deforms[i].b.z;
            pos += baseNormal * wave(deforms[i].a.y, deforms[i].a.z, deforms[i].a.w, deforms[i].b.x + off, deforms[i].b.y, t);
        } else if (type == 2) {
            pos += baseNormal * deforms[i].c.y * sin(inUv.x * deforms[i].c.x + t * deforms[i].c.z);
        } else if (type == 3) {
            pos += deforms[i].c.xyz * wave(deforms[i].a.y, deforms[i].a.z, deforms[i].a.w, deforms[i].b.x, deforms[i].b.y, t);
        }
    }
    vec3 world = mode2.x > 0.5 ? pos + eye.xyz : (model * vec4(pos, 1.0)).xyz;
    vec3 normal = normalize(mat3(model) * baseNormal);

    vec2 uv;
    int gen = int(mode.x + 0.5);
    if (gen == 1) {
        uv = inUv2;
    } else if (gen == 2) {
        // Reflect the direction to the camera off the surface.
        vec3 toEye = normalize(eye.xyz - world);
        vec3 r = normal * (2.0 * dot(normal, toEye)) - toEye;
        uv = vec2(0.5 + r.y * 0.5, 0.5 - r.z * 0.5);
    } else if (gen == 3) {
        uv = vec2(dot(pos, texVec0.xyz), dot(pos, texVec1.xyz));
    } else {
        uv = inUv;
    }
    vec3 h = vec3(uv, 1.0);
    uv = vec2(dot(preRow0.xyz, h), dot(preRow1.xyz, h));
    if (turb.x != 0.0) {
        uv.x += turb.x * sin(((pos.x + pos.z) / 1024.0 + turb.y) * TAU);
        uv.y += turb.x * sin((pos.y / 1024.0 + turb.y) * TAU);
    }
    h = vec3(uv, 1.0);
    outUv = vec2(dot(postRow0.xyz, h), dot(postRow1.xyz, h));

    outColor = vec4(source(mode.y, inColor, normal).rgb, source(mode.z, inColor, normal).a) * color;
    if (mode.w > 0.0)
        outColor = vec4(color.rgb, clamp(distance(world, eye.xyz) / mode.w, 0.0, 1.0));
    outWorld = world;
    gl_Position = viewProj * vec4(world, 1.0);
}
