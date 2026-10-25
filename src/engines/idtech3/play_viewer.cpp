// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Play page: a Holomatch level run by Elite Force's own game code, as
// a listen server would. The server game (vm/qagame.qvm) runs the level
// with one player, whose commands come from the keyboard and mouse; the
// client game (vm/cgame.qvm) gets a snapshot after each server frame and
// says what to draw: the level from the player's eye, then the HUD.
// Entities the client game adds as models are drawn with their skins,
// lit by the light grid; sprites, quads and lines (beams) with their
// shaders and colours. Other kinds aren't drawn yet.

#include "engines/idtech3/eliteforce/cgame_host.h"
#include "engines/idtech3/eliteforce/game_host.h"
#include "engines/idtech3/entities.h"
#include "engines/idtech3/scene_drawer.h"
#include "engines/idtech3/viewers.h"

#include "common/font.h"
#include "common/fs.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace reluva::idtech3 {

namespace {

constexpr uint64_t kHintMs = 8000;
constexpr float kPi = 3.14159265358979f;
// The command's button bits: attack 1 (Quake III's) and alt attack 32
// (Elite Force's own), found with reluva-tool game-run -buttons: holding
// either changes the same player state words (weapon time, ammunition,
// events). See eliteforce-syscalls.md.
constexpr int32_t kButtonAttack = 1, kButtonAltAttack = 32;
// An angle in degrees as usercmd_t carries it: 65536 to a full turn.
int32_t toShort(float degrees) { return int32_t(std::lround(degrees * 65536.0f / 360.0f)) & 0xffff; }

} // namespace

PlayViewer::PlayViewer(ViewerContext& ctx) : Page(ctx) {
    if (std::strcmp(ctx.game.gameId, "stvef") != 0 || !ctx.files.has("vm/qagame.qvm") || !ctx.files.has("vm/cgame.qvm"))
        return;
    for (const std::string& n : ctx.files.list()) {
        const std::string key = normalizeArchiveName(n);
        if (key.rfind("maps/", 0) == 0 && key.size() > 4 && key.compare(key.size() - 4, 4, ".bsp") == 0)
            names_.push_back(key);
    }
    std::sort(names_.begin(), names_.end());
    // Holomatch maps first.
    for (size_t i = 0; i < names_.size(); ++i)
        if (names_[i].rfind("maps/hm_", 0) == 0) {
            index_ = int(i);
            break;
        }
}

PlayViewer::~PlayViewer() { deactivate(); }

void PlayViewer::activate() {
    const VideoSettings video = videoSettings(ctx_.cvars);
    frameW_ = video.width;
    frameH_ = video.height;
    ctx_.sys.setVideoMode(frameW_, frameH_, video.fullscreen);
    if (Error err = ctx_.sys.init3D(frameW_, frameH_, renderer_)) {
        renderer_ = nullptr;
        ctx_.sys.initGraphics(ctx_.screen.w, ctx_.screen.h, PixelFormat::RGBA32);
        clear(16, 18, 24);
        text(4, 4, "The Play page needs 3D rendering, which isn't available:", {255, 120, 120});
        text(4, 4 + font::kLineHeight, err.message);
        setTitle("3D rendering not available");
        return;
    }
    overlay_.create(frameW_, frameH_, PixelFormat::RGBA32);
    shaders_ = std::make_unique<ShaderCache>(*renderer_, ctx_.files, ctx_.shaders);
    world_ = std::make_unique<WorldRenderer>(*renderer_, *shaders_);
    models_ = std::make_unique<ModelCache>(*renderer_, *shaders_, ctx_.files);
    applyVideoSettings(video, *shaders_, world_.get(), models_.get());
    scene_ = std::make_unique<SceneDrawer>(*renderer_, *shaders_, *models_);
    shownMs_ = 0;
    ctx_.sys.setRelativeMouse(true);
    // A level the menus asked for.
    if (!ctx_.requestMap.empty()) {
        const std::string wanted = normalizeArchiveName("maps/" + ctx_.requestMap + ".bsp");
        for (size_t i = 0; i < names_.size(); ++i)
            if (names_[i] == wanted)
                index_ = int(i);
        ctx_.requestMap.clear();
    }
    start(index_);
}

void PlayViewer::deactivate() {
    closeTyping();
    if (renderer_)
        ctx_.sys.setRelativeMouse(false);
    // The level stops with the page; it starts again when shown.
    cgame_.reset();
    game_.reset();
    stopSounds();
    scene_.reset();
    models_.reset();
    world_.reset();
    shaders_.reset();
    renderer_ = nullptr;
    buttons_.clear();
}

