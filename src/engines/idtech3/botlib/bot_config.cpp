// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The bots' configuration files. See bot_config.h.

#include "engines/idtech3/botlib/bot_config.h"

#include <cmath>
#include <functional>

namespace reluva::idtech3::botlib {

namespace {

constexpr int kDefaultCase = 999999; // a default case: always below it

// A number: a sign may go before it (a weight's minus is dropped, as the
// original does).
bool readValue(BotScript& s, float& value) {
    double v = 0;
    if (!s.readNumber(v))
        return false;
    value = float(std::fabs(v));
    return true;
}

// "return w;" or "return balance(w, min, max);" (the middle value used).
bool readReturn(BotScript& s, FuzzySwitch& f) {
    if (s.check("balance")) {
        float lo = 0, hi = 0;
        if (!s.expect("(") || !readValue(s, f.weight) || !s.expect(",") || !readValue(s, lo) || !s.expect(",") ||
            !readValue(s, hi) || !s.expect(")"))
            return false;
    } else if (!readValue(s, f.weight)) {
        return false;
    }
    return s.expect(";");
}

std::unique_ptr<FuzzySwitch> readSwitch(BotScript& s) {
    // switch (INDEX) { case N: <return or switch> ... default: ... }
    double index = 0;
    if (!s.expect("(") || !s.readNumber(index) || !s.expect(")") || !s.expect("{"))
        return nullptr;
    std::unique_ptr<FuzzySwitch> first;
    FuzzySwitch* last = nullptr;
    bool haveDefault = false;
    BotToken t;
    while (s.next(t) && !t.is("}")) {
        auto f = std::make_unique<FuzzySwitch>();
        f->index = int(index);
        if (t.is("default")) {
            f->value = kDefaultCase;
            haveDefault = true;
        } else if (t.is("case")) {
            double v = 0;
            if (!s.readNumber(v))
                return nullptr;
            f->value = int(v);
        } else {
            s.fail("expected case or default, found " + t.text);
            return nullptr;
        }
        if (!s.expect(":"))
            return nullptr;
        const bool braced = s.check("{");
        if (s.check("return")) {
            if (!readReturn(s, *f))
                return nullptr;
        } else if (s.check("switch")) {
            if (!(f->child = readSwitch(s)))
                return nullptr;
        } else {
            s.fail("expected return or switch");
            return nullptr;
        }
        if (braced && !s.expect("}"))
            return nullptr;
        FuzzySwitch* raw = f.get();
        if (last)
            last->next = std::move(f);
        else
            first = std::move(f);
        last = raw;
    }
    if (!s.error().empty())
        return nullptr;
    if (!haveDefault) {
        auto f = std::make_unique<FuzzySwitch>();
        f->index = int(index);
        f->value = kDefaultCase;
        if (last)
            last->next = std::move(f);
        else
            first = std::move(f);
    }
    return first;
}

// A structure: { field value ... }, arrays as { a, b, c }. Each field's
// values in order, strings as text.
struct Field {
    std::vector<double> numbers;
    std::string text;
};
bool readStructure(BotScript& s, std::map<std::string, Field>& out) {
    if (!s.expect("{"))
        return false;
    BotToken t;
    while (s.next(t) && !t.is("}")) {
        if (t.type != BotToken::Type::Name) {
            s.fail("expected a field name, found " + t.text);
            return false;
        }
        Field& f = out[t.text];
        if (s.check("{")) {
            do {
                double v = 0;
                if (!s.readNumber(v))
                    return false;
                f.numbers.push_back(v);
            } while (s.check(","));
            if (!s.expect("}"))
                return false;
            continue;
        }
        BotToken v;
        if (!s.next(v)) {
            s.fail("field " + t.text + " without a value");
            return false;
        }
        if (v.type == BotToken::Type::String) {
            f.text = v.text;
        } else {
            s.unread(v);
            double n = 0;
            if (!s.readNumber(n))
                return false;
            f.numbers.push_back(n);
        }
    }
    return s.error().empty();
}

float num(const std::map<std::string, Field>& m, const char* name, size_t i = 0) {
    const auto it = m.find(name);
    return it == m.end() || i >= it->second.numbers.size() ? 0.0f : float(it->second.numbers[i]);
}
std::string text(const std::map<std::string, Field>& m, const char* name) {
    const auto it = m.find(name);
    return it == m.end() ? std::string() : it->second.text;
}
Vec3f vec(const std::map<std::string, Field>& m, const char* name) {
    return {num(m, name, 0), num(m, name, 1), num(m, name, 2)};
}

bool done(BotScript& s, std::string* error) {
    if (!s.error().empty()) {
        if (error)
            *error = s.error();
        return false;
    }
    return true;
}

} // namespace

int WeightConfig::find(const std::string& name) const {
    for (size_t i = 0; i < weights.size(); ++i)
        if (weights[i].first == name)
            return int(i);
    return -1;
}

float WeightConfig::evaluate(int weight, const std::vector<int>& inventory) const {
    if (weight < 0 || size_t(weight) >= weights.size())
        return 0;
    // Down the switches: a case applies while the count is below its value.
    const FuzzySwitch* f = weights[size_t(weight)].second.get();
    while (f) {
        const int count = f->index >= 0 && size_t(f->index) < inventory.size() ? inventory[size_t(f->index)] : 0;
        if (count < f->value) {
            if (!f->child)
                return f->weight;
            f = f->child.get();
        } else {
            if (!f->next)
                return f->weight;
            f = f->next.get();
        }
    }
    return 0;
}

std::unique_ptr<WeightConfig> readWeightConfig(const BotScript::Loader& load, const std::string& file,
                                               std::string* error) {
    BotScript s(load);
    if (!s.open(file)) {
        done(s, error);
        return nullptr;
    }
    auto config = std::make_unique<WeightConfig>();
    config->file = file;
    BotToken t;
    while (s.next(t)) {
        if (!t.is("weight")) {
            s.fail("expected weight, found " + t.text);
            break;
        }
        std::string name;
        if (!s.readString(name))
            break;
        const bool braced = s.check("{");
        std::unique_ptr<FuzzySwitch> f;
        if (s.check("switch")) {
            f = readSwitch(s);
        } else if (s.check("return")) {
            f = std::make_unique<FuzzySwitch>();
            f->value = kDefaultCase;
            if (!readReturn(s, *f))
                f.reset();
        } else {
            s.fail("expected switch or return");
        }
        if (!f || (braced && !s.expect("}")))
            break;
        config->weights.emplace_back(name, std::move(f));
    }
    if (!done(s, error))
        return nullptr;
    return config;
}

bool readItemConfig(const BotScript::Loader& load, const std::string& file, std::vector<ItemInfo>& out,
                    std::string* error) {
    out.clear();
    BotScript s(load);
    if (!s.open(file))
        return done(s, error);
    BotToken t;
    while (s.next(t)) {
        if (!t.is("iteminfo")) {
            s.fail("expected iteminfo, found " + t.text);
            break;
        }
        ItemInfo item;
        std::map<std::string, Field> f;
        if (!s.readString(item.classname) || !readStructure(s, f))
            break;
        item.name = text(f, "name");
        item.model = text(f, "model");
        item.modelIndex = int(num(f, "modelindex"));
        item.type = int(num(f, "type"));
        item.index = int(num(f, "index"));
        item.respawnTime = num(f, "respawntime");
        item.mins = vec(f, "mins");
        item.maxs = vec(f, "maxs");
        out.push_back(std::move(item));
    }
    return done(s, error);
}

bool readWeaponConfig(const BotScript::Loader& load, const std::string& file, std::vector<WeaponInfo>& out,
                      std::string* error) {
    out.assign(32, {});
    BotScript s(load);
    if (!s.open(file))
        return done(s, error);
    std::vector<ProjectileInfo> projectiles;
    BotToken t;
    while (s.next(t)) {
        std::map<std::string, Field> f;
        if (t.is("weaponinfo")) {
            if (!readStructure(s, f))
                break;
            WeaponInfo w;
            w.valid = true;
            w.number = int(num(f, "number"));
            w.name = text(f, "name");
            w.model = text(f, "model");
            w.level = int(num(f, "level"));
            w.weaponIndex = int(num(f, "weaponindex"));
            w.flags = int(num(f, "flags"));
            w.projectile = text(f, "projectile");
            w.numProjectiles = int(num(f, "numprojectiles"));
            w.hspread = num(f, "hspread");
            w.vspread = num(f, "vspread");
            w.speed = num(f, "speed");
            w.acceleration = num(f, "acceleration");
            w.recoil = vec(f, "recoil");
            w.offset = vec(f, "offset");
            w.angleOffset = vec(f, "angleoffset");
            w.extraZVelocity = num(f, "extrazvelocity");
            w.ammoAmount = int(num(f, "ammoamount"));
            w.ammoIndex = int(num(f, "ammoindex"));
            w.activate = num(f, "activate");
            w.reload = num(f, "reload");
            w.spinUp = num(f, "spinup");
            w.spinDown = num(f, "spindown");
            if (w.number < 0 || w.number >= int(out.size())) {
                s.fail("weapon number out of range");
                break;
            }
            out[size_t(w.number)] = std::move(w);
        } else if (t.is("projectileinfo")) {
            if (!readStructure(s, f))
                break;
            ProjectileInfo p;
            p.name = text(f, "name");
            p.model = text(f, "model");
            p.flags = int(num(f, "flags"));
            p.gravity = num(f, "gravity");
            p.damage = int(num(f, "damage"));
            p.radius = num(f, "radius");
            p.visDamage = int(num(f, "visdamage"));
            p.damageType = int(num(f, "damagetype"));
            p.healthInc = int(num(f, "healthinc"));
            p.push = num(f, "push");
            p.detonation = num(f, "detonation");
            p.bounce = num(f, "bounce");
            p.bounceFric = num(f, "bouncefric");
            p.bounceStop = num(f, "bouncestop");
            projectiles.push_back(std::move(p));
        } else {
            s.fail("expected weaponinfo or projectileinfo, found " + t.text);
            break;
        }
    }
    if (!done(s, error))
        return false;
    // Each weapon with its projectile's description.
    for (WeaponInfo& w : out) {
        if (!w.valid)
            continue;
        bool found = false;
        for (const ProjectileInfo& p : projectiles)
            if (p.name == w.projectile) {
                w.proj = p;
                found = true;
                break;
            }
        if (!found) {
            if (error)
                *error = file + ": weapon " + w.name + " has no projectile " + w.projectile;
            return false;
        }
    }
    return true;
}

} // namespace reluva::idtech3::botlib
