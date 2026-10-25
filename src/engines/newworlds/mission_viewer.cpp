// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/newworlds/model_render.h"
#include "engines/newworlds/star_system.h"
#include "engines/newworlds/view3d.h"
#include "engines/newworlds/viewers.h"

#include "common/audio/decode.h"
#include "common/font.h"
#include "common/fs.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <fstream>
#include <iterator>

namespace reluva::newworlds {

namespace {

constexpr Color kHeadingColor{255, 220, 90};
constexpr Color kTextColor{230, 230, 230};
constexpr Color kNoteColor{150, 160, 190};
constexpr int kPanelMargin = 12; // round the panel, and inside it
constexpr int kMusicVolume = 160; // under the speech
constexpr float kBriefingTurn = 0.05f / 1000; // radians per millisecond (about 3 degrees a second)

std::string upperCase(std::string s) {
    for (char& c : s)
        c = char(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

const char* sideName(char side) {
    switch (side) {
    case 'K': return "the Klingons";
    case 'R': return "the Romulans";
    default: return "the Federation";
    }
}

} // namespace

MissionViewer::MissionViewer(System& sys, Surface& screen, const Archives& archives, std::vector<Level> levels,
                             audio::Mixer* mixer, const std::filesystem::path& gameDir)
    : sys_(sys), screen_(screen), archives_(archives), missions_(std::move(levels)), mixer_(mixer),
      map_(sys, screen, archives) {
    map_.setLevels(missions_);
    map_.setOverlay([this] { drawBriefing(); });
    map_.setTitleFilter([this](const std::string& mapTitle) {
        mapTitle_ = mapTitle;
        return title(mapTitle);
    });
    fonts_ = loadGameFonts(archives_);
    loadBriefingArt();
    textureFiles_ = listTextureFiles(archives_);
    for (const GameFont& f : fonts_)
        if (f.name == "fnt/fe8pt")
            font_ = &f;

    std::error_code ec;
    if (!gameDir.empty())
        for (std::filesystem::recursive_directory_iterator it(gameDir, ec), end; !ec && it != end; it.increment(ec)) {
            const std::string ext = fs::toLower(it->path().extension().string());
            if (it->is_regular_file(ec) && (ext == ".mp3" || ext == ".wav"))
                mediaFiles_.emplace(upperCase(it->path().stem().string()), it->path());
        }

    // Without any of the missions' maps or text there's nothing to show.
    const bool any = std::any_of(missions_.begin(), missions_.end(), [&](const Level& l) {
        for (const auto& a : archives_) {
            for (const std::string& name : missionMapNames(l.shortName))
                if (a->find(name))
                    return true;
            for (char side : missionSides(l.shortName))
                if (const std::string table = missionTableName(l.shortName, side); !table.empty() && a->find(table))
                    return true;
        }
        return false;
    });
    if (!any)
        missions_.clear();
}

void MissionViewer::activate() {
    if (!shown_) {
        shown_ = true;
        show(0);
        return;
    }
    if (hasMap_)
        map_.activate();
    if (music_)
        startMusic();
    redraw();
    updateTitle();
}

void MissionViewer::deactivate() {
    stopClip();
    if (mixer_)
        mixer_->stop(musicHandle_);
    musicHandle_ = audio::kNoSound;
}

void MissionViewer::show(int index) {
    stopPlay();
    stopClip();
    soundStatus_.clear();
    current_ = index;
    side_ = 0;
    const Level& level = missions_[size_t(current_)];
    hasMap_ = false;
    mapName_.clear();
    mapNote_.clear();
    mapTitle_.clear();
    const std::vector<std::string> names = missionMapNames(level.shortName);
    for (size_t i = 0; i < names.size() && !hasMap_; ++i)
        if (map_.showMap(names[i])) {
            hasMap_ = true;
            mapName_ = names[i];
            if (!level.shortName.empty() && (level.shortName[0] == 'C' || level.shortName[0] == 'c'))
                mapNote_ = "the map its recording fits";
            else if (i > 0)
                mapNote_ = "mission 12's map, whose world the briefing names";
        }
    if (!hasMap_)
        mapNote_ = "no " + names[0] + " in these archives";
    loadSide();
    if (music_)
        startMusic();
    redraw();
    updateTitle();
}

void MissionViewer::loadSide() {
    stopPlay();
    stopScript();
    stopBriefing();
    stopClip();
    page_ = clip_ = 0;
    pages_.clear();
    clips_.clear();
    status_.clear();
    const Level& level = missions_[size_t(current_)];
    const char side = missionSides(level.shortName)[size_t(side_)];
    tableName_ = missionTableName(level.shortName, side);

    table_ = StringTable{};
    StringTable& table = table_;
    if (tableName_.empty())
        status_ = "no briefing known for this mission";
    else {
        status_ = "no " + tableName_ + " in these archives";
        for (const auto& a : archives_)
            if (const CatEntry* e = a->find(tableName_)) {
                std::vector<uint8_t> data;
                if (Error err = a->read(*e, data))
                    status_ = err.message;
                else if (Error err2 = table.parse(data))
                    status_ = err2.message;
                else
                    status_.clear();
                break;
            }
    }
    // Pages in the table's order: a "~HEADING~" string each, and the plain
    // strings between them (the title and stardate first, the objectives
    // as listed in game last) run together.
    for (const StringTable::String& s : table.strings()) {
        const size_t close = s.text.size() > 1 && s.text[0] == '~' ? s.text.find('~', 1) : std::string::npos;
        if (close != std::string::npos) {
            std::string heading = s.text.substr(1, close - 1);
            std::replace(heading.begin(), heading.end(), '\t', ' ');
            pages_.push_back({std::move(heading), s.text.substr(close + 1), s.id});
            continue;
        }
        if (pages_.empty() || !pages_.back().heading.empty())
            pages_.push_back({{}, {}, s.id});
        Page& page = pages_.back();
        page.text += (page.text.empty() ? "" : "\n") + s.text;
    }

    for (const auto& [stem, path] : mediaFiles_)
        if (fs::toLower(path.extension().string()) == ".mp3" && isMissionClip(stem, level.shortName, side))
            clips_.push_back(stem);
    std::sort(clips_.begin(), clips_.end(), clipBefore);
    script_ = briefingScript(level.shortName, side, table_, clips_);
    loadStarSystem();
    startColony();
}

void MissionViewer::startColony() {
    colony_.reset();
    colonySite_ = {};
    if (hasMap_) {
        const Level& level = missions_[size_t(current_)];
        const char side = missionSides(level.shortName)[size_t(side_)];
        colonySite_ = colonySite(level.shortName, side);
        if (colonySite_.a >= 0) {
            ColonyGround ground{map_.blocksWide(), map_.blocksHigh(),
                                [this](int bx, int by) { return map_.depositAt(bx, by); },
                                [this](int a, int b) { return map_.groundAt(a, b); },
                                [this](const std::string& model) { return map_.modelSize(model); }};
            colony_ = std::make_unique<ColonyLayout>(side, std::move(ground));
            if (!colony_->start(colonySite_.a, colonySite_.b))
                colony_.reset();
        }
    }
    showColony();
}

void MissionViewer::showColony() {
    std::vector<MapViewer::Building> buildings;
    // In play, each has the side's construction bee to work on it while
    // it goes up.
    std::string bee;
    if (economy_) {
        const char side = missionSides(missions_[size_t(current_)].shortName)[size_t(side_)];
        bee = side == 'K' ? "geo/kln/conbee.geo" : side == 'R' ? "geo/rom/rcb.geo" : "geo/fed/fconbee.geo";
    }
    if (colony_ && colonyShown_)
        for (const ColonyBuilding& c : colony_->buildings())
            buildings.push_back({c.model, c.a, c.b, 0, 1, bee});
    map_.setBuildings(buildings);
    showProgress();
    map_.setPaved(colony_ && colonyShown_ ? colony_->pad() : std::vector<std::pair<int, int>>{});
}

bool MissionViewer::hubPosition(float& x, float& y) const {
    if (!colony_ || colony_->buildings().empty())
        return false;
    const ColonyBuilding& hub = colony_->buildings()[0];
    x = float(hub.b) / 8 * MapViewer::kBlockUnits;
    y = float(hub.a) / 8 * MapViewer::kBlockUnits;
    return true;
}

// Play: the colony's starting units by the hub (four of the side's ground
// scouts, as F1 and R1 start with four vehicles of one kind), the game's
// camera over it, the interface up.
void MissionViewer::startPlay() {
    if (!hasMap_ || running_)
        return;
    const Level& level = missions_[size_t(current_)];
    const char side = missionSides(level.shortName)[size_t(side_)];
    world_ = std::make_unique<World>(map_.terrain());
    // The colony as it starts: the hub alone, and the stock every
    // recording started with (the Romulans a little less).
    colonyShown_ = true;
    startColony();
    static constexpr Stock kStart{500, 1000, 2300, 500, 4000, 5000}, kRomulan{500, 988, 2211, 497, 3500, 5000};
    ColonyMap ground{[this](int tx, int ty) { return map_.depositAt(tx / 2, ty / 2); },
                     [this](int tx, int ty) { return map_.depositAmount(tx / 2, ty / 2); }};
    economy_ = std::make_unique<Economy>(side, side == 'R' ? kRomulan : kStart, 20, 50, std::move(ground));
    structureIds_.clear();
    if (colony_)
        for (const ColonyBuilding& c : colony_->buildings())
            structureIds_.push_back(economy_->addBuilt(c.name, float(c.b) / 8 * MapViewer::kBlockUnits,
                                                       float(c.a) / 8 * MapViewer::kBlockUnits));
    // The mission's script, from its start, if Reluva has one.
    run_.reset();
    walk_.clear();
    log_.clear();
    walkNote_.clear();
    if (const MissionScript* script = findMissionScript(level.shortName, side)) {
        run_ = std::make_unique<MissionRun>(*script, findMissionScript("generic", 'F'));
        const ScriptEvent start{"start", {}};
        deliver(start, run_->fire(start));
    }
    buildMenu_ = false;
    placing_.clear();
    float hx = map_.worldWidth() / 2, hy = map_.worldHeight() / 2;
    hubPosition(hx, hy);
    // In a row south of the hub, each on the nearest open tile.
    for (int i = 0; i < 4; ++i) {
        const float x = hx + (float(i) - 1.5f) * 2500, y = hy - 1.4f * kTile;
        const auto path = world_->findPath(x, y, x, y);
        const float px = path.empty() ? x : path.back().first, py = path.empty() ? y : path.back().second;
        world_->addUnit(side, groundScout(side), px, py, 3.1416f / 2);
    }
    playing_ = true;
    interface_ = true;
    briefing_ = false;
    boxing_ = tricorderFull_ = false;
    lastTickMs_ = nowMs_;
    map_.centreOn(hx, hy);
    map_.setView(MapViewer::View::BirdsEye);
    showUnits();
    redraw();
    updateTitle();
}

void MissionViewer::stopPlay() {
    if (!playing_)
        return;
    playing_ = false;
    world_.reset();
    economy_.reset();
    structureIds_.clear();
    run_.reset();
    log_.clear();
    walkNote_.clear();
    buildMenu_ = false;
    placing_.clear();
    map_.setGhost({}, 0, 0, false);
    map_.setUnits({});
    map_.unlock();
    redraw();
    updateTitle();
}

void MissionViewer::showUnits() {
    std::vector<MapViewer::UnitDraw> draw;
    if (world_)
        for (const Unit& u : world_->units())
            draw.push_back({u.kind.model, u.x, u.y, u.heading, u.selected});
    map_.setUnits(std::move(draw));
}

std::vector<const BuildingType*> MissionViewer::buildMenu() const {
    std::vector<const BuildingType*> out;
    if (!economy_)
        return out;
    const char side = missionSides(missions_[size_t(current_)].shortName)[size_t(side_)];
    for (const BuildingType& t : buildingTypes())
        if (t.needs != "-" && !buildingModel(t.id, side).empty())
            out.push_back(&t);
    return out;
}

void MissionViewer::choose(const BuildingType& t) {
    if (!economy_ || !economy_->available(t))
        return;
    placing_ = t.id;
    buildMenu_ = false;
    ghostA_ = ghostB_ = -1;
    updateGhost();
}

// The hologram follows the pointer, on the tile grid, green where the
// building can go.
void MissionViewer::updateGhost() {
    if (placing_.empty() || !colony_)
        return;
    float x = 0, y = 0;
    int a = -1, b = -1;
    bool ok = false;
    if (map_.screenToGround(mouseX_, mouseY_, x, y)) {
        const int pa = int(y / MapViewer::kBlockUnits * 8), pb = int(x / MapViewer::kBlockUnits * 8);
        if (!colony_->spotAt(placing_, pa, pb, a, b, ok))
            a = b = -1;
    }
    if (a == ghostA_ && b == ghostB_ && ok == ghostOk_)
        return;
    ghostA_ = a, ghostB_ = b, ghostOk_ = ok;
    const char side = missionSides(missions_[size_t(current_)].shortName)[size_t(side_)];
    map_.setGhost(a < 0 ? std::string() : buildingModel(placing_, side), a, b, ok);
}

void MissionViewer::placeBuilding() {
    if (placing_.empty() || !colony_ || !economy_ || !ghostOk_ || ghostA_ < 0)
        return;
    const float x = float(ghostB_) / 8 * MapViewer::kBlockUnits, y = float(ghostA_) / 8 * MapViewer::kBlockUnits;
    const int id = economy_->order(placing_, x, y, int(x / kTile), int(y / kTile));
    if (id == 0 || !colony_->addAt(placing_, ghostA_, ghostB_))
        return;
    structureIds_.push_back(id);
    placing_.clear();
    map_.setGhost({}, 0, 0, false);
    showColony();
}

void MissionViewer::showProgress() {
    if (!economy_)
        return;
    const auto& all = economy_->structures();
    for (size_t i = 0; i < structureIds_.size(); ++i)
        for (const ColonyStructure& c : all)
            if (c.id == structureIds_[i])
                map_.setBuildingProgress(i, c.progress);
}

bool MissionViewer::playEvent(const Event& ev) {
    if (ev.type == EventType::MouseMove || ev.type == EventType::MouseDown || ev.type == EventType::MouseUp)
        mouseX_ = ev.mouseX, mouseY_ = ev.mouseY;
    const bool ctrl = (ev.mods & keymod::Ctrl) != 0, shift = (ev.mods & keymod::Shift) != 0;
    switch (ev.type) {
    case EventType::KeyDown:
        if (ev.key == key::Escape && (buildMenu_ || !placing_.empty())) {
            buildMenu_ = false;
            placing_.clear();
            map_.setGhost({}, 0, 0, false);
            break;
        }
        if (ev.key == key::Escape || ev.key == key::F1 + 2) {
            stopPlay();
            return true;
        }
        if (ev.key == 'b') {
            buildMenu_ = !buildMenu_;
            placing_.clear();
            map_.setGhost({}, 0, 0, false);
            break;
        }
        if (buildMenu_ && ev.key >= '0' && ev.key <= '9') {
            // 1-9, then 0 for the tenth.
            const size_t n = ev.key == '0' ? 9 : size_t(ev.key - '1');
            const auto menu = buildMenu();
            if (n < menu.size())
                choose(*menu[n]);
            break;
        }
        if (ev.key == key::Tab) {
            tricorderFull_ = !tricorderFull_;
            break;
        }
        if (ev.key == key::Home) {
            float x = 0, y = 0;
            if (hubPosition(x, y))
                map_.centreOn(x, y);
            break;
        }
        if (ev.key == 'v' && shift) {
            // To the selected vehicles, or the first one.
            float x = 0, y = 0;
            if (world_->groupCentre(0, x, y))
                map_.centreOn(x, y);
            else if (!world_->units().empty())
                map_.centreOn(world_->units()[0].x, world_->units()[0].y);
            break;
        }
        if (ev.key >= '1' && ev.key <= '9') {
            const int n = int(ev.key - '0');
            float x = 0, y = 0;
            if (ctrl)
                world_->setGroup(n);
            else if (shift && world_->groupCentre(n, x, y))
                map_.centreOn(x, y);
            else if (!shift)
                world_->selectGroup(n);
            showUnits();
            break;
        }
        if (ev.key == 'i') {
            interface_ = !interface_;
            break;
        }
        if (map_.gameKey(ev.key, mouseX_, mouseY_))
            break;
        return true; // the data viewer's keys don't apply in play
    case EventType::MouseDown:
        if (!placing_.empty() && !ctrl) {
            // Left click builds where the hologram is green; right click
            // puts it away.
            if (ev.button == MouseButton::Left)
                placeBuilding();
            else if (ev.button == MouseButton::Right) {
                placing_.clear();
                map_.setGhost({}, 0, 0, false);
            }
            break;
        }
        if (ev.button == MouseButton::Left && ctrl) {
            map_.zoomAt(ev.mouseX, ev.mouseY, true);
        } else if (ev.button == MouseButton::Right && ctrl) {
            map_.zoomAt(ev.mouseX, ev.mouseY, false);
        } else if (ev.button == MouseButton::Right) {
            // Right-click drops the selection, or the camera's lock.
            if (map_.locked())
                map_.unlock();
            else
                world_->clearSelection();
            showUnits();
        } else if (ev.button == MouseButton::Left) {
            boxing_ = true;
            boxX_ = ev.mouseX, boxY_ = ev.mouseY;
            return true;
        }
        break;
    case EventType::MouseMove:
        if (!placing_.empty()) {
            updateGhost();
            return true;
        }
        if (!boxing_)
            return true;
        break;
    case EventType::MouseUp: {
        if (ev.button != MouseButton::Left || !boxing_)
            return true;
        boxing_ = false;
        const Level& level = missions_[size_t(current_)];
        const char side = missionSides(level.shortName)[size_t(side_)];
        const int x0 = std::min(boxX_, ev.mouseX), x1 = std::max(boxX_, ev.mouseX);
        const int y0 = std::min(boxY_, ev.mouseY), y1 = std::max(boxY_, ev.mouseY);
        std::vector<int> picked;
        if (x1 - x0 > 6 || y1 - y0 > 6) {
            // A box: the side's units inside it.
            for (const Unit& u : world_->units()) {
                int sx = 0, sy = 0;
                if (u.side == side && map_.worldToScreen(u.x, u.y, map_.groundAtWorld(u.x, u.y), sx, sy) &&
                    sx >= x0 && sx <= x1 && sy >= y0 && sy <= y1)
                    picked.push_back(u.id);
            }
            world_->select(picked);
        } else {
            // A click: the nearest unit to it, if close; else, with units
            // selected, they go there; a double click on the ground locks
            // the camera on it.
            int best = -1, bestD = 18 * 18;
            for (const Unit& u : world_->units()) {
                int sx = 0, sy = 0;
                if (!map_.worldToScreen(u.x, u.y, map_.groundAtWorld(u.x, u.y) + 300, sx, sy))
                    continue;
                const int d = (sx - ev.mouseX) * (sx - ev.mouseX) + (sy - ev.mouseY) * (sy - ev.mouseY);
                if (d < bestD && u.side == side)
                    best = u.id, bestD = d;
            }
            float gx = 0, gy = 0;
            if (best >= 0)
                world_->select({best});
            else if (map_.screenToGround(ev.mouseX, ev.mouseY, gx, gy)) {
                if (!world_->selection().empty())
                    world_->moveSelected(gx, gy);
                else if (ev.clicks >= 2)
                    map_.lockOn(gx, gy);
            }
        }
        showUnits();
        break;
    }
    case EventType::MouseWheel: map_.handleEvent(ev); return true;
    default: return true;
    }
    redraw();
    updateTitle();
    return true;
}

void MissionViewer::handleEvent(const Event& ev) {
    if (missions_.empty())
        return;
    if (playing_ && playEvent(ev))
        return;
    if (ev.type == EventType::KeyDown && ev.key == key::F1 + 2) {
        startPlay();
        return;
    }
    if (ev.type != EventType::KeyDown) {
        if (hasMap_)
            map_.handleEvent(ev);
        return;
    }
    const int n = int(missions_.size());
    const int sides = int(missionSides(missions_[size_t(current_)].shortName).size());
    switch (ev.key) {
    case key::Right: show((current_ + 1) % n); return;
    case key::Left: show((current_ + n - 1) % n); return;
    case key::Down:
    case key::PageDown:
        if (page_ + 1 < int(pages_.size()))
            ++page_, briefing_ = true;
        break;
    case key::Up:
    case key::PageUp:
        if (page_ > 0)
            --page_, briefing_ = true;
        break;
    case 'b': briefing_ = !briefing_; break;
    case key::Return:
        if (running_)
            startStep(step_ + 1);
        else
            startBriefing();
        break;
    case key::Backspace: stopBriefing(); break;
    case 'x': stepScript(); break;
    case 'z': stopScript(); break;
    case 'i': interface_ = !interface_; break;
    case 'c':
        colonyShown_ = !colonyShown_;
        showColony();
        break;
    case 'v':
        if (colony_ && !colony_->buildings().empty())
            map_.lookAt(colony_->buildings()[0].a, colony_->buildings()[0].b, true);
        break;
    case 'j':
        if (sides < 2)
            return;
        side_ = (side_ + 1) % sides;
        loadSide();
        break;
    case key::Space:
        if (clipPlaying())
            stopClip();
        else
            playClip(clip_);
        break;
    case 'n':
        if (!clips_.empty())
            playClip((clip_ + 1) % int(clips_.size()));
        break;
    case 'p':
        if (!clips_.empty())
            playClip((clip_ + int(clips_.size()) - 1) % int(clips_.size()));
        break;
    case 'm':
        music_ = !music_;
        soundStatus_.clear();
        if (music_)
            startMusic();
        else
            stopMusic();
        break;
    default:
        if (hasMap_)
            map_.handleEvent(ev);
        return;
    }
    redraw();
    updateTitle();
}

void MissionViewer::update(uint64_t nowMs) {
    if (missions_.empty())
        return;
    nowMs_ = nowMs;
    // The briefing moves on when its step's clip ends, or its time is up.
    if (running_ && (stepEndMs_ ? nowMs >= stepEndMs_ : !clipPlaying()))
        startStep(step_ + 1);
    // The galaxy and models turn slowly too.
    if (running_ && !stepOnMap_ &&
        (script_[size_t(step_)].view == BriefingView::Galaxy || script_[size_t(step_)].view == BriefingView::Model) &&
        nowMs - lastTurnMs_ >= 100) {
        lastTurnMs_ = nowMs;
        dirty_ = true;
    }
    // The briefing's map views turn slowly about their target.
    if (running_ && stepOnMap_ && nowMs - lastTurnMs_ >= 100) {
        map_.turn(float(nowMs - lastTurnMs_) * kBriefingTurn);
        lastTurnMs_ = nowMs;
    }
    if (playing_ && world_) {
        const double dt = double(nowMs - lastTickMs_) / 1000.0;
        lastTickMs_ = nowMs;
        if (world_->advance(std::min(dt, 0.25)))
            showUnits();
        std::vector<ScriptEvent> happened;
        if (economy_) {
            happened = economy_->advance(std::min(dt, 0.25));
            showProgress();
        }
        // The script hears what happened, and its timers run.
        if (run_ && !run_->finished()) {
            for (const ScriptEvent& e : happened)
                deliver(e, run_->fire(e));
            const ScriptEvent tick{"timer", {}};
            deliver(tick, run_->advance(std::min(dt, 0.25)));
        }
        updateGhost(); // the camera may have moved under the pointer
        if (economy_ && nowMs / 1000 != (nowMs - uint64_t(dt * 1000)) / 1000)
            redraw(); // the panels' figures, once a second
    }
    if (mapInView())
        map_.update(nowMs);
    else if (dirty_) {
        if (running_)
            drawBackdrop(script_[size_t(step_)].view);
        else
            drawNoMap();
        drawBriefing();
        dirty_ = false;
    }
    // A clip ending changes the title.
    const bool playing = clipPlaying();
    if (playing != wasPlaying_)
        updateTitle();
    wasPlaying_ = playing;
}

void MissionViewer::redraw() {
    if (mapInView())
        map_.redraw();
    else
        dirty_ = true;
}

int MissionViewer::measure(std::string_view text) const {
    return font_ ? font_->textWidth(text) : int(font::length(text)) * font::kAdvance;
}

int MissionViewer::lineHeight() const { return font_ ? font_->lineHeight : font::kLineHeight; }

void MissionViewer::drawText(int x, int y, std::string_view text, Color color) {
    if (font_)
        font_->drawText(screen_, x, y, text, color);
    else
        font::drawText(screen_, x, y, text, color);
}

void MissionViewer::drawNoMap() {
    for (int y = 0; y < screen_.h; ++y) {
        uint8_t* row = screen_.row(y);
        for (int x = 0; x < screen_.w; ++x, row += 4)
            row[0] = 22, row[1] = 26, row[2] = 38, row[3] = 255;
    }
    drawText(kPanelMargin, kPanelMargin, mapNote_, kNoteColor);
}

void MissionViewer::drawBriefing() {
    if (playing_) {
        if (interface_)
            drawInterface();
        drawPlay();
        return;
    }
    if (interface_ && !running_) {
        drawInterface();
        return;
    }
    if (!briefing_)
        return;
    // Lay the page out in lines, word-wrapped to the panel.
    struct Line {
        std::string text;
        Color color;
    };
    std::vector<Line> lines;
    const int width = screen_.w - 4 * kPanelMargin;
    auto wrap = [&](const std::string& text, Color color) {
        size_t start = 0;
        while (start <= text.size()) {
            const size_t end = std::min(text.find('\n', start), text.size());
            const std::string para = text.substr(start, end - start);
            std::string line;
            size_t i = 0;
            while (i < para.size()) {
                const size_t space = std::min(para.find(' ', i), para.size());
                const std::string word = para.substr(i, space - i);
                const std::string next = line.empty() ? word : line + " " + word;
                if (!line.empty() && measure(next) > width) {
                    lines.push_back({line, color});
                    line = word;
                } else
                    line = next;
                i = space + 1;
            }
            lines.push_back({line, color});
            start = end + 1;
        }
    };
    if (run_ && !running_) {
        // The mission's objectives, then its latest messages.
        wrap("OBJECTIVES", kHeadingColor);
        const MissionScript* script = findMissionScript(
            missions_[size_t(current_)].shortName, missionSides(missions_[size_t(current_)].shortName)[size_t(side_)]);
        for (const MissionScript::Objective& o : script->objectives) {
            const MissionRun::State st = run_->objectiveState(o.id);
            if (st == MissionRun::State::Hidden)
                continue;
            const std::string* text = table_.find(o.id);
            wrap(std::string(st == MissionRun::State::Complete ? "[done] " : "[    ] ") +
                     (text ? *text : "objective " + std::to_string(o.id)),
                 st == MissionRun::State::Complete ? kNoteColor : kTextColor);
        }
        if (!log_.empty())
            wrap("COMMUNICATIONS " + std::to_string(log_.size()), kHeadingColor);
        for (size_t i = log_.size() > 4 ? log_.size() - 4 : 0; i < log_.size(); ++i)
            wrap(log_[i].first + ": " + log_[i].second, kTextColor);
        lines.push_back({walkNote_ + " (X: next; Z: stop)", kNoteColor});
    } else if (running_) {
        // The step's page, if it has one.
        const uint32_t id = script_[size_t(step_)].page;
        for (const Page& page : pages_)
            if (id && page.id == id) {
                if (!page.heading.empty())
                    wrap(page.heading, kHeadingColor);
                wrap(page.text, kTextColor);
                break;
            }
        if (!lines.empty())
            lines.push_back({"briefing " + std::to_string(step_ + 1) + "/" + std::to_string(script_.size()) +
                                 " (Enter: next; Backspace: stop)",
                             kNoteColor});
    } else if (!status_.empty())
        wrap(status_, kNoteColor);
    else if (!pages_.empty()) {
        const Page& page = pages_[size_t(page_)];
        if (!page.heading.empty())
            wrap(page.heading, kHeadingColor);
        wrap(page.text, kTextColor);
        lines.push_back({"page " + std::to_string(page_ + 1) + "/" + std::to_string(pages_.size()) +
                             " (Up/Down; B hides)",
                         kNoteColor});
    }
    if (lines.empty())
        return;

    // A dark panel along the bottom, as tall as the page (or the screen).
    const int lh = lineHeight();
    const int maxLines = std::max(1, (screen_.h - 4 * kPanelMargin) / lh);
    if (int(lines.size()) > maxLines) {
        lines.resize(size_t(maxLines));
        lines.back() = {"...", kNoteColor};
    }
    const int height = int(lines.size()) * lh + 2 * kPanelMargin;
    const int top = screen_.h - kPanelMargin - height;
    for (int y = std::max(top, 0); y < screen_.h - kPanelMargin; ++y) {
        uint8_t* px = screen_.row(y) + kPanelMargin * 4;
        for (int x = kPanelMargin; x < screen_.w - kPanelMargin; ++x, px += 4) {
            px[0] = uint8_t(px[0] / 4 + 8);
            px[1] = uint8_t(px[1] / 4 + 12);
            px[2] = uint8_t(px[2] / 4 + 28);
        }
    }
    for (size_t i = 0; i < lines.size(); ++i)
        drawText(2 * kPanelMargin, top + kPanelMargin + int(i) * lh, lines[i].text, lines[i].color);
}

std::string MissionViewer::title(const std::string& mapTitle) const {
    if (missions_.empty())
        return {};
    const Level& level = missions_[size_t(current_)];
    const std::vector<char> sides = missionSides(level.shortName);
    std::string t = "New Worlds missions " + std::to_string(current_ + 1) + "/" + std::to_string(missions_.size()) +
                    ": " + level.shortName + " \"" + level.title + "\", " + std::to_string(level.maxPlayers) +
                    (level.maxPlayers == 1 ? " player" : " players") + (level.commentedOut ? " (not listed)" : "");
    if (sides.size() > 1)
        t += std::string(", as ") + sideName(sides[size_t(side_)]) + " (J: side)";
    t += hasMap_ ? "; " + mapName_ : "";
    if (!mapNote_.empty())
        t += " (" + mapNote_ + ")";
    if (clips_.empty())
        t += "; no voice clips";
    else {
        t += "; voice " + std::to_string(clip_ + 1) + "/" + std::to_string(clips_.size()) + " " + clips_[size_t(clip_)];
        t += clipPlaying() ? " [playing]" : " (Space: play)";
    }
    if (colony_)
        t += "; colony at " + std::to_string(colonySite_.a) + ":" + std::to_string(colonySite_.b) + " (" +
             colonySite_.source + "), " + std::to_string(colony_->buildings().size()) + " building" +
             (colony_->buildings().size() == 1 ? "" : "s") + (colonyShown_ ? "" : ", hidden (C)");
    else if (hasMap_)
        t += "; no colony site known";
    if (playing_ && world_)
        t += "; playing: " + std::to_string(world_->selection().size()) + " selected (F3 or Escape: stop)";
    if (playing_ && economy_)
        t += "; power " + std::to_string(int(economy_->powerSupply())) + "/" +
             std::to_string(int(economy_->powerDemand())) + " (B: build)";
    if (run_)
        t += "; script: " + walkNote_;
    if (running_)
        t += "; briefing step " + std::to_string(step_ + 1) + "/" + std::to_string(script_.size()) +
             (hasRecordedBriefing(level.shortName, sides[size_t(side_)]) ? "" : " (by the general rule)");
    if (music_ && !musicName_.empty())
        t += " [music " + musicName_ + "]";
    if (!soundStatus_.empty())
        t += " [" + soundStatus_ + "]";
    // Then the map's own details: its archive, size and view settings.
    if (const size_t p = mapTitle.find(" ("); hasMap_ && p != std::string::npos)
        t += mapTitle.substr(p);
    return t;
}

void MissionViewer::updateTitle() {
    if (!missions_.empty())
        sys_.setWindowTitle(title(mapTitle_));
}

std::shared_ptr<const audio::Sound> MissionViewer::loadSound(const std::string& stem) {
    const auto it = mediaFiles_.find(upperCase(stem));
    if (it == mediaFiles_.end()) {
        soundStatus_ = "no " + stem + " in the game folder";
        return nullptr;
    }
    std::ifstream in(it->second, std::ios::binary);
    const std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto sound = std::make_shared<audio::Sound>();
    if (Error err = audio::decodeSound(data, *sound)) {
        soundStatus_ = stem + ": " + err.message;
        return nullptr;
    }
    return sound;
}

// What play adds over the map: the full-screen Tricorder (Tab), with the
// units on it, and the box a left drag draws.
void MissionViewer::drawPlay() {
    if (tricorderFull_ && hasMap_) {
        const int size = std::min(screen_.w, screen_.h) - 40;
        const int x0 = (screen_.w - size) / 2, y0 = (screen_.h - size) / 2;
        for (int y = 0; y < screen_.h; ++y) {
            uint8_t* row = screen_.row(y);
            for (int x = 0; x < screen_.w; ++x, row += 4)
                row[0] = uint8_t(row[0] / 4), row[1] = uint8_t(row[1] / 4), row[2] = uint8_t(row[2] / 4);
        }
        map_.drawMinimap(screen_, x0, y0, size, size);
        if (world_)
            for (const Unit& u : world_->units()) {
                int px = 0, py = 0;
                map_.minimapPoint(u.x, u.y, x0, y0, size, size, px, py);
                for (int dy = -2; dy <= 2; ++dy)
                    for (int dx = -2; dx <= 2; ++dx)
                        if (px + dx >= 0 && py + dy >= 0 && px + dx < screen_.w && py + dy < screen_.h) {
                            uint8_t* p = screen_.row(py + dy) + (px + dx) * 4;
                            p[0] = u.selected ? 255 : 120, p[1] = u.selected ? 150 : 255, p[2] = u.selected ? 30 : 120;
                        }
            }
    }
    if (boxing_ && (std::abs(mouseX_ - boxX_) > 6 || std::abs(mouseY_ - boxY_) > 6)) {
        const int x0 = std::min(boxX_, mouseX_), x1 = std::max(boxX_, mouseX_);
        const int y0 = std::min(boxY_, mouseY_), y1 = std::max(boxY_, mouseY_);
        auto put = [&](int x, int y) {
            if (x >= 0 && y >= 0 && x < screen_.w && y < screen_.h) {
                uint8_t* p = screen_.row(y) + x * 4;
                p[0] = 120, p[1] = 255, p[2] = 120;
            }
        };
        for (int x = x0; x <= x1; ++x)
            put(x, y0), put(x, y1);
        for (int y = y0; y <= y1; ++y)
            put(x0, y), put(x1, y);
    }
}

// The in-game interface, laid out as the recordings show it on the 640 x
// 480 screen; see docs/formats/newworlds/gui.md#the-in-game-interface.
void MissionViewer::drawInterface() {
    const Level& level = missions_[size_t(current_)];
    const char side = missionSides(level.shortName)[size_t(side_)];
    if (uiSide_ != side) {
        uiSide_ = side;
        ui_ = GameUi{};
        ui_.load(archives_, side);
    }
    if (!ui_.loaded()) {
        drawText(kPanelMargin, kPanelMargin, "no GUI archive for this side", kNoteColor);
        return;
    }
    const int lh = ui_.smallHeight();
    const Color text = ui_.textColor(), bright = ui_.brightColor();
    // The right-hand panels are about 100 pixels wide in the recordings;
    // wide enough here for their longest lines in the font we have.
    const int sideW = std::max({100, ui_.smallWidth("COLONY SIZE = 20/50"), ui_.smallWidth("TRANSALUMIN = 5000"),
                                ui_.smallWidth("TERRAIN AT <000:000>")}) +
                      22;

    // The compass strip along the top: a tick every 5 degrees, a label
    // every 10, N E S W at the quarters, centred on the camera's heading.
    if (hasMap_) {
        const float heading = map_.heading();
        const int cx = screen_.w / 2, span = 120, pxPerDeg = 2;
        for (int d = -span / 2; d <= span / 2; ++d) {
            int deg = int(std::lround(heading)) + d;
            deg = ((deg % 360) + 360) % 360;
            const int x = cx + d * pxPerDeg;
            if (deg % 10 == 0) {
                static const char* kPoints[] = {"N", "E", "S", "W"};
                const std::string label = deg % 90 == 0 ? kPoints[deg / 90]
                                                        : (deg < 100 ? "0" : "") + std::to_string(deg);
                ui_.drawSmall(screen_, x - ui_.smallWidth(label) / 2, 2, label, deg % 90 == 0 ? bright : text);
            } else if (deg % 5 == 0) {
                ui_.drawSmall(screen_, x - 1, 2, "+", text);
            }
        }
    }

    // NEW WORLDS: the colony panel: the colony's size and stock (in play
    // the economy's; else what every recording started with), the
    // selection, or the build menu.
    {
        const auto menu = buildMenu_ ? buildMenu() : std::vector<const BuildingType*>{};
        const int lines = buildMenu_ ? int(menu.size()) + 1 : 7;
        const int x = screen_.w - sideW, y = 0, w = sideW, h = 24 + lines * lh + 8;
        ui_.drawPanel(screen_, x, y, w, h, "NEW WORLDS");
        // The Romulans start with a little less (most of their recordings).
        static const char* const kNames[] = {"DILITHIUM", "POLYCOMP", "THERMOCONC", "TRANSALUMIN", "NITRIUM", "DURANIUM"};
        static constexpr int kStart[] = {500, 1000, 2300, 500, 4000, 5000}, kRomulan[] = {500, 988, 2211, 497, 3500, 5000};
        std::pair<const char*, int> kStock[6];
        for (int i = 0; i < 6; ++i)
            kStock[i] = {kNames[i], side == 'R' ? kRomulan[i] : kStart[i]};
        int ty = y + 16;
        const std::vector<int> sel = world_ ? world_->selection() : std::vector<int>{};
        const BuildingType* placing = placing_.empty() ? nullptr : buildingType(placing_);
        if (buildMenu_) {
            // The menu: each type with its key; dim if what it needs
            // doesn't stand yet, plain if the stock can't pay for it.
            ui_.drawSmall(screen_, x + 10, ty, "BUILD", bright);
            const Color dim{uint8_t(text.r / 2), uint8_t(text.g / 2), uint8_t(text.b / 2)};
            for (size_t i = 0; i < menu.size(); ++i) {
                const BuildingType& t = *menu[i];
                const Color c = !economy_->available(t) ? dim : economy_->affordable(t) ? bright : text;
                ui_.drawSmall(screen_, x + 10, ty + lh * int(i + 1), std::to_string((i + 1) % 10) + " " + t.panel, c);
            }
        } else if (placing) {
            // Placing: the building and what it costs.
            ui_.drawSmall(screen_, x + 10, ty, placing->panel, bright);
            int line = 1;
            for (int m = 0; m < kMaterials; ++m)
                if (placing->cost[size_t(m)] > 0)
                    ui_.drawSmall(screen_, x + 10, ty + lh * line++,
                                  std::string(materialName(m)) + " = " + std::to_string(placing->cost[size_t(m)]),
                                  economy_->stock()[size_t(m)] >= placing->cost[size_t(m)] ? text : Color{255, 80, 60});
        } else if (sel.size() == 1) {
            // One unit: its name and state, as the game's panel.
            const Unit* u = world_->unit(sel[0]);
            ui_.drawSmall(screen_, x + 10, ty, u->kind.name, bright);
            ui_.drawSmall(screen_, x + 10, ty + lh, u->moving() ? "EN ROUTE" : "READY", text);
        } else if (sel.size() > 1) {
            ui_.drawSmall(screen_, x + 10, ty, "MULTISELECTION", bright);
            for (size_t i = 0; i < sel.size() && i < 6; ++i)
                ui_.drawSmall(screen_, x + 10, ty + lh * int(i + 1), world_->unit(sel[i])->kind.name, text);
        } else {
            if (economy_) {
                for (int i = 0; i < 6; ++i)
                    kStock[i].second = economy_->stock()[size_t(i)];
                ui_.drawSmall(screen_, x + 10, ty,
                              "COLONY SIZE = " + std::to_string(economy_->crew()) + "/" +
                                  std::to_string(economy_->capacity()),
                              bright);
            } else {
                ui_.drawSmall(screen_, x + 10, ty, "COLONY SIZE = 20/50", bright);
            }
            for (const auto& [name, n] : kStock) {
                ty += lh;
                ui_.drawSmall(screen_, x + 10, ty, std::string(name) + " = " + std::to_string(n), text);
            }
        }
    }

    // TRICORDER: the map small, and the camera's position.
    {
        const int x = screen_.w - sideW, w = sideW, h = 102, y = screen_.h - h;
        ui_.drawPanel(screen_, x, y, w, h, "TRICORDER");
        if (hasMap_) {
            map_.drawMinimap(screen_, x + 12, y + 14, w - 24, h - 30 - lh);
            if (world_)
                for (const Unit& u : world_->units()) {
                    int px = 0, py = 0;
                    map_.minimapPoint(u.x, u.y, x + 12, y + 14, w - 24, h - 30 - lh, px, py);
                    for (int dy = 0; dy < 2; ++dy)
                        for (int dx = 0; dx < 2; ++dx)
                            if (px + dx >= 0 && py + dy >= 0 && px + dx < screen_.w && py + dy < screen_.h) {
                                uint8_t* p = screen_.row(py + dy) + (px + dx) * 4;
                                p[0] = 120, p[1] = 255, p[2] = 120;
                            }
                }
            int a = 0, b = 0;
            map_.cameraPosition(a, b);
            auto pad = [](int v) {
                std::string s = std::to_string(v);
                return std::string(s.size() < 3 ? 3 - s.size() : 0, '0') + s;
            };
            ui_.drawSmall(screen_, x + 10, y + h - 12 - lh, "TERRAIN AT <" + pad(a) + ":" + pad(b) + ">", text);
        }
    }

    // COMMUNICATIONS: the script's latest message, with its sender's portrait.
    {
        const int w = 220, h = 70, x = (screen_.w - sideW - w) / 2 + 10, y = screen_.h - h;
        ui_.drawPanel(screen_, x, y, w, h, "COMMUNICATIONS");
        if (log_.empty()) {
            ui_.drawSmall(screen_, x + 60, y + 16, "NO MESSAGES ON FILE.", text);
        } else {
            const auto& [sender, message] = log_.back();
            const std::string natives = level.shortName == "C9" ? "orion" : "taubat";
            if (const Texture* p = ui_.portraitFor(sender, natives))
                ui_.drawPicture(screen_, *p, x + 10, y + 13, 44, 41);
            std::string up = sender + ":";
            std::transform(up.begin(), up.end(), up.begin(), [](unsigned char c) { return char(std::toupper(c)); });
            int ty = y + 14;
            ui_.drawSmall(screen_, x + 58, ty, up, text);
            for (const std::string& l : ui_.wrapSmall(message, w - 70)) {
                ty += lh;
                if (ty > y + h - 12 - lh)
                    break;
                ui_.drawSmall(screen_, x + 58, ty, l, text);
            }
            const std::string counter = std::to_string(log_.size()) + "/" + std::to_string(log_.size());
            ui_.drawSmall(screen_, x + w - 12 - ui_.smallWidth(counter), y + h - 11 - lh, counter, text);
        }
    }

    // COMMAND: the objectives, as the recordings' panel lists them.
    if (run_) {
        const MissionScript* script = findMissionScript(level.shortName, side);
        std::vector<std::pair<std::string, Color>> lines;
        const int w = 260;
        for (const MissionScript::Objective& o : script->objectives) {
            const MissionRun::State st = run_->objectiveState(o.id);
            if (st == MissionRun::State::Hidden)
                continue;
            const std::string* t = table_.find(o.id);
            const std::string s = "MISSION OBJECTIVE: " + (t ? *t : std::to_string(o.id)) +
                                  (st == MissionRun::State::Complete ? " - COMPLETE" : " - INCOMPLETE");
            for (const std::string& l : ui_.wrapSmall(s, w - 24))
                lines.push_back({l, st == MissionRun::State::Complete ? bright : text});
        }
        const int h = 30 + int(lines.size()) * lh;
        ui_.drawPanel(screen_, 0, 0, w, h, "COMMAND");
        for (size_t i = 0; i < lines.size(); ++i)
            ui_.drawSmall(screen_, 12, 15 + int(i) * lh, lines[i].first, lines[i].second);
    }
}

void MissionViewer::stepScript() {
    const Level& level = missions_[size_t(current_)];
    const char side = missionSides(level.shortName)[size_t(side_)];
    if (!run_) {
        const MissionScript* script = findMissionScript(level.shortName, side);
        if (!script) {
            soundStatus_ = "no script for this mission yet";
            updateTitle();
            return;
        }
        stopBriefing();
        run_ = std::make_unique<MissionRun>(*script, findMissionScript("generic", 'F'));
        walk_ = run_->walkthrough();
        walkPos_ = 0;
        log_.clear();
        walkNote_ = "script loaded";
        briefing_ = true;
        redraw();
        updateTitle();
        return;
    }
    if (run_->finished())
        return;
    std::vector<MissionRun::Output> out;
    ScriptEvent fired{"timer", {}};
    if (walkPos_ < walk_.size()) {
        const ScriptEvent& e = walk_[walkPos_++];
        walkNote_ = "event " + std::to_string(walkPos_) + "/" + std::to_string(walk_.size()) + ": " + e.kind;
        for (const std::string& a : e.args)
            walkNote_ += " " + a;
        out = run_->fire(e);
        fired = e;
        // The colony grows as the script builds (on the missions page only:
        // the game's own building is the player's).
        if (e.kind == "built" && !e.args.empty() && colony_) {
            if (colony_->add(e.args[0]))
                showColony();
            else
                walkNote_ += " (not placed)";
        }
    } else if (run_->nextTimer() >= 0) {
        walkNote_ = "time passes (" + std::to_string(int(run_->nextTimer())) + " s)";
        out = run_->advance(run_->nextTimer());
    } else {
        walkNote_ = "nothing more scripted";
    }
    deliver(fired, out);
    redraw();
    updateTitle();
}

// What the script said: its messages to the log, the game's spoken
// announcement for it (Media/Misc), if any, and the outcome in the note.
void MissionViewer::deliver(const ScriptEvent& fired, const std::vector<MissionRun::Output>& out) {
    const char side = missionSides(missions_[size_t(current_)].shortName)[size_t(side_)];
    if (playing_ && !out.empty())
        walkNote_ = fired.kind; // in play, the latest that said anything
    if (const std::string voice = announcementFor(fired, out, side); !voice.empty() && mixer_) {
        stopClip();
        if (auto sound = loadSound(voice))
            clipHandle_ = mixer_->play(std::move(sound), audio::SoundType::Speech);
        else
            soundStatus_.clear(); // the game folder may not have Misc
        walkNote_ += " [" + voice + "]";
    }
    for (const MissionRun::Output& o : out) {
        if (o.kind == MissionRun::Output::Message)
            log_.push_back({o.sender, o.text});
        else if (o.kind == MissionRun::Output::Won)
            walkNote_ += "; mission won";
        else if (o.kind == MissionRun::Output::Lost)
            walkNote_ += "; mission lost";
    }
    if (!out.empty())
        redraw();
}

void MissionViewer::stopScript() {
    if (!run_)
        return;
    run_.reset();
    walk_.clear();
    log_.clear();
    startColony();
    redraw();
    updateTitle();
}

void MissionViewer::startBriefing() {
    if (script_.empty())
        return;
    savedColouring_ = map_.colouring();
    running_ = true;
    briefing_ = true;
    startStep(0);
}

void MissionViewer::stopBriefing() {
    if (!running_)
        return;
    running_ = false;
    stopClip();
    map_.setColouring(savedColouring_);
    map_.resetCamera();
    redraw();
    updateTitle();
}

void MissionViewer::startStep(int step) {
    stopClip();
    if (step >= int(script_.size())) {
        stopBriefing();
        return;
    }
    step_ = step;
    const BriefingStep& s = script_[size_t(step)];
    stepOnMap_ = hasMap_ && (s.view == BriefingView::Ground || s.view == BriefingView::Relief);
    if (stepOnMap_) {
        map_.setColouring(s.view == BriefingView::Relief ? 1 : 0);
        map_.lookAt(s.siteA, s.siteB, s.view == BriefingView::Ground);
        lastTurnMs_ = nowMs_;
    }
    // Its clip, if the game folder has it; else the step is shown a while.
    stepEndMs_ = nowMs_ + 5000;
    const auto it = std::find(clips_.begin(), clips_.end(), s.clip);
    if (!s.clip.empty() && it != clips_.end()) {
        playClip(int(it - clips_.begin()));
        if (clipHandle_ != audio::kNoSound)
            stepEndMs_ = 0;
    }
    redraw();
    updateTitle();
}

bool MissionViewer::mapInView() const { return hasMap_ && (!running_ || stepOnMap_); }

namespace {

// A texture from whichever archive has it; false if none does or it doesn't decode.
bool loadTexture(const Archives& archives, const std::string& name, Texture& out) {
    for (const auto& a : archives)
        if (const CatEntry* e = a->find(name)) {
            std::vector<uint8_t> data;
            return !a->read(*e, data) && !decodeTexture(data, out);
        }
    return false;
}

} // namespace

void MissionViewer::loadBriefingArt() {
    // The emblem screens are six tiles, three across and two down:
    // 256 + 256 + 128 by 256 + 224, the 640 x 480 screen.
    const std::pair<char, const char*> sets[] = {{'F', "lbp/gui/ufp00"}, {'K', "lbp/gui/kliemp0"}, {'R', "lbp/gui/romemp0"}};
    for (const auto& [side, prefix] : sets) {
        Texture screen;
        screen.width = 640;
        screen.height = 480;
        screen.rgba.assign(size_t(640) * 480 * 4, 0);
        bool ok = true;
        for (int t = 0; t < 6 && ok; ++t) {
            Texture tile;
            ok = loadTexture(archives_, prefix + std::to_string(t + 1) + ".lbp", tile);
            const int x0 = (t % 3) * 256, y0 = (t / 3) * 256;
            for (int y = 0; ok && y < tile.height && y0 + y < 480; ++y)
                for (int x = 0; x < tile.width && x0 + x < 640; ++x)
                    std::copy_n(&tile.rgba[(size_t(y) * size_t(tile.width) + size_t(x)) * 4], 4,
                                &screen.rgba[(size_t(y0 + y) * 640 + size_t(x0 + x)) * 4]);
        }
        if (ok)
            emblems_[side] = std::move(screen);
    }
    // The galaxy: a slab of star field round three flat layers (blue arm,
    // red ribbon, stars), numbering tabras01-04 in name order.
    for (const auto& a : archives_)
        if (const CatEntry* e = a->find("geo/solsys/tabras.geo")) {
            std::vector<uint8_t> data;
            if (a->read(*e, data) || parseGeo(data, galaxy_))
                galaxy_ = GeoModel{};
            break;
        }
    for (int t = 1; t <= 4 && !galaxy_.faces.empty(); ++t) {
        Texture tex;
        loadTexture(archives_, "lbp/txr/solsys/tabras0" + std::to_string(t) + ".mp0", tex);
        galaxyTextures_.push_back(std::move(tex));
    }
    // The star systems' models, by name (the galaxy isn't one).
    for (const auto& a : archives_)
        for (const CatEntry& e : a->entries()) {
            std::string folder, stem, ext;
            splitName(e.name, folder, stem, ext);
            if (folder == "solsys" && ext == "geo" && stem != "tabras" &&
                std::find(systemNames_.begin(), systemNames_.end(), stem) == systemNames_.end())
                systemNames_.push_back(stem);
        }
}

// The star system the mission's table names, with its pictures.
void MissionViewer::loadStarSystem() {
    system_ = GeoModel{};
    systemLayout_ = {};
    systemTextures_.clear();
    systemName_ = starSystemName(table_, systemNames_);
    if (systemName_.empty())
        return;
    for (const auto& a : archives_)
        if (const CatEntry* e = a->find("geo/solsys/" + systemName_ + ".geo")) {
            std::vector<uint8_t> data;
            if (a->read(*e, data) || parseGeo(data, system_))
                system_ = GeoModel{};
            break;
        }
    if (system_.faces.empty())
        return;
    systemLayout_ = readStarSystem(system_);
    for (const std::string& stem : starSystemTextures(systemLayout_, hasMap_ ? planetPicture(map_.mapArchive()) : "",
                                                          systemName_)) {
        Texture tex;
        if (!stem.empty())
            loadTexture(archives_, "lbp/txr/solsys/" + stem + ".mp0", tex);
        systemTextures_.push_back(std::move(tex));
    }
}

// The system from near the mission's planet, turning about it: the
// starfield adding its light behind (its alpha layer is its brightness,
// dimmed as in the recordings), the star and planets as stacks of round
// pictures, each turned to face the camera (so its slices stand one behind
// another, a ball from any side; seen edge-on they'd be a pile of discs).
bool MissionViewer::drawStarSystem() {
    if (system_.faces.empty())
        return false;
    for (int y = 0; y < screen_.h; ++y) {
        uint8_t* row = screen_.row(y);
        for (int x = 0; x < screen_.w; ++x, row += 4)
            row[0] = row[1] = row[2] = 0, row[3] = 255;
    }
    const int body = missionBody(systemLayout_);
    GeoModel::Vec3 target{0, 0, 0};
    if (body >= 0)
        target = systemLayout_.bodies[size_t(body)].centre;
    const float yaw = float(nowMs_ % 120000) / 120000.0f * 6.2832f;
    const OrbitCamera cam({target.x, target.y, target.z}, yaw, 0.25f, 2200.0f, screen_.w, screen_.h);
    raster_.begin(screen_);
    for (int pass = 0; pass < 2; ++pass) // the starfield first
        for (size_t f = 0; f < system_.faces.size(); ++f) {
            const GeoModel::Face& face = system_.faces[f];
            const int slot = systemLayout_.faceSlot[f];
            const bool backdrop = std::find(systemLayout_.backdropSlots.begin(), systemLayout_.backdropSlots.end(),
                                            slot) != systemLayout_.backdropSlots.end();
            if (slot < 0 || size_t(slot) >= systemTextures_.size() || systemTextures_[size_t(slot)].rgba.empty() ||
                backdrop != (pass == 0) || face.corners.size() < 3 || face.part >= system_.parts.size())
                continue;
            const Texture& tex = systemTextures_[size_t(slot)];
            const auto& verts = system_.parts[face.part].vertices;
            const int body = systemLayout_.faceBody[f];
            std::vector<ViewVertex> poly;
            for (const GeoModel::Corner& c : face.corners) {
                if (c.vertex >= verts.size())
                    break;
                const GeoModel::Vec3& p = verts[c.vertex];
                Vec at{p.x, p.y, p.z};
                if (body >= 0) {
                    const GeoModel::Vec3& o = systemLayout_.bodies[size_t(body)].centre;
                    const Vec d{p.x - o.x, p.y - o.y, p.z - o.z};
                    at = Vec{o.x, o.y, o.z} + cam.right * d.x + cam.up * d.y - cam.forward * d.z;
                }
                poly.push_back(cam.toView(at, c.u, c.v));
            }
            if (poly.size() != face.corners.size())
                continue;
            poly = clipNear(poly, cam.nearZ);
            if (poly.size() < 3)
                continue;
            std::vector<ScreenVertex> s;
            for (const ViewVertex& v : poly)
                s.push_back(cam.project(v));
            auto shade = [&](float u, float v, uint8_t* out) {
                const int tx = std::clamp(int(u * float(tex.width)), 0, tex.width - 1);
                const int ty = std::clamp(int(v * float(tex.height)), 0, tex.height - 1);
                const size_t i = size_t(ty) * size_t(tex.width) + size_t(tx);
                const int a = tex.alpha.empty() ? 255 : tex.alpha[i];
                if (backdrop) {
                    for (int k = 0; k < 3; ++k)
                        out[k] = uint8_t(std::min(255, out[k] + tex.rgba[i * 4 + size_t(k)] * a / 512));
                    return false;
                }
                if (a < 128)
                    return false; // outside the disc
                for (int k = 0; k < 3; ++k)
                    out[k] = tex.rgba[i * 4 + size_t(k)];
                return true;
            };
            for (size_t i = 1; i + 1 < s.size(); ++i)
                raster_.drawTriangle(s[0], s[i], s[i + 1], shade);
        }
    return true;
}

bool MissionViewer::drawEmblem(char side) {
    const auto it = emblems_.find(side);
    if (it == emblems_.end())
        return false;
    const Texture& e = it->second;
    for (int y = 0; y < screen_.h; ++y) {
        uint8_t* row = screen_.row(y);
        const int sy = y * e.height / screen_.h;
        for (int x = 0; x < screen_.w; ++x, row += 4) {
            const uint8_t* p = &e.rgba[(size_t(sy) * size_t(e.width) + size_t(x * e.width / screen_.w)) * 4];
            row[0] = p[0], row[1] = p[1], row[2] = p[2], row[3] = 255;
        }
    }
    return true;
}

bool MissionViewer::drawGalaxy() {
    if (galaxy_.faces.empty())
        return false;
    for (int y = 0; y < screen_.h; ++y) {
        uint8_t* row = screen_.row(y);
        for (int x = 0; x < screen_.w; ++x, row += 4)
            row[0] = row[1] = row[2] = 0, row[3] = 255;
    }
    // Seen from above at an angle, turning; each layer adds its light (its
    // alpha layer is its brightness), so there's no depth to sort.
    const float yaw = float(nowMs_ % 120000) / 120000.0f * 6.2832f;
    const OrbitCamera cam({0, 0, 0}, yaw, 0.45f, 16000.0f, screen_.w, screen_.h);
    raster_.begin(screen_);
    for (const GeoModel::Face& face : galaxy_.faces) {
        if (face.corners.size() < 3 || face.part >= galaxy_.parts.size())
            continue;
        const uint32_t ti = face.corners.back().extra[4];
        if (ti >= galaxyTextures_.size() || galaxyTextures_[ti].rgba.empty())
            continue;
        const Texture& tex = galaxyTextures_[ti];
        const auto& verts = galaxy_.parts[face.part].vertices;
        std::vector<ViewVertex> poly;
        for (const GeoModel::Corner& c : face.corners) {
            if (c.vertex >= verts.size())
                break;
            const GeoModel::Vec3& p = verts[c.vertex];
            poly.push_back(cam.toView({p.x, p.y, p.z}, c.u, c.v));
        }
        if (poly.size() != face.corners.size())
            continue;
        poly = clipNear(poly, cam.nearZ);
        if (poly.size() < 3)
            continue;
        std::vector<ScreenVertex> s;
        for (const ViewVertex& v : poly)
            s.push_back(cam.project(v));
        auto shade = [&](float u, float v, uint8_t* out) {
            const int tx = std::clamp(int(u * float(tex.width)), 0, tex.width - 1);
            const int ty = std::clamp(int(v * float(tex.height)), 0, tex.height - 1);
            const size_t i = size_t(ty) * size_t(tex.width) + size_t(tx);
            const int a = tex.alpha.empty() ? 255 : tex.alpha[i];
            for (int k = 0; k < 3; ++k)
                out[k] = uint8_t(std::min(255, out[k] + tex.rgba[i * 4 + size_t(k)] * a / 255));
            return false;
        };
        for (size_t i = 1; i + 1 < s.size(); ++i)
            raster_.drawTriangle(s[0], s[i], s[i + 1], shade);
    }
    return true;
}

bool MissionViewer::drawModel(const std::string& name) {
    if (name.empty())
        return false;
    auto [it, added] = models_.try_emplace(name);
    BriefingModel& m = it->second;
    if (added)
        m.ok = loadTexturedModel(archives_, textureFiles_, name, m.model);
    if (!m.ok)
        return false;

    // The grid, as behind the recordings' model views: teal lines for the
    // Federation, dark red for the Klingons, green crosses for the Romulans.
    const Level& level = missions_[size_t(current_)];
    const char side = missionSides(level.shortName)[size_t(side_)];
    const uint8_t grid[3] = {uint8_t(side == 'F' ? 20 : side == 'R' ? 20 : 70),
                             uint8_t(side == 'F' ? 45 : side == 'R' ? 80 : 14),
                             uint8_t(side == 'F' ? 60 : side == 'R' ? 25 : 14)};
    const int w = screen_.w, h = screen_.h;
    for (int y = 0; y < h; ++y) {
        uint8_t* row = screen_.row(y);
        for (int x = 0; x < w; ++x, row += 4) {
            const bool line = side == 'R' ? (y % 24 == 0 && (x + 2) % 24 < 5) || (x % 24 == 0 && (y + 2) % 24 < 5)
                                          : (y % 24 == 0 && x % 4 == 0) || (x % 24 == 0 && y % 4 == 0);
            row[0] = line ? grid[0] : 0, row[1] = line ? grid[1] : 0, row[2] = line ? grid[2] : 0, row[3] = 255;
        }
    }
    // Turning once a minute, in the side's style.
    return drawModelView(screen_, raster_, m.model, float(nowMs_ % 60000) / 60000.0f * 6.2832f, 0.35f,
                         briefingModelStyle(side));
}

// The briefing's views without a map: the game's galaxy, star system,
// models and emblem where the archives have them; else Reluva's own
// stand-ins.
void MissionViewer::drawBackdrop(BriefingView view) {
    const char side = missionSides(missions_[size_t(current_)].shortName)[size_t(side_)];
    if ((view == BriefingView::Galaxy && drawGalaxy()) || (view == BriefingView::Emblem && drawEmblem(side)) ||
        (view == BriefingView::StarSystem && drawStarSystem()) ||
        (view == BriefingView::Model && drawModel(script_[size_t(step_)].model)))
        return;
    const int w = screen_.w, h = screen_.h;
    for (int y = 0; y < h; ++y) {
        uint8_t* row = screen_.row(y);
        for (int x = 0; x < w; ++x, row += 4)
            row[0] = row[1] = row[2] = 0, row[3] = 255;
    }
    auto plot = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b) {
        if (x >= 0 && y >= 0 && x < w && y < h) {
            uint8_t* p = screen_.row(y) + x * 4;
            p[0] = r, p[1] = g, p[2] = b;
        }
    };
    uint32_t seed = 12345;
    auto rnd = [&] { return (seed = seed * 1664525u + 1013904223u) >> 8; };
    switch (view) {
    case BriefingView::Galaxy:
        for (int i = 0; i < 900; ++i) {
            // A flattened disc of stars with a red and a blue arm.
            const float t = float(rnd() % 1000) / 1000.0f * 6.283f, rr = float(rnd() % 1000) / 1000.0f;
            const int x = w / 2 + int(std::cos(t) * rr * float(w) * 0.32f);
            const int y = h / 2 + int(std::sin(t) * rr * float(h) * 0.16f);
            const uint8_t v = uint8_t(120 + rnd() % 136);
            plot(x, y, v, v, v);
        }
        for (int i = 0; i < 300; ++i) {
            const int x = int(rnd() % unsigned(w / 3)), d = int(rnd() % 9) - 4;
            plot(w / 3 + x, h / 2 - 30 + x / 4 + d, 200, 40, 40);
            plot(w / 3 + x, h / 2 + 20 - x / 5 + d, 60, 80, 220);
        }
        break;
    case BriefingView::StarSystem:
        for (int i = 0; i < 400; ++i)
            plot(int(rnd() % unsigned(w)), int(rnd() % unsigned(h)), 90, 90, 110);
        for (int i = 0; i < 7; ++i) {
            const int cx = int(rnd() % unsigned(w)), cy = int(rnd() % unsigned(h)), r = 2 + int(rnd() % 5);
            const uint8_t cr = uint8_t(rnd() % 256), cg = uint8_t(rnd() % 256), cb = uint8_t(rnd() % 256);
            for (int y = -r; y <= r; ++y)
                for (int x = -r; x <= r; ++x)
                    if (x * x + y * y <= r * r)
                        plot(cx + x, cy + y, cr, cg, cb);
        }
        break;
    case BriefingView::Model:
    case BriefingView::Ground:
    case BriefingView::Relief:
        // A dim red grid, as the model views have (and the map views when
        // there's no map).
        for (int y = 0; y < h; y += 24)
            for (int x = 0; x < w; x += 4)
                plot(x, y, 70, 14, 14);
        for (int x = 0; x < w; x += 24)
            for (int y = 0; y < h; y += 4)
                plot(x, y, 70, 14, 14);
        drawText(kPanelMargin, kPanelMargin, view == BriefingView::Model ? "(a model turning)" : mapNote_, kNoteColor);
        break;
    case BriefingView::Emblem: {
        const char* name = side == 'K' ? "KLINGON EMPIRE" : side == 'R' ? "ROMULAN STAR EMPIRE" : "UNITED FEDERATION OF PLANETS";
        const Color c = side == 'K' ? Color{200, 60, 40} : side == 'R' ? Color{60, 170, 90} : Color{90, 140, 230};
        const int scale = 3, tw = int(font::length(name)) * font::kAdvance * scale;
        font::drawText(screen_, (w - tw) / 2, h / 3, name, c, scale);
        break;
    }
    }
}

bool MissionViewer::clipPlaying() const { return mixer_ && mixer_->isPlaying(clipHandle_); }

void MissionViewer::playClip(int index) {
    stopClip();
    clip_ = index;
    soundStatus_.clear();
    if (clips_.empty())
        return;
    if (!mixer_) {
        soundStatus_ = "no audio output";
        return;
    }
    if (auto sound = loadSound(clips_[size_t(clip_)]))
        clipHandle_ = mixer_->play(std::move(sound), audio::SoundType::Speech);
}

void MissionViewer::stopClip() {
    if (mixer_)
        mixer_->stop(clipHandle_);
    clipHandle_ = audio::kNoSound;
}

void MissionViewer::startMusic() {
    const std::string theme = hasMap_ ? planetTheme(map_.mapArchive()) : std::string();
    if (mixer_ && musicHandle_ != audio::kNoSound && mixer_->isPlaying(musicHandle_) && theme == musicName_)
        return; // already playing it
    stopMusic();
    musicName_.clear();
    if (theme.empty()) {
        soundStatus_ = "no music known for this map";
        return;
    }
    if (!mixer_) {
        soundStatus_ = "no audio output";
        return;
    }
    if (auto sound = loadSound(theme)) {
        musicName_ = theme;
        musicHandle_ = mixer_->play(std::move(sound), audio::SoundType::Music, kMusicVolume, 0, true);
    }
}

void MissionViewer::stopMusic() {
    if (mixer_)
        mixer_->stop(musicHandle_);
    musicHandle_ = audio::kNoSound;
}

} // namespace reluva::newworlds