void PlayViewer::start(int index) {
    index_ = index;
    error_.clear();
    cgame_.reset();
    game_.reset();
    stopSounds();
    if (scene_)
        scene_->reset();
    models_->clear();
    world_->clear();
    shaders_->clear();
    yaw_ = pitch_ = 0;
    const std::string& path = names_[size_t(index)];
    std::string map = path.substr(5, path.size() - 9); // without maps/ and .bsp
    setTitle(map + " (" + std::to_string(index + 1) + "/" + std::to_string(names_.size()) + ")");

    auto bsp = std::make_shared<Bsp>();
    std::vector<uint8_t> data;
    Error err = ctx_.files.read(path, data);
    if (!err)
        err = parseBsp(data, *bsp);
    if (!err)
        err = world_->load(*bsp, ctx_.shaders);
    if (err) {
        stop("Couldn't load " + path + ": " + err.message);
        return;
    }
    // The light grid lights models, with the worldspawn's cell size if it
    // gives one.
    grid_ = LightGrid{};
    {
        float cell[3] = {64, 64, 128};
        std::vector<Entity> entities;
        if (!parseEntities(bsp->entities, entities) && !entities.empty())
            entities[0].vector("gridsize", cell);
        grid_.build(*bsp, {cell[0], cell[1], cell[2]});
    }

    // The server: the level, and the player joining it. The holodeck
    // intro, which holds new players in place, is off until there's a
    // way to watch it.
    cvars_.set("g_holoIntro", "0");
    game_ = std::make_unique<eliteforce::GameHost>(ctx_.files, cvars_);
    game_->print = [this](const std::string& text) { addPrint(text, false); };
    printPartial_[0].clear();
    closeTyping();
    err = game_->load();
    if (!err)
        err = game_->startLevel(map, bsp, int32_t(ctx_.sys.getMillis()));
    std::string refused;
    if (!err)
        err = game_->connectClient(0,
                                   "\\name\\Reluva\\model\\munro\\handicap\\100\\sex\\male\\color\\4"
                                   "\\snaps\\20\\rate\\25000\\ip\\localhost",
                                   refused);
    if (!err && !refused.empty())
        err = Error::fail("the game refused the player: " + refused);
    if (!err)
        err = game_->beginClient(0);
    // What the menus asked the server to run once the level started
    // (adding bots...), as typed at its console.
    for (const std::string& command : ctx_.requestCommands) {
        if (err)
            break;
        bool handled = false;
        err = game_->serverCommand(command, &handled);
    }
    ctx_.requestCommands.clear();
    if (err) {
        stop("The server game stopped: " + err.message);
        return;
    }

    // The player's client game, given what the server has set.
    cgame_ = std::make_unique<eliteforce::CgameHost>(ctx_.files, cvars_);
    cgame_->print = [this](const std::string& text) { addPrint(text, true); };
    scene_->setHandles({&cgame_->models(), &cgame_->skins(), &cgame_->shaders()});
    cgame_->subdivisions = videoSettings(cvars_).subdivisions;
    notify_.clear();
    printPartial_[1].clear();
    if (ctx_.mixer) {
        soundSystem_ = std::make_unique<SoundSystem>(*ctx_.mixer, soundLibrary_);
        cgame_->soundSystem = soundSystem_.get();
    }
    err = cgame_->load();
    if (!err) {
        cgame_->setConfigstrings(game_->configstrings());
        sendServerCommands();
        loadingFailed_ = false;
        cgame_->updateScreen = [this] { drawLoadingScreen(); };
        err = cgame_->init(0, 0, 0);
        cgame_->updateScreen = nullptr;
    }
    if (err) {
        stop("The client game stopped: " + err.message);
        return;
    }
    clientTime_ = game_->levelTime();
    lastMs_ = 0;
    cgame_->newSnapshot(game_->snapshot(0));
}

void PlayViewer::drawLoadingScreen() {
    // As the original: the client game's frame drawn now, in the middle of
    // its loading, which only draws what it's loading (the level's picture,
    // its name, the media so far); then shown.
    if (!cgame_ || !renderer_ || loadingFailed_)
        return;
    cgame_->clearPictures();
    cgame_->clearScene();
    int32_t ignored = 0;
    if (cgame_->call(eliteforce::CgameHost::DrawActiveFrame, {game_ ? game_->levelTime() : 0, 0, 0}, ignored)) {
        loadingFailed_ = true; // it can't yet; the level goes on loading regardless
        return;
    }
    renderer_->setGamma(brightness(ctx_));
    renderer_->beginFrame(gfx::identity(), {0, 0, 0}, {0, 0, 0});
    scene_->drawPictures(cgame_->pictures(), 0, cgame_->pictures().size(), 640, 480, 0);
    std::fill(overlay_.pixels.begin(), overlay_.pixels.end(), 0);
    renderer_->endFrame(&overlay_);
    cgame_->clearPictures();
    cgame_->clearScene();
}

void PlayViewer::stop(const std::string& why) {
    error_ = why;
    const std::string log = cgame_ ? cgame_->log() : game_ ? game_->log() : std::string();
    if (!log.empty()) {
        std::string last = log;
        while (!last.empty() && last.back() == '\n')
            last.pop_back();
        error_ += " (last printed: " + last.substr(last.rfind('\n') == std::string::npos ? 0 : last.rfind('\n') + 1) + ")";
    }
    cgame_.reset();
    game_.reset();
    stopSounds();
}

void PlayViewer::stopSounds() {
    // After the client game, which uses it; the mixer's volumes back as
    // the other pages expect them.
    soundSystem_.reset();
    if (ctx_.mixer) {
        ctx_.mixer->setTypeVolume(audio::SoundType::Effects, audio::kMaxVolume);
        ctx_.mixer->setTypeVolume(audio::SoundType::Music, audio::kMaxVolume);
    }
}

void PlayViewer::sendServerCommands() {
    std::vector<std::string> commands;
    for (const auto& [to, text] : game_->serverCommands())
        if (to == 0 || to == -1)
            commands.push_back(text);
    cgame_->setServerCommands(std::move(commands));
}

void PlayViewer::consoleCommand(const std::string& text) {
    // As the original's client: commands it knows itself, then the client
    // game's, and anything else goes to the server, whose game answers
    // (an unknown command it prints as such).
    const std::vector<std::string> w = splitCommand(text);
    if (w.empty() || !cgame_ || !game_)
        return;
    const std::string verb = fs::toLower(w[0]);
    if (verb == "centerview") {
        pitch_ = 0;
        return;
    }
    if (verb == "vstr" && w.size() >= 2) {
        runBinding(cvars_.string(w[1]), 0, true);
        return;
    }
    if (verb == "togglemenu") {
        ctx_.requestPage = "Menu";
        return;
    }
    if (verb == "toggleconsole") {
        if (typing_ == Typing::Console)
            closeTyping();
        else
            openTyping(Typing::Console);
        return;
    }
    if (verb == "messagemode" || verb == "messagemode2") {
        openTyping(verb == "messagemode" ? Typing::Say : Typing::SayTeam, -1, bindingKeyChar_);
        return;
    }
    if (verb == "messagemode3" || verb == "messagemode4") {
        // A word to the player in the crosshair, or the last to hit you,
        // as the client game says.
        int32_t client = -1;
        if (Error err = cgame_->call(verb == "messagemode3" ? 4 : 5, {}, client)) {
            stop("The client game stopped: " + err.message);
            return;
        }
        if (client >= 0)
            openTyping(Typing::Tell, client, bindingKeyChar_);
        return;
    }
    if (verb == "clear") {
        consoleLines_.clear();
        return;
    }
    if (verb == "screenshot" || verb == "screenshotjpeg") {
        notice_ = "\"" + text + "\" isn't done yet";
        return;
    }
    bool handled = false;
    if (Error err = cgame_->consoleCommand(text, handled)) {
        stop("The client game stopped: " + err.message);
        return;
    }
    if (handled)
        return;
    // Settings, after the client game's own commands as the original's
    // console: set (seta, sets, setu) name value, or a setting's name
    // alone (shows it) or with a value.
    if ((verb == "set" || verb == "seta" || verb == "sets" || verb == "setu") && w.size() >= 3) {
        const int32_t flags = verb == "seta"   ? CvarStore::Archive
                              : verb == "sets" ? CvarStore::ServerInfo
                              : verb == "setu" ? CvarStore::UserInfo
                                               : 0;
        cvars_.create(w[1], w[2], flags);
        cvars_.set(w[1], w[2]);
        return;
    }
    if (const CvarStore::Cvar* cv = cvars_.find(w[0])) {
        if (w.size() == 1)
            addPrint("\"" + cv->name + "\" is \"" + cv->value + "\"\n", false);
        else
            cvars_.set(w[0], w[1]);
        return;
    }
    // On a server of one's own, its game's console commands (addbot,
    // kick...) next; the rest goes to the server as the player's.
    if (Error err = game_->serverCommand(text, &handled)) {
        stop("The server game stopped: " + err.message);
        return;
    }
    if (handled)
        return;
    if (Error err = game_->clientCommand(0, text))
        stop("The server game stopped: " + err.message);
}

bool PlayViewer::isButton(const std::string& name) {
    static const char* const kButtons[] = {"forward", "back", "moveleft", "moveright", "moveup", "movedown",
                                           "left", "right", "lookup", "lookdown", "speed", "strafe",
                                           "attack", "altattack", "mlook", "klook"};
    for (const char* b : kButtons)
        if (name == b)
            return true;
    return name.rfind("button", 0) == 0 && name.size() > 6 && name.size() <= 8 &&
           std::isdigit(static_cast<unsigned char>(name[6]));
}

void PlayViewer::runBinding(const std::string& line, int32_t key, bool down) {
    // A key's command: "+name" is held while the key is (the engine's own
    // buttons, or the client game's +/- pair); others run on the press.
    for (const std::string& command : splitCommands(line)) {
        const std::vector<std::string> w = splitCommand(command);
        if (w.empty())
            continue;
        if (w[0][0] == '+' || w[0][0] == '-') {
            const std::string name = fs::toLower(w[0].substr(1));
            if (w[0][0] == '-')
                continue; // a key bound to "-x" alone does nothing useful
            if (isButton(name)) {
                if (down)
                    buttons_[name].insert(key);
                else
                    buttons_[name].erase(key);
            } else {
                std::string text = (down ? "+" : "-") + name;
                for (size_t i = 1; i < w.size(); ++i)
                    text += " " + w[i];
                consoleCommand(text);
            }
            continue;
        }
        if (down)
            consoleCommand(command);
    }
}

bool PlayViewer::held(const char* button) const {
    const auto it = buttons_.find(button);
    return it != buttons_.end() && !it->second.empty();
}

bool PlayViewer::takesKey(const Event& ev) const {
    // Esc goes back to the menus, as in the game; with Ctrl it leaves.
    // While typing, every key is the typing's (Tab, Esc too), but Ctrl+Esc.
    if (ev.type == EventType::KeyDown && ev.key == key::Escape && (ev.mods & keymod::Ctrl))
        return false;
    return typing_ != Typing::None || (ev.type == EventType::KeyDown && ev.key == key::Escape);
}

void PlayViewer::openTyping(Typing t, int32_t client, uint32_t key) {
    // The keys held are let go, as the original's typing takes them.
    buttons_.clear();
    typing_ = t;
    tellClient_ = client;
    field_.clear();
    cursor_ = 0;
    consoleScroll_ = 0;
    historyPos_ = history_.size();
    swallowChar_ = key;
    ctx_.sys.setTextInput(true);
}

void PlayViewer::closeTyping() {
    if (typing_ != Typing::None && renderer_)
        ctx_.sys.setTextInput(false);
    typing_ = Typing::None;
    field_.clear();
    cursor_ = 0;
}

void PlayViewer::submitTyping() {
    std::string text = field_;
    const Typing t = typing_;
    field_.clear();
    cursor_ = 0;
    if (t == Typing::Console) {
        // Lines starting / or \ are commands; others are said, as the
        // original's console does once connected.
        if (text.empty())
            return;
        std::replace(text.begin(), text.end(), '"', '\'');
        if (history_.empty() || history_.back() != text)
            history_.push_back(text);
        historyPos_ = history_.size();
        consoleLines_.push_back("]" + text);
        consoleScroll_ = 0;
        if (text[0] == '/' || text[0] == '\\')
            consoleCommand(text.substr(1));
        else
            consoleCommand("say \"" + text + "\"");
        return;
    }
    closeTyping();
    if (text.empty())
        return;
    // Quotes would end the line early: they become apostrophes.
    std::replace(text.begin(), text.end(), '"', '\'');
    if (t == Typing::Say)
        consoleCommand("say \"" + text + "\"");
    else if (t == Typing::SayTeam)
        consoleCommand("say_team \"" + text + "\"");
    else if (t == Typing::Tell)
        consoleCommand("tell " + std::to_string(tellClient_) + " \"" + text + "\"");
}

void PlayViewer::typingEvent(const Event& ev) {
    if (ev.type == EventType::TextInput) {
        // Characters typed (Latin-1, as the original's), but the one of the
        // key that opened the typing, and the console key in the console.
        for (uint32_t cp : decodeUtf8(ev.text)) {
            if (cp == swallowChar_) {
                swallowChar_ = 0;
                continue;
            }
            swallowChar_ = 0;
            if (cp < 32 || cp == 127 || cp > 255 || field_.size() >= 255)
                continue;
            if (typing_ == Typing::Console && (cp == '`' || cp == '~'))
                continue;
            field_.insert(cursor_++, 1, char(cp));
        }
        return;
    }
    if (ev.type != EventType::KeyDown)
        return;
    swallowChar_ = 0;
    switch (ev.key) {
    case key::Escape: closeTyping(); return;
    case key::Return:
    case key::KpEnter: submitTyping(); return;
    case key::Backspace:
        if (cursor_ > 0)
            field_.erase(--cursor_, 1);
        return;
    case key::Delete:
        if (cursor_ < field_.size())
            field_.erase(cursor_, 1);
        return;
    case key::Left: cursor_ = cursor_ > 0 ? cursor_ - 1 : 0; return;
    case key::Right: cursor_ = std::min(field_.size(), cursor_ + 1); return;
    case key::Home: cursor_ = 0; return;
    case key::End: cursor_ = field_.size(); return;
    default: break;
    }
    if (typing_ != Typing::Console)
        return;
    switch (ev.key) {
    case '`': closeTyping(); return; // the console key closes it again
    case key::Up:
    case key::Down:
        // Earlier lines typed, back and forth.
        if (history_.empty())
            return;
        if (ev.key == key::Up && historyPos_ > 0)
            --historyPos_;
        else if (ev.key == key::Down && historyPos_ < history_.size())
            ++historyPos_;
        field_ = historyPos_ < history_.size() ? history_[historyPos_] : std::string();
        cursor_ = field_.size();
        return;
    case key::PageUp: consoleScroll_ = std::min(consoleScroll_ + 4, int(consoleLines_.size())); return;
    case key::PageDown: consoleScroll_ = std::max(consoleScroll_ - 4, 0); return;
    default: return;
    }
}

void PlayViewer::handleEvent(const Event& ev) {
    if (typing_ != Typing::None) {
        // Typing takes the keys and characters; the mouse still looks.
        if (ev.type == EventType::MouseMove) {
            mouseX_ += ev.moveX;
            mouseY_ += ev.moveY;
        } else {
            typingEvent(ev);
        }
        return;
    }
    if (ev.type == EventType::KeyDown && ev.key == key::Escape) {
        ctx_.requestPage = "Menu";
        return;
    }
    if (!renderer_ || names_.empty())
        return;
    if (ev.type == EventType::KeyDown && ev.key == '`' && !(ev.mods & keymod::Ctrl)) {
        // The console key, whatever it's bound to, as in the original.
        openTyping(Typing::Console, -1, '`');
        return;
    }
    switch (ev.type) {
    case EventType::KeyDown:
    case EventType::KeyUp: {
        const bool down = ev.type == EventType::KeyDown;
        if (down && (ev.mods & keymod::Ctrl)) {
            // Reluva's own: restart, another map, the info.
            const int n = int(names_.size());
            if (ev.key == 'r') {
                start(index_);
                return;
            }
            if (ev.key == key::PageUp || ev.key == key::PageDown) {
                start((index_ + (ev.key == key::PageUp ? n - 1 : 1)) % n);
                return;
            }
            if (ev.key == 'i') {
                showInfo_ = !showInfo_;
                return;
            }
        }
        if (down && ev.repeat)
            return;
        // The key's character, in case its command opens typing (so it
        // isn't typed as well).
        bindingKeyChar_ = ev.key < 128 ? ev.key : 0;
        if (const int32_t k = toQ3Key(ev.key))
            runBinding(ctx_.bindings.get(k), k, down);
        bindingKeyChar_ = 0;
        break;
    }
    case EventType::MouseDown:
    case EventType::MouseUp:
        if (const int32_t k = toQ3Button(ev.button))
            runBinding(ctx_.bindings.get(k), k, ev.type == EventType::MouseDown);
        break;
    case EventType::MouseWheel:
        if (ev.wheelY) {
            const int32_t k = ev.wheelY > 0 ? q3key::WheelUp : q3key::WheelDown;
            runBinding(ctx_.bindings.get(k), k, true);
            runBinding(ctx_.bindings.get(k), k, false);
        }
        break;
    case EventType::MouseMove:
        // Turned into looking once a frame (runGame), as the settings say.
        mouseX_ += ev.moveX;
        mouseY_ += ev.moveY;
        break;
    default: break;
    }
}

void PlayViewer::runGame(uint64_t nowMs) {
    if (!game_ || !cgame_)
        return;
    const int32_t dt = lastMs_ ? int32_t(std::min<uint64_t>(nowMs - lastMs_, 100)) : 0;
    lastMs_ = nowMs;
    // The settings, the original's names and defaults (the menus' Controls
    // set them): a setting not yet made counts as its default.
    auto setting = [&](const char* name, float fallback) {
        return cvars_.find(name) ? cvars_.value(name) : fallback;
    };
    // The mouse: degrees per unit moved are the sensitivity times m_yaw and
    // m_pitch (negative m_pitch: inverted); m_filter averages this frame's
    // move with the last; without free look (cl_freelook 0, unless +mlook
    // is held) it doesn't look up and down. Moving it up looks up: pitch
    // counts downwards.
    float mx = mouseX_, my = mouseY_;
    mouseX_ = mouseY_ = 0;
    if (setting("m_filter", 0) != 0) {
        const float px = mx, py = my;
        mx = (mx + lastMouseX_) / 2;
        my = (my + lastMouseY_) / 2;
        lastMouseX_ = px;
        lastMouseY_ = py;
    }
    const float sensitivity = setting("sensitivity", 5);
    yaw_ -= mx * sensitivity * setting("m_yaw", 0.022f);
    if (setting("cl_freelook", 1) != 0 || held("mlook"))
        pitch_ += my * sensitivity * setting("m_pitch", 0.022f);
    // Turning and looking by keys: cl_yawspeed and cl_pitchspeed degrees a
    // second, times cl_anglespeedkey with +speed; with +strafe, left and
    // right step instead.
    const float keyScale = (held("speed") ? setting("cl_anglespeedkey", 1.5f) : 1.0f) * float(dt) / 1000.0f;
    const bool strafe = held("strafe");
    if (!strafe)
        yaw_ += float(held("left") - held("right")) * setting("cl_yawspeed", 140) * keyScale;
    pitch_ += float(held("lookdown") - held("lookup")) * setting("cl_pitchspeed", 140) * keyScale;
    // Not held within straight up and down here: the game keeps the view
    // there, adding its own offset (the player state's delta angles, set
    // when spawning to face the spawn point's way). Holding the command's
    // pitch as well left the view stuck above level whenever that offset
    // pointed upwards. Kept to a turn either way, as it wraps anyway.
    pitch_ = std::remainder(pitch_, 360.0f);
    if (dt > 0)
        clientTime_ += dt;
    // The server first, kept a frame ahead of the player's time, so the
    // client game always has the snapshot after it to move things towards;
    // then the player's command, stamped with a time the server has
    // reached, as the original's client stamps commands with the
    // snapshots' time. Given first, a command was up to a frame ahead of
    // the server, and with long frames (50 ms and more) the player's
    // weapon stopped firing and their sounds stopped (game-run -events).
    using Clock = std::chrono::steady_clock;
    auto ms = [](Clock::time_point from) {
        return std::chrono::duration<float, std::milli>(Clock::now() - from).count();
    };
    const auto serverStart = Clock::now();
    while (game_->levelTime() <= clientTime_) {
        if (Error err = game_->runFrame()) {
            stop("The server game stopped: " + err.message);
            return;
        }
        sendServerCommands();
        cgame_->setConfigstrings(game_->configstrings());
        cgame_->newSnapshot(game_->snapshot(0));
    }
    timing_.server = ms(serverStart);
    if (dt > 0) {
        // The player's command for this moment, as a client sends one each
        // frame: where they look, how they move, what they press. The game
        // moves the player by it at once, as the original server does.
        eliteforce::GameHost::UserCommand cmd;
        cmd.serverTime = clientTime_;
        cmd.angles = {toShort(pitch_), toShort(yaw_), 0};
        for (const auto& [name, keys] : buttons_) {
            if (keys.empty())
                continue;
            if (name == "attack")
                cmd.buttons |= kButtonAttack;
            else if (name == "altattack")
                cmd.buttons |= kButtonAltAttack;
            else if (name.rfind("button", 0) == 0)
                cmd.buttons |= 1 << std::atoi(name.c_str() + 6);
        }
        cmd.weapon = uint8_t(cgame_->weaponSelect());
        // Running unless +speed is held (cl_run 1, the default), or the reverse.
        const int speed = (cvars_.find("cl_run") ? cvars_.integer("cl_run") != 0 : true) != held("speed") ? 127 : 64;
        int side = held("moveright") - held("moveleft");
        if (strafe)
            side += held("right") - held("left");
        cmd.forward = int8_t(std::clamp(speed * (held("forward") - held("back")), -127, 127));
        cmd.right = int8_t(std::clamp(speed * side, -127, 127));
        cmd.up = int8_t(std::clamp(speed * (held("moveup") - held("movedown")), -127, 127));
        if (Error err = game_->clientThink(0, cmd)) {
            stop("The server game stopped: " + err.message);
            return;
        }
        cgame_->addUserCommand(cmd);
    }
    // The last two seconds' frames, for the info line: each frame's length,
    // whether it sent a command, how many snapshots, and the player's event
    // sequence (player state word 108) as the server has it.
    recentFrames_.push_back({nowMs, dt, dt > 0, game_->snapshotsMade(), game_->clientInt(0, 108)});
    while (!recentFrames_.empty() && recentFrames_.front().ms + 2000 < nowMs)
        recentFrames_.pop_front();
    cgame_->clearScene();
    cgame_->clearPictures();
    if (soundSystem_)
        soundSystem_->setTime(nowMs);
    int32_t ignored = 0;
    const auto clientStart = Clock::now();
    if (Error err = cgame_->call(eliteforce::CgameHost::DrawActiveFrame, {clientTime_, 0, 0}, ignored)) {
        stop("The client game stopped: " + err.message);
        return;
    }
    timing_.client = ms(clientStart);
    if (soundSystem_) {
        // At the original's settings: effects (s_volume) 0.8 and music
        // (s_musicvolume) 0.25 unless the menus set them.
        auto setting = [&](const char* name, float fallback) {
            return cvars_.find(name) ? cvars_.value(name) : fallback;
        };
        soundSystem_->setVolumes(setting("s_volume", 0.8f), setting("s_musicvolume", 0.25f));
        soundSystem_->endFrame();
    }
}

void PlayViewer::update(uint64_t nowMs) {
    if (!renderer_)
        return;
    if (!shownMs_)
        shownMs_ = nowMs;
    runGame(nowMs);
    renderer_->setGamma(brightness(ctx_));
    const float time = float(clientTime_) / 1000.0f;

    // The client game's frame: each scene it rendered over the pictures
    // drawn before it, the rest of its pictures (the HUD) over them all.
    // The player's view is the first with the level in it.
    const auto drawStart = std::chrono::steady_clock::now();
    renderer_->beginFrame(gfx::identity(), {0, 0, 0}, {0, 0, 0});
    bool haveView = false;
    if (cgame_) {
        const auto& scene = cgame_->scene();
        const auto& pictures = cgame_->pictures();
        const bool dynamicLights = !cvars_.find("r_dynamiclight") || cvars_.integer("r_dynamiclight") != 0;
        size_t drawn = 0;
        bool first = true;
        for (const auto& v : scene.views) {
            scene_->drawPictures(pictures, drawn, v.picturesBefore, 640, 480, time);
            drawn = std::max(drawn, v.picturesBefore);
            scene_->drawView(scene, v, 640, 480, time, first, world_.get(), &grid_, dynamicLights);
            first = false;
            if (!haveView && !(readInt(v.refdef, 76) & kNoWorldModel)) {
                haveView = true;
                eye_ = scene_->eye();
                shown_ = {scene_->placedCount(), scene_->effectCount(), scene_->lightCount(),
                          scene_->otherEntities(), scene_->unloadable()};
            }
        }
        scene_->drawPictures(pictures, drawn, pictures.size(), 640, 480, time);
    }
    timing_.draw = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - drawStart).count();

    drawOverlay(nowMs, haveView);
    renderer_->endFrame(&overlay_);
}

void PlayViewer::addPrint(const std::string& text, bool notify) {
    // Whole lines into the console (the last thousand); the client game's
    // also on screen, each 3 seconds (the original's con_notifytime), at
    // most the last 4.
    std::string& partial = printPartial_[notify ? 1 : 0];
    partial += text;
    size_t end;
    while ((end = partial.find('\n')) != std::string::npos) {
        const std::string line = partial.substr(0, end);
        partial.erase(0, end + 1);
        consoleLines_.push_back(line);
        while (consoleLines_.size() > 1000)
            consoleLines_.pop_front();
        if (consoleScroll_ > 0)
            ++consoleScroll_;
        if (!notify)
            continue;
        notify_.emplace_back(ctx_.sys.getMillis(), line);
        while (notify_.size() > 4)
            notify_.pop_front();
    }
}

void PlayViewer::drawColoured(int x, int y, const std::string& line) {
    // A line in the original's colours (^0-^7), with a shadow.
    static const Color kColors[8] = {{0, 0, 0},     {255, 0, 0},   {0, 255, 0},   {255, 255, 0},
                                     {0, 0, 255},   {0, 255, 255}, {255, 0, 255}, {255, 255, 255}};
    Color color{255, 255, 255};
    std::string run;
    auto flush = [&] {
        if (run.empty())
            return;
        font::drawText(overlay_, x + 1, y + 1, run, {0, 0, 0});
        font::drawText(overlay_, x, y, run, color);
        x += int(font::length(run)) * font::kAdvance;
        run.clear();
    };
    for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '^' && i + 1 < line.size() && line[i + 1] != '^') {
            flush();
            color = kColors[(line[i + 1] - '0') & 7];
            ++i;
            continue;
        }
        run += line[i];
    }
    flush();
}

void PlayViewer::drawOverlay(uint64_t nowMs, bool haveView) {
    std::fill(overlay_.pixels.begin(), overlay_.pixels.end(), 0);
    // (A line printed during this frame is stamped a little after nowMs.)
    while (!notify_.empty() && notify_.front().first + 3000 < nowMs)
        notify_.pop_front();
    const int lh = font::kLineHeight;
    const bool blink = (nowMs / 250) % 2 == 0;
    if (typing_ == Typing::Console) {
        // The console over the top half: the lines printed, newest at the
        // bottom (PageUp/PageDown scroll), and what's being typed.
        const int height = frameH_ / 2;
        for (int yy = 0; yy < height; ++yy) {
            uint8_t* p = overlay_.row(yy);
            for (int x = 0; x < frameW_; ++x) {
                p[x * 4 + 0] = 10, p[x * 4 + 1] = 14, p[x * 4 + 2] = 24;
                p[x * 4 + 3] = 215;
            }
        }
        for (int x = 0; x < frameW_; ++x) {
            uint8_t* p = overlay_.row(height) + x * 4;
            p[0] = 200, p[1] = 140, p[2] = 40, p[3] = 255;
        }
        int y = height - 2 * lh - 4;
        drawColoured(4, height - lh - 4, "]" + field_.substr(0, cursor_) + (blink ? "_" : " ") + field_.substr(cursor_));
        for (int i = int(consoleLines_.size()) - 1 - consoleScroll_; i >= 0 && y >= 0; --i, y -= lh)
            drawColoured(4, y, consoleLines_[size_t(i)]);
    } else {
        // The notify lines at the top, then a chat line being typed.
        int notifyY = 4;
        for (const auto& [when, line] : notify_) {
            drawColoured(4, notifyY, line);
            notifyY += lh;
        }
        if (typing_ != Typing::None) {
            const char* prompt = typing_ == Typing::Say ? "Say: " : typing_ == Typing::SayTeam ? "Say Team: " : "Tell: ";
            drawColoured(4, notifyY, std::string("^7") + prompt + field_.substr(0, cursor_) + (blink ? "_" : " ") +
                                         field_.substr(cursor_));
        }
    }
    std::vector<std::string> lines;
    if (!error_.empty()) {
        lines.push_back(error_);
        lines.push_back("Ctrl+R: start the level again, PageUp/PageDown: another map.");
    }
    if (showInfo_) {
        char line[256];
        if (game_) {
            std::snprintf(line, sizeof line, "time %.1f s, eye at %.0f %.0f %.0f%s; %zu models, %zu effects and %zu lights drawn, %d other entities not yet",
                          float(clientTime_) / 1000.0f, eye_[0], eye_[1], eye_[2],
                          haveView ? "" : " (no view yet)", shown_.models, shown_.effects, shown_.lights,
                          shown_.other);
            lines.push_back(line);
            // What the player's commands carry, to see that keys arrive.
            std::string held;
            for (const auto& [name, keys] : buttons_)
                if (!keys.empty())
                    held += " +" + name;
            std::snprintf(line, sizeof line, "weapon chosen by the client game: %d; held:%s",
                          cgame_ ? cgame_->weaponSelect() : 0, held.empty() ? " nothing" : held.c_str());
            lines.push_back(line);
            // Where each frame's time goes, smoothed, to find what's slow.
            std::snprintf(line, sizeof line,
                          "frame: server and bots %.1f ms, client game %.1f ms, drawing %.1f ms; %zu sounds playing",
                          timing_.server, timing_.client, timing_.draw,
                          soundSystem_ ? soundSystem_->playingCount() : size_t(0));
            lines.push_back(line);
            if (recentFrames_.size() > 1) {
                const auto& first = recentFrames_.front();
                const auto& last = recentFrames_.back();
                int commands = 0, longest = 0;
                for (const auto& f : recentFrames_) {
                    commands += f.command;
                    longest = std::max(longest, f.dt);
                }
                std::snprintf(line, sizeof line,
                              "last 2 s: %zu frames (%.1f ms each, the longest %d), %d commands, %d snapshots, %d "
                              "player events on the server",
                              recentFrames_.size(), float(last.ms - first.ms) / float(recentFrames_.size() - 1), longest,
                              commands, last.snapshots - first.snapshots, last.events - first.events);
                lines.push_back(line);
            }
            if (soundSystem_) {
                for (const std::string& r : soundSystem_->recent())
                    lines.push_back("sound: " + (r.size() > 140 ? r.substr(0, 140) + "..." : r));
                std::string loops = "looping:";
                for (const std::string& l : soundSystem_->loopingNow())
                    loops += " " + l;
                lines.push_back(loops.size() > 150 ? loops.substr(0, 150) + "..." : loops);
            }
            if (soundSystem_ && !soundSystem_->missing().empty()) {
                // Their names, the voices' (which the game files lack, as the
                // original's) left out, then why the last one failed.
                std::string names;
                size_t voices = 0;
                for (const std::string& n : soundSystem_->missing()) {
                    if (n.rfind("sound/voice/", 0) == 0)
                        ++voices;
                    else
                        names += " " + n;
                }
                lines.push_back(std::to_string(soundSystem_->missing().size()) + " sounds couldn't be played (" +
                                std::to_string(voices) + " voices):" + (names.size() > 120 ? names.substr(0, 120) + "..." : names));
                if (const auto& problems = soundLibrary_.problems(); !problems.empty())
                    lines.push_back("  last: " + (problems.back().size() > 150 ? problems.back().substr(0, 150) + "..."
                                                                               : problems.back()));
            }
            if (world_) {
                const WorldRenderer::Visibility v = world_->visibility();
                std::snprintf(line, sizeof line,
                              "visibility: cluster %d, %zu of %zu surfaces drawn (%zu past the vis data and doors)%s, "
                              "%d areas hidden",
                              v.cluster, v.visible, v.total, v.potentiallyVisible,
                              v.culling ? "" : " (all: no culling)", v.hiddenAreas);
                lines.push_back(line);
                std::snprintf(line, sizeof line, "mirror and portal views drawn so far: %d%s", scene_->portalViews(),
                              renderer_->hasStencil() ? "" : " (none: no stencil buffer)");
                lines.push_back(line);
            }
            std::snprintf(line, sizeof line, "%d areas; sight checks a closed door blocked: %d%s", game_->areaCount(),
                          game_->closedDoorBlocks(), cvars_.integer("cm_noAreas") ? " (cm_noAreas 1: areas ignored)" : "");
            lines.push_back(line);
            if (!shown_.unloadable.empty()) {
                std::string names = "Models not drawn (only .md3 and .mdr):";
                for (const std::string& n : shown_.unloadable)
                    names += " " + n;
                lines.push_back(names.size() > 150 ? names.substr(0, 150) + "..." : names);
            }
        }
        for (const auto* host : {"server", "client"}) {
            const std::map<int, int>* unhandled = nullptr;
            if (host[0] == 's' && game_)
                unhandled = &game_->unhandled();
            else if (host[0] == 'c' && cgame_)
                unhandled = &cgame_->unhandled();
            if (!unhandled || unhandled->empty())
                continue;
            std::string calls = std::string("The ") + host + " game's system calls not implemented yet:";
            for (const auto& [number, count] : *unhandled)
                calls += " " + std::to_string(number);
            lines.push_back(calls);
        }
        if (!notice_.empty())
            lines.push_back(notice_);
    }
    if (nowMs - shownMs_ < kHintMs) {
        lines.push_back("Keys do what they're bound to (set in the menus' Controls). The mouse looks. ~: the console.");
        lines.push_back("Ctrl+PageUp/PageDown: another map, Ctrl+I: info, Ctrl+R: restart, Esc: the menus, Ctrl+Esc: launcher.");
    }
    for (size_t i = 0; i < lines.size(); ++i) {
        const int y = frameH_ - int(lines.size() - i) * lh - 4;
        const int w = std::min(frameW_, int(font::length(lines[i])) * font::kAdvance + 8);
        for (int yy = y - 2; yy < y + lh; ++yy) {
            uint8_t* p = overlay_.row(yy);
            for (int x = 0; x < w; ++x)
                p[x * 4 + 3] = 170;
        }
        font::drawText(overlay_, 4, y, lines[i], i == 0 && !error_.empty() ? Color{255, 120, 120} : Color{230, 230, 230});
    }
}

} // namespace reluva::idtech3
