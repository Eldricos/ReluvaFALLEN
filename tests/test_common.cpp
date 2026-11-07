// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Small self-contained tests for common/. No framework, to keep deps at zero.

#include "common/audio/decode.h"
#include "common/audio/mixer.h"
#include "common/audio/mp3.h"
#include "common/audio/tracker.h"
#include "common/audio/wav.h"
#include "data/tone440_mp3.h"
#include "common/archive.h"
#include "common/config.h"
#include "common/detection.h"
#include "common/font.h"
#include "common/fs.h"
#include "common/gfx3d.h"
#include "common/image.h"
#include "common/md5.h"
#include "common/text.h"
#include "common/zip.h"
#include "zip_writer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <fstream>
#include <string>

using namespace reluva;

static int failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, \
                         __LINE__, #cond);                              \
            ++failures;                                                 \
        }                                                               \
    } while (0)

static std::string md5Of(const std::string& s) {
    Md5 m;
    m.update(s.data(), s.size());
    return m.hexDigest();
}

static void testMd5() {
    CHECK(md5Of("") == "d41d8cd98f00b204e9800998ecf8427e");
    CHECK(md5Of("abc") == "900150983cd24fb0d6963f7d28e17f72");
    CHECK(md5Of("The quick brown fox jumps over the lazy dog") == "9e107d9d372bb6826bd81d3542a419d6");
    // Crosses the 56-byte padding boundary.
    CHECK(md5Of(std::string(64, 'a')) == "014842d480b571495a4a0363793f7367");
}

static void testFsAndDetection(const std::filesystem::path& tmp) {
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp / "Data");
    {
        std::ofstream(tmp / "Data" / "GAME.DAT", std::ios::binary) << "abc";
    }

    auto found = fs::findFile(tmp, "data/game.dat");
    CHECK(found.has_value());
    CHECK(!fs::findFile(tmp, "data/missing.dat"));
    CHECK(fs::md5File(*found, kDetectionHashBytes) == "900150983cd24fb0d6963f7d28e17f72");

    std::vector<GameVariant> table = {
        {"good", "Good", "en", "windows", {{"DATA/game.dat", "900150983cd24fb0d6963f7d28e17f72", 3}}},
        {"badsize", "Bad size", "en", "windows", {{"data/game.dat", "", 4}}},
        {"badmd5", "Bad md5", "en", "windows", {{"data/game.dat", "00000000000000000000000000000000", -1}}},
    };
    auto results = detectFromTable("eng", table, tmp);
    CHECK(results.size() == 1);
    CHECK(!results.empty() && results[0].gameId == "good");

    std::filesystem::remove_all(tmp);
}

static void testConfig(const std::filesystem::path& tmp) {
    std::filesystem::create_directories(tmp);
    auto path = tmp / "test.ini";
    {
        Config c;
        c.load(path);
        c.set(Config::kGlobalSection, "last_target", "foo");
        c.set("foo", "path", "/games/foo bar");
        CHECK(!c.save());
    }
    Config c;
    c.load(path);
    CHECK(c.get("foo", "path") == "/games/foo bar");
    CHECK(c.targets().size() == 1);
    CHECK(c.get("foo", "missing", "dflt") == "dflt");
    std::filesystem::remove_all(tmp);
}

static void testUtf16() {
    const uint16_t hello[] = {'H', 'i', ' ', 0x00e9, 0x20ac, 0xd83d, 0xde80}; // e-acute, euro, rocket
    CHECK(text::utf16leToUtf8(hello, 7) == "Hi \xc3\xa9\xe2\x82\xac\xf0\x9f\x9a\x80");
    const uint16_t lone[] = {0xd800, 'x'};
    CHECK(text::utf16leToUtf8(lone, 2) == "\xef\xbf\xbdx");
    CHECK(text::utf16leToUtf8(nullptr, 0).empty());
}

static void testFont() {
    using namespace reluva;
    CHECK(font::length("Caf\xc3\xa9 \xe2\x80\x99") == 6); // é and ’ are one character each
    Surface s;
    s.create(20, 12, PixelFormat::RGBA32);
    const int end = font::drawText(s, 1, 1, "I|", {255, 0, 0});
    CHECK(end == 1 + 2 * font::kAdvance);
    // 'I' has its middle column set from row 1 to 5 and a bar on row 0.
    auto red = [&](int x, int y) { return s.row(y)[x * 4] == 255 && s.row(y)[x * 4 + 1] == 0; };
    CHECK(red(3, 1) && red(2, 1) && red(4, 1) && !red(1, 2) && red(3, 4) && !red(0, 0));
    CHECK(red(9, 7) && !red(9, 8)); // '|' runs rows 0-6
    font::drawText(s, 15, 10, "W", {255, 0, 0}, 2); // clipped, not out of bounds
}

static void testWav() {
    using namespace reluva::audio;
    // One 8-byte IMA ADPCM block: header (sample 100, index 0), then nibbles
    // 4, 0, 0, ... (low nibble first); the fact chunk keeps 5 frames.
    std::vector<uint8_t> b;
    auto put32 = [](std::vector<uint8_t>& v, uint32_t x) {
        for (int i = 0; i < 4; ++i)
            v.push_back(uint8_t(x >> (8 * i)));
    };
    auto put16 = [&](uint16_t v) { b.push_back(uint8_t(v)); b.push_back(uint8_t(v >> 8)); };
    auto tag = [&](const char* t) { b.insert(b.end(), t, t + 4); };
    tag("RIFF"); put32(b, 0); tag("WAVE");
    tag("fmt "); put32(b, 20);
    put16(0x11); put16(1); put32(b, 22050); put32(b, 22050); put16(8); put16(4); put16(2); put16(9);
    tag("fact"); put32(b, 4); put32(b, 5);
    tag("data"); put32(b, 8);
    for (uint8_t v : {100, 0, 0, 0, 0x04, 0, 0, 0})
        b.push_back(v);
    Sound s;
    CHECK(!decodeWav(b, s));
    CHECK(s.rate == 22050 && s.channels == 1 && s.format == 0x11);
    CHECK((s.samples == std::vector<int16_t>{100, 107, 108, 109, 109}));
    // Back through 16-bit PCM unchanged.
    Sound pcm;
    CHECK(!decodeWav(encodePcmWav(s), pcm));
    CHECK(pcm.format == 1 && pcm.rate == 22050 && pcm.samples == s.samples);
    b.resize(20);
    CHECK(bool(decodeWav(b, s))); // cut inside the fmt chunk
}

static void testMp3() {
    using namespace reluva::audio;
    const std::vector<uint8_t> tone(std::begin(kTone440Mp3), std::end(kTone440Mp3));
    Sound s;
    CHECK(!decodeMp3(tone, s));
    CHECK(s.format == kFormatMp3 && s.rate == 22050 && s.channels == 1);
    // About 0.2 s (whole frames of 576 samples, plus the encoder's delay).
    CHECK(s.frames() >= 4410 && s.frames() <= 4410 + 3 * 576);
    // 440 Hz: about 2 x 440 x 0.1 = 88 zero crossings in the middle 0.1 s.
    int crossings = 0;
    for (size_t i = 1102; i < 3307; ++i)
        crossings += (s.samples[i - 1] < 0) != (s.samples[i] < 0);
    CHECK(crossings >= 84 && crossings <= 92);
    // The same after an ID3v2 tag (header, 5 bytes of padding), and through decodeSound.
    std::vector<uint8_t> tagged = {'I', 'D', '3', 3, 0, 0, 0, 0, 0, 5, 0, 0, 0, 0, 0};
    tagged.insert(tagged.end(), tone.begin(), tone.end());
    Sound t;
    CHECK(!decodeSound(tagged, t) && t.samples == s.samples);
    CHECK(bool(decodeMp3(std::vector<uint8_t>(100, 0), t))); // no frames
}

static void testMixer() {
    using namespace reluva::audio;
    auto make = [](int rate, int channels, std::vector<int16_t> samples) {
        auto s = std::make_shared<Sound>();
        s->rate = rate, s->channels = channels, s->samples = std::move(samples);
        return std::shared_ptr<const Sound>(s);
    };
    std::vector<int16_t> out(40);
    Mixer m(22050);
    // A sound at the output rate plays sample for sample, then stops and frees its channel.
    const SoundHandle a = m.play(make(22050, 1, std::vector<int16_t>(10, 1000)), SoundType::Effects);
    CHECK(a != kNoSound && m.isPlaying(a));
    m.mix(out.data(), 12);
    CHECK(out[0] == 1000 && out[1] == 1000 && out[18] == 1000 && out[19] == 1000 && out[20] == 0 && out[23] == 0);
    CHECK(!m.isPlaying(a));
    m.mix(out.data(), 4);
    CHECK(out[0] == 0 && out[7] == 0); // silence with nothing playing
    // Two sounds add up, and clamp at 16 bits.
    m.play(make(22050, 1, {30000, 1000}), SoundType::Effects);
    m.play(make(22050, 1, {30000, -3000}), SoundType::Music);
    m.mix(out.data(), 2);
    CHECK(out[0] == 32767 && out[2] == -2000 && out[3] == -2000);
    // Volumes: the sound's and its type's multiply.
    m.setTypeVolume(SoundType::Music, kMaxVolume / 2);
    m.play(make(22050, 1, {1000}), SoundType::Music, kMaxVolume / 2);
    m.mix(out.data(), 1);
    CHECK(out[0] == 250 && out[1] == 250 && m.typeVolume(SoundType::Music) == 128);
    m.setTypeVolume(SoundType::Music, kMaxVolume);
    // Pan: full right silences the left.
    m.play(make(22050, 1, {1000}), SoundType::Speech, kMaxVolume, kMaxPan);
    m.mix(out.data(), 1);
    CHECK(out[0] == 0 && out[1] == 1000);
    // Resampling: an 11025 Hz ramp at 22050 Hz gets a frame in between each.
    const SoundHandle r = m.play(make(11025, 1, {0, 100, 200, 300}), SoundType::Effects);
    m.mix(out.data(), 4);
    CHECK(out[0] == 0 && out[2] == 50 && out[4] == 100 && out[6] == 150 && m.position(r) == 2.0);
    m.stopAll();
    // Stereo keeps its channels; a loop repeats until stopped.
    const SoundHandle l = m.play(make(22050, 2, {10, -10, 20, -20}), SoundType::Music, kMaxVolume, 0, true);
    m.mix(out.data(), 5);
    CHECK(out[0] == 10 && out[1] == -10 && out[2] == 20 && out[3] == -20 && out[4] == 10 && out[8] == 10);
    CHECK(m.isPlaying(l) && m.isTypePlaying(SoundType::Music) && !m.isTypePlaying(SoundType::Speech));
    m.stopType(SoundType::Music);
    m.mix(out.data(), 2);
    CHECK(!m.isPlaying(l) && out[0] == 0);
    // Pitch: 2 plays every other frame; seek starts it further in.
    const SoundHandle p = m.play(make(22050, 1, {0, 100, 200, 300, 400, 500}), SoundType::Effects);
    m.setPitch(p, 2);
    m.seek(p, 1);
    m.mix(out.data(), 2);
    CHECK(out[0] == 100 && out[2] == 300 && m.position(p) == 5.0);
    m.stopAll();
    // Muffled: a tone at the top of the band (alternating samples) all but
    // goes; a steady level passes once the filter settles.
    std::vector<int16_t> high(2000), steady(2000, 1000);
    for (size_t k = 0; k < high.size(); ++k)
        high[k] = k % 2 ? -1000 : 1000;
    std::vector<int16_t> big(4000);
    const SoundHandle h = m.play(make(22050, 1, high), SoundType::Effects);
    m.setMuffle(h, 1);
    m.mix(big.data(), 2000);
    CHECK(std::abs(big[3000]) < 200 && std::abs(big[3002]) < 200); // a seventh, at 22 kHz
    const SoundHandle st = m.play(make(22050, 1, steady), SoundType::Effects);
    m.setMuffle(st, 1);
    m.mix(big.data(), 2000);
    CHECK(std::abs(big[3000] - 1000) < 5);
    m.stopAll();
    // The Butterworth filters: a low-pass at 1 kHz takes the top of the
    // band away, a high-pass at 600 Hz a steady level; the other way
    // round each passes it.
    const SoundHandle lp = m.play(make(22050, 1, high), SoundType::Effects);
    m.setFilter(lp, Mixer::Filter::LowPass, 1000);
    m.mix(big.data(), 2000);
    CHECK(std::abs(big[3000]) < 5 && std::abs(big[3002]) < 5);
    const SoundHandle hp = m.play(make(22050, 1, steady), SoundType::Effects);
    m.setFilter(hp, Mixer::Filter::HighPass, 600);
    m.mix(big.data(), 2000);
    CHECK(std::abs(big[3000]) < 5);
    const SoundHandle lp2 = m.play(make(22050, 1, steady), SoundType::Effects);
    m.setFilter(lp2, Mixer::Filter::LowPass, 1000);
    m.mix(big.data(), 2000);
    CHECK(std::abs(big[3000] - 1000) < 5);
    m.stopAll();
    // The reverb: a click, then its echoes from the first comb (29.7 ms,
    // 655 frames) on; dry before, and the dry part weighed by 1 - mix.
    std::vector<int16_t> click(3000, 0);
    click[0] = 10000;
    const SoundHandle rv = m.play(make(22050, 1, click), SoundType::Effects);
    m.setReverb(rv, {0.5f, 1.5f, 5000, 0});
    std::vector<int16_t> tail(6000);
    m.mix(tail.data(), 3000);
    CHECK(tail[0] == 5000 && tail[2 * 300] == 0);
    int echoes = 0;
    for (size_t k = 2 * 640; k < tail.size(); k += 2)
        echoes += tail[k] != 0;
    CHECK(echoes > 100 && tail[2 * 655] != tail[2 * 655 + 1]); // left and right differ
    m.stopAll();
    // Its tail: once a short sound has ended, the reverb goes on through
    // silence for the tail's time, then the sound is done.
    std::vector<int16_t> blip(10, 0);
    blip[0] = 10000;
    const SoundHandle tb = m.play(make(22050, 1, blip), SoundType::Effects);
    m.setReverb(tb, {0.5f, 1.5f, 5000, 0, 0.1f});
    m.mix(tail.data(), 1000);
    CHECK(m.isPlaying(tb) && tail[2 * 655] != 0);
    m.mix(tail.data(), 2000);
    CHECK(!m.isPlaying(tb));
    // Nothing to play.
    CHECK(m.play(nullptr, SoundType::Effects) == kNoSound && m.play(make(22050, 1, {}), SoundType::Effects) == kNoSound);
}

static void testZip(const std::filesystem::path& tmp) {
    using test::toBytes;
    CHECK(crc32(reinterpret_cast<const uint8_t*>("123456789"), 9) == 0xcbf43926);

    // "textures/borg/borgfloor1\n" 12 times, raw-deflated by zlib.
    std::string text;
    for (int i = 0; i < 12; ++i)
        text += "textures/borg/borgfloor1\n";
    const std::vector<uint8_t> deflated = {0x2b, 0x49, 0xad, 0x28, 0x29, 0x2d, 0x4a, 0x2d, 0xd6, 0x4f,
                                           0xca, 0x2f, 0x4a, 0x07, 0x13, 0x69, 0x39, 0xf9, 0xf9, 0x45,
                                           0x86, 0x5c, 0x25, 0xa3, 0x12, 0xe8, 0x12, 0x00};
    std::filesystem::remove_all(tmp);
    const auto path = tmp / "pak0.pk3";
    test::writeFile(path, test::buildZip({{"Scripts/", {}, {}},
                                          {"Scripts/Borg.shader", toBytes("hello"), {}},
                                          {"maps/list.txt", toBytes(text), deflated},
                                          {"empty.cfg", {}, {}}},
                                         "a comment"));
    ZipArchive zip;
    CHECK(!zip.open(path));
    CHECK(zip.entries().size() == 3); // the folder entry is left out
    CHECK(zip.has("scripts\\BORG.shader") && !zip.has("scripts/"));
    std::vector<uint8_t> data;
    CHECK(!zip.read("scripts/borg.shader", data) && data == toBytes("hello"));
    CHECK(!zip.read("MAPS/LIST.TXT", data) && data == toBytes(text));
    CHECK(!zip.read("empty.cfg", data) && data.empty());
    CHECK(zip.read("missing.txt", data));

    // A wrong CRC, and something that isn't a zip.
    auto damaged = test::buildZip({{"a.txt", toBytes("abc"), {}}});
    damaged[30 + 5] = 'X'; // the file's data, after the 30-byte header and name
    test::writeFile(path, damaged);
    CHECK(!zip.open(path) && zip.read("a.txt", data));
    test::writeFile(path, toBytes("definitely not a zip archive"));
    CHECK(zip.open(path));
    std::filesystem::remove_all(tmp);
}

static void testSearchSet(const std::filesystem::path& tmp) {
    using test::toBytes;
    std::filesystem::remove_all(tmp);
    test::writeFile(tmp / "low.zip", test::buildZip({{"a.txt", toBytes("low"), {}}, {"b.txt", toBytes("b"), {}}}));
    test::writeFile(tmp / "high.zip", test::buildZip({{"A.TXT", toBytes("high"), {}}}));
    test::writeFile(tmp / "dir" / "Sub" / "C.txt", toBytes("loose"));

    SearchSet set;
    auto open = [](const std::filesystem::path& p) {
        auto z = std::make_unique<ZipArchive>();
        CHECK(!z->open(p));
        return z;
    };
    set.add(open(tmp / "low.zip"), 1);
    set.add(open(tmp / "high.zip"), 2);
    auto dir = std::make_unique<DirectoryArchive>();
    CHECK(!dir->open(tmp / "dir"));
    set.add(std::move(dir), 0);

    std::vector<uint8_t> data;
    CHECK(!set.read("a.txt", data) && data == toBytes("high"));
    CHECK(!set.read("b.txt", data) && data == toBytes("b"));
    CHECK(!set.read("sub/c.txt", data) && data == toBytes("loose"));
    CHECK(set.read("d.txt", data));
    const auto names = set.list();
    CHECK(names.size() == 3 && names[0] == "A.TXT" && names[2] == "Sub/C.txt");
    CHECK(set.archives().size() == 3 && set.archives()[0] == set.find("a.txt"));
    CHECK(normalizeArchiveName("\\\\Textures\\\\Base//Floor.TGA") == "textures/base/floor.tga");
    std::filesystem::remove_all(tmp);
}

static void testImage() {
    // 2x2 uncompressed 24-bit TGA, stored bottom row first (descriptor 0).
    std::vector<uint8_t> tga = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 2, 0, 24, 0};
    for (uint8_t v : {0, 0, 255, 0, 255, 0,      // bottom: red, green (stored B, G, R)
                      255, 0, 0, 255, 255, 255}) // top: blue, white
        tga.push_back(v);
    image::Image img;
    CHECK(!image::decode(tga, img));
    CHECK(img.w == 2 && img.h == 2);
    auto is = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        const uint8_t* p = img.pixel(x, y);
        return p[0] == r && p[1] == g && p[2] == b && p[3] == a;
    };
    CHECK(is(0, 0, 0, 0, 255, 255) && is(1, 0, 255, 255, 255, 255));
    CHECK(is(0, 1, 255, 0, 0, 255) && is(1, 1, 0, 255, 0, 255));

    // 3x1 RLE 32-bit TGA, top row first (descriptor 0x28): a run of 3.
    const std::vector<uint8_t> rle = {0, 0, 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 1, 0, 32, 0x28,
                                      0x82, 10, 20, 30, 128};
    CHECK(!image::decode(rle, img));
    CHECK(img.w == 3 && img.h == 1 && is(0, 0, 30, 20, 10, 128) && is(2, 0, 30, 20, 10, 128));
    CHECK(image::decode({1, 2, 3}, img));

    // Blending half-transparent red over black.
    Surface s;
    s.create(2, 1, PixelFormat::RGBA32);
    img.w = 1;
    img.h = 1;
    img.rgba = {255, 0, 0, 128};
    image::blit(s, img, 1, 0);
    image::blit(s, img, 5, 5); // clipped away
    CHECK(s.row(0)[0] == 0 && s.row(0)[4] == 128 && s.row(0)[7] == 255);
}

static void testGfxMath() {
    using namespace reluva::gfx;
    auto approx = [](float a, float b) { return std::fabs(a - b) < 1e-4f; };
    auto same = [&](const Vec3& a, const Vec3& b) { return approx(a[0], b[0]) && approx(a[1], b[1]) && approx(a[2], b[2]); };
    // Depth runs 0 at the near plane to 1 at the far one; +Y stays up.
    const Mat4 proj = perspective(3.14159265f / 2, 1, 4, 100);
    CHECK(same(transformPoint(proj, {0, 0, -4}), {0, 0, 0}));
    CHECK(same(transformPoint(proj, {0, 0, -100}), {0, 0, 1}));
    CHECK(same(transformPoint(proj, {4, 4, -4}), {1, 1, 0}));
    // Looking along +X from (10, 0, 0) with Z up: +X is ahead (-Z in view
    // space), +Y is to the left, +Z up.
    const Mat4 view = lookAlong({10, 0, 0}, {1, 0, 0}, {0, 0, 1});
    CHECK(same(transformPoint(view, {15, 0, 0}), {0, 0, -5}));
    CHECK(same(transformPoint(view, {10, 2, 0}), {-2, 0, 0}));
    CHECK(same(transformPoint(view, {10, 0, 3}), {0, 3, 0}));
    CHECK(multiply(identity(), view) == view && multiply(view, identity()) == view);
    const Mat4 both = multiply(proj, view);
    CHECK(same(transformPoint(both, {14, 0, 0}), transformPoint(proj, transformPoint(view, {14, 0, 0}))));
}

// Tracker modules built in memory: one square-wave sample, a few patterns.
namespace trackertest {

using Bytes = std::vector<uint8_t>;

void put16(Bytes& b, size_t at, unsigned v) {
    if (b.size() < at + 2)
        b.resize(at + 2);
    b[at] = uint8_t(v), b[at + 1] = uint8_t(v >> 8);
}
void put32(Bytes& b, size_t at, uint32_t v) {
    put16(b, at, v & 0xffff);
    put16(b, at + 2, v >> 16);
}
void putText(Bytes& b, size_t at, const char* s) {
    for (size_t i = 0; s[i]; ++i) {
        if (b.size() <= at + i)
            b.resize(at + i + 1);
        b[at + i] = uint8_t(s[i]);
    }
}
void align16(Bytes& b) { b.resize((b.size() + 15) / 16 * 16); }

constexpr int kFrames = 300; // the sample's length
int square(int i) { return i / 8 % 2 ? 48 : -48; }

struct Note {
    int row, channel;      // rows count on across patterns: row 64 is pattern 1's first
    int note = 0;          // 61 = C-5 (MOD: a period); 254 cut, 255 off
    int volume = -1;       // the volume column, -1 none
    int fx = 0, param = 0; // in the format's own numbering
};

// 4-channel M.K.; notes are periods.
Bytes mod(const std::vector<int>& orders, int patterns, const std::vector<Note>& notes) {
    Bytes b(1084 + size_t(patterns) * 1024);
    putText(b, 0, "test");
    b[42] = kFrames / 2 >> 8, b[43] = kFrames / 2 & 255; // sample 1: length in words
    b[45] = 64;                                           // volume
    b[49] = 1;                                            // loop length 1 word: none
    b[950] = uint8_t(orders.size());
    b[951] = 127;
    for (size_t i = 0; i < orders.size(); ++i)
        b[952 + i] = uint8_t(orders[i]);
    putText(b, 1080, "M.K.");
    for (const Note& n : notes) {
        const size_t at = 1084 + size_t(n.row / 64) * 1024 + size_t(n.row % 64) * 16 + size_t(n.channel) * 4;
        const int sample = n.note ? 1 : 0;
        b[at] = uint8_t(n.note >> 8);
        b[at + 1] = uint8_t(n.note);
        b[at + 2] = uint8_t(sample << 4 | n.fx);
        b[at + 3] = uint8_t(n.param);
    }
    for (int i = 0; i < kFrames; ++i)
        b.push_back(uint8_t(square(i)));
    return b;
}

// Unsigned 8-bit sample, looping when `loop`.
Bytes s3m(const std::vector<int>& orders, int patterns, const std::vector<Note>& notes, int speed, int tempo,
          bool loop) {
    Bytes b(0x60);
    putText(b, 0, "test");
    b[0x1C] = 0x1A, b[0x1D] = 16;
    put16(b, 0x20, unsigned(orders.size()));
    put16(b, 0x22, 1);
    put16(b, 0x24, unsigned(patterns));
    put16(b, 0x28, 0x1320);
    put16(b, 0x2A, 2);
    putText(b, 0x2C, "SCRM");
    b[0x30] = 64, b[0x31] = uint8_t(speed), b[0x32] = uint8_t(tempo), b[0x33] = 0xB0;
    for (int c = 0; c < 32; ++c)
        b[0x40 + size_t(c)] = c < 4 ? uint8_t(c % 2 ? 8 + c : c) : 255;
    for (int o : orders)
        b.push_back(uint8_t(o));
    const size_t ptrs = b.size();
    b.resize(b.size() + 2 + 2 * size_t(patterns));
    align16(b);
    const size_t ins = b.size();
    put16(b, ptrs, unsigned(ins / 16));
    b.resize(ins + 0x50);
    b[ins] = 1;
    put32(b, ins + 0x10, kFrames);
    put32(b, ins + 0x18, kFrames);
    b[ins + 0x1C] = 64;
    b[ins + 0x1F] = loop ? 1 : 0;
    put32(b, ins + 0x20, 8363);
    putText(b, ins + 0x4C, "SCRS");
    align16(b);
    put16(b, ins + 14, unsigned(b.size() / 16));
    for (int i = 0; i < kFrames; ++i)
        b.push_back(uint8_t(square(i) + 128));
    for (int p = 0; p < patterns; ++p) {
        align16(b);
        put16(b, ptrs + 2 + 2 * size_t(p), unsigned(b.size() / 16));
        const size_t start = b.size();
        b.resize(start + 2);
        for (int row = 0; row < 64; ++row) {
            for (const Note& n : notes) {
                if (n.row != p * 64 + row)
                    continue;
                b.push_back(uint8_t(n.channel | (n.note ? 32 : 0) | (n.volume >= 0 ? 64 : 0) | (n.fx ? 128 : 0)));
                if (n.note) {
                    b.push_back(uint8_t(n.note >= 254 ? n.note : (n.note - 13) / 12 << 4 | (n.note - 13) % 12));
                    b.push_back(n.note >= 254 ? 0 : 1);
                }
                if (n.volume >= 0)
                    b.push_back(uint8_t(n.volume));
                if (n.fx) {
                    b.push_back(uint8_t(n.fx));
                    b.push_back(uint8_t(n.param));
                }
            }
            b.push_back(0);
        }
        put16(b, start, unsigned(b.size() - start));
    }
    return b;
}

// 2 channels, linear slides, one instrument with a looping 16-bit sample;
// `envelope`: a volume envelope falling from 64 to 0 over 10 ticks.
Bytes xm(const std::vector<int>& orders, int patterns, const std::vector<Note>& notes, bool envelope) {
    Bytes b(60 + 276);
    putText(b, 0, "Extended Module: test");
    b[37] = 0x1A;
    put16(b, 58, 0x0104);
    put32(b, 60, 276);
    put16(b, 64, unsigned(orders.size()));
    put16(b, 68, 2);
    put16(b, 70, unsigned(patterns));
    put16(b, 72, 1);
    put16(b, 74, 1);
    put16(b, 76, 6);
    put16(b, 78, 125);
    for (size_t i = 0; i < orders.size(); ++i)
        b[80 + i] = uint8_t(orders[i]);
    for (int p = 0; p < patterns; ++p) {
        const size_t h = b.size();
        b.resize(h + 9);
        put32(b, h, 9);
        put16(b, h + 5, 64);
        for (int row = 0; row < 64; ++row)
            for (int c = 0; c < 2; ++c) {
                const Note* found = nullptr;
                for (const Note& n : notes)
                    if (n.row == p * 64 + row && n.channel == c)
                        found = &n;
                if (!found) {
                    b.push_back(0x80);
                    continue;
                }
                b.push_back(uint8_t(found->note == 255 ? 97 : found->note ? found->note - 12 : 0));
                b.push_back(found->note && found->note != 255 ? 1 : 0);
                b.push_back(uint8_t(found->volume >= 0 ? 0x10 + found->volume : 0));
                b.push_back(uint8_t(found->fx));
                b.push_back(uint8_t(found->param));
            }
        put16(b, h + 7, unsigned(b.size() - h - 9));
    }
    const size_t ins = b.size();
    b.resize(ins + 263);
    put32(b, ins, 263);
    put16(b, ins + 27, 1);
    put32(b, ins + 29, 40);
    if (envelope) {
        put16(b, ins + 129, 0), put16(b, ins + 131, 64), put16(b, ins + 133, 10), put16(b, ins + 135, 0);
        b[ins + 225] = 2;
        b[ins + 233] = 1;
    }
    const size_t smp = b.size();
    b.resize(smp + 40);
    put32(b, smp, kFrames * 2);
    put32(b, smp + 8, kFrames * 2);
    b[smp + 12] = 64;
    b[smp + 14] = 0x10 | 1; // 16-bit, forward loop
    b[smp + 15] = 128;
    int old = 0;
    for (int i = 0; i < kFrames; ++i) {
        const int v = square(i) * 256;
        b.push_back(uint8_t((v - old) & 0xff));
        b.push_back(uint8_t((v - old) >> 8 & 0xff));
        old = v;
    }
    return b;
}

// Instrument mode, linear slides, one instrument mapping every note to a
// looping IT 2.14-compressed 8-bit sample.
Bytes it(const std::vector<int>& orders, int patterns, const std::vector<Note>& notes) {
    Bytes b(0xC0);
    putText(b, 0, "IMPMtest");
    put16(b, 0x20, unsigned(orders.size()));
    put16(b, 0x22, 1);
    put16(b, 0x24, 1);
    put16(b, 0x26, unsigned(patterns));
    put16(b, 0x28, 0x214);
    put16(b, 0x2A, 0x214);
    put16(b, 0x2C, 1 | 4 | 8);
    b[0x30] = 128, b[0x31] = 64, b[0x32] = 6, b[0x33] = 125, b[0x34] = 128;
    for (int c = 0; c < 64; ++c)
        b[0x40 + size_t(c)] = 32, b[0x80 + size_t(c)] = 64;
    for (int o : orders)
        b.push_back(uint8_t(o));
    const size_t ptrs = b.size();
    b.resize(ptrs + 4 * (2 + size_t(patterns)));
    const size_t ins = b.size();
    put32(b, ptrs, uint32_t(ins));
    b.resize(ins + 554);
    putText(b, ins, "IMPI");
    b[ins + 0x18] = 128, b[ins + 0x19] = 0x80 | 32;
    for (size_t n = 0; n < 120; ++n)
        b[ins + 0x40 + 2 * n] = uint8_t(n), b[ins + 0x41 + 2 * n] = 1;
    const size_t smp = b.size();
    put32(b, ptrs + 4, uint32_t(smp));
    b.resize(smp + 0x50);
    putText(b, smp, "IMPS");
    b[smp + 0x11] = 64, b[smp + 0x12] = 1 | 8 | 0x10, b[smp + 0x13] = 64, b[smp + 0x2E] = 1, b[smp + 0x2F] = 32;
    put32(b, smp + 0x30, kFrames);
    put32(b, smp + 0x38, kFrames);
    put32(b, smp + 0x3C, 8363);
    put32(b, smp + 0x48, uint32_t(b.size()));
    // One block of 9-bit deltas (bit 8 clear: a value, not a width change).
    Bytes bits((kFrames * 9 + 7) / 8);
    for (int i = 0, bit = 0; i < kFrames; ++i) {
        const unsigned v = uint8_t(square(i) - (i ? square(i - 1) : 0));
        for (int k = 0; k < 9; ++k, ++bit)
            bits[size_t(bit / 8)] |= uint8_t((v >> k & 1) << (bit % 8));
    }
    put16(b, b.size(), unsigned(bits.size()));
    b.insert(b.end(), bits.begin(), bits.end());
    for (int p = 0; p < patterns; ++p) {
        const size_t h = b.size();
        put32(b, ptrs + 8 + 4 * size_t(p), uint32_t(h));
        b.resize(h + 8);
        put16(b, h + 2, 64);
        for (int row = 0; row < 64; ++row) {
            for (const Note& n : notes) {
                if (n.row != p * 64 + row)
                    continue;
                const bool real = n.note && n.note < 254;
                b.push_back(uint8_t((n.channel + 1) | 0x80));
                b.push_back(uint8_t((n.note ? 1 : 0) | (real ? 2 : 0) | (n.volume >= 0 ? 4 : 0) | (n.fx ? 8 : 0)));
                if (n.note)
                    b.push_back(uint8_t(n.note >= 254 ? n.note : n.note - 1));
                if (real)
                    b.push_back(1);
                if (n.volume >= 0)
                    b.push_back(uint8_t(n.volume));
                if (n.fx) {
                    b.push_back(uint8_t(n.fx));
                    b.push_back(uint8_t(n.param));
                }
            }
            b.push_back(0);
        }
        put16(b, h, unsigned(b.size() - h - 8));
    }
    return b;
}

// The loudest sample in frames [from, to).
int peak(const audio::Sound& s, size_t from, size_t to) {
    int p = 0;
    for (size_t i = from * 2; i < std::min(to * 2, s.samples.size()); ++i)
        p = std::max(p, std::abs(int(s.samples[i])));
    return p;
}

} // namespace trackertest

static void testTracker() {
    using namespace reluva::audio;
    using namespace trackertest;
    constexpr size_t kTick = 882, kRow = 6 * kTick, kPattern = 64 * kRow; // speed 6, tempo 125, 44100 Hz
    TrackerModule m;
    Sound s;

    // MOD: a C-2 (period 428) at the start of a one-pattern song, which then
    // loops to its start. The 300-frame sample at 8363 Hz lasts ~1582 frames.
    const Bytes modBytes = mod({0}, 1, {{0, 0, 428}});
    CHECK(!loadTrackerModule(modBytes, m));
    CHECK(trackerFormat(m) == "MOD" && m.channels == 4 && m.samples.size() == 31);
    CHECK(m.samples[0].data.size() == kFrames && m.samples[0].data[8] == 48 * 256);
    CHECK(!renderTrackerModule(m, 44100, 0, 60, s));
    CHECK(s.rate == 44100 && s.channels == 2 && s.frames() == kPattern);
    CHECK(s.loopStart == 0 && s.loopEnd == kPattern);
    CHECK(peak(s, 0, 1000) > 1000 && peak(s, 1700, kPattern) == 0);
    CHECK(s.samples[0] != 0 && s.samples[1] == 0); // channel 1 is hard left
    // maxSeconds caps the render.
    CHECK(!renderTrackerModule(m, 44100, 0, 0.5, s) && s.frames() == 22050 && s.loopStart == 0);

    // Cxx scales the volume.
    Sound full, half;
    CHECK(!loadTrackerModule(mod({0}, 1, {{0, 0, 428, -1, 0xC, 0x40}}), m));
    CHECK(!renderTrackerModule(m, 44100, 0, 60, full));
    CHECK(!loadTrackerModule(mod({0}, 1, {{0, 0, 428, -1, 0xC, 0x20}}), m));
    CHECK(!renderTrackerModule(m, 44100, 0, 60, half));
    const int pf = peak(full, 0, 1000), ph = peak(half, 0, 1000);
    CHECK(ph > 0 && std::abs(pf - 2 * ph) <= 2);

    // A position jump back to order 1 from order 2's first row: the loop
    // starts at order 1. Starting at order 1 plays from there.
    CHECK(!loadTrackerModule(mod({0, 1, 2}, 3, {{0, 0, 428}, {128, 0, 0, -1, 0xB, 1}}), m));
    CHECK(!renderTrackerModule(m, 44100, 0, 60, s));
    CHECK(s.frames() == 2 * kPattern + kRow && s.loopStart == kPattern);
    CHECK(!renderTrackerModule(m, 44100, 1, 60, s));
    CHECK(s.frames() == kPattern + kRow && s.loopStart == 0 && peak(s, 0, s.frames()) == 0);
    // Speed 3 (F03) and a pattern break (D62) at row 1 to order 1's row 62.
    CHECK(!loadTrackerModule(mod({0, 1}, 2, {{0, 0, 0, -1, 0xF, 3}, {1, 0, 0, -1, 0xD, 0x62}}), m));
    CHECK(!renderTrackerModule(m, 44100, 0, 60, s));
    CHECK(s.frames() == 4 * 3 * kTick && s.loopStart == 0);

    // S3M: speed 3, tempo 150 (735 frames a tick); a looping sample cut (^^) at row 4.
    const Bytes s3mBytes = s3m({0, 255}, 1, {{0, 0, 61}, {4, 0, 254}}, 3, 150, true);
    CHECK(!loadTrackerModule(s3mBytes, m));
    CHECK(trackerFormat(m) == "S3M" && m.channels == 4 && m.samples[0].loop);
    CHECK(m.samples[0].data.size() == kFrames && m.samples[0].data[0] == -48 * 256);
    CHECK(!renderTrackerModule(m, 44100, 0, 60, s));
    CHECK(s.frames() == 64 * 3 * 735 && s.loopStart == 0);
    CHECK(peak(s, 0, 4 * 3 * 735) > 1000 && peak(s, 4 * 3 * 735, s.frames()) == 0);
    // Speed (A02) and tempo (T250) from the pattern; the volume column.
    CHECK(!loadTrackerModule(s3m({0}, 1, {{0, 0, 61, 32, 1, 2}, {1, 1, 0, -1, 20, 250}}, 6, 125, true), m));
    CHECK(!renderTrackerModule(m, 44100, 0, 60, s));
    CHECK(s.frames() == 2 * kTick + 63 * 2 * 441);
    CHECK(peak(s, 0, 1000) > 0 && peak(s, 0, 1000) < 9000);

    // XM: a looping 16-bit sample keyed off at row 8 (no envelope: a cut).
    const Bytes xmBytes = xm({0}, 1, {{0, 0, 61}, {8, 0, 255}}, false);
    CHECK(!loadTrackerModule(xmBytes, m));
    CHECK(trackerFormat(m) == "XM" && m.instruments.size() == 1 && m.samples.size() == 1 && m.linearSlides);
    CHECK(m.samples[0].data.size() == kFrames && m.samples[0].data[8] == 48 * 256 &&
          m.samples[0].data[0] == -48 * 256);
    CHECK(!renderTrackerModule(m, 44100, 0, 60, s));
    CHECK(s.frames() == kPattern && peak(s, 7 * kRow, 8 * kRow) > 1000 && peak(s, 8 * kRow + 1, kPattern) == 0);
    // A volume column of 32 halves it; the envelope silences it after 10 ticks.
    CHECK(!loadTrackerModule(xm({0}, 1, {{0, 1, 61, 32}}, false), m));
    CHECK(!renderTrackerModule(m, 44100, 0, 60, half));
    CHECK(!loadTrackerModule(xm({0}, 1, {{0, 1, 61}}, false), m));
    CHECK(!renderTrackerModule(m, 44100, 0, 60, full));
    CHECK(peak(half, 0, 1000) > 0 && std::abs(peak(full, 0, 1000) - 2 * peak(half, 0, 1000)) <= 2);
    CHECK(!loadTrackerModule(xm({0}, 1, {{0, 1, 61}}, true), m));
    CHECK(!renderTrackerModule(m, 44100, 0, 60, s));
    CHECK(peak(s, 0, kTick) > 1000 && peak(s, 4 * kTick, 5 * kTick) < peak(s, 0, kTick) &&
          peak(s, 11 * kTick, kPattern) == 0);

    // IT: instruments, a compressed sample, a note cut at row 4, and a jump
    // (B00) back to order 0 at order 1's row 2.
    const Bytes itBytes = it({0, 1, 255}, 2, {{0, 0, 61}, {4, 0, 254}, {66, 1, 0, -1, 2, 0}});
    CHECK(!loadTrackerModule(itBytes, m));
    CHECK(trackerFormat(m) == "IT" && m.instruments.size() == 1 && m.samples.size() == 1);
    bool same = m.samples[0].data.size() == kFrames;
    for (int i = 0; same && i < kFrames; ++i)
        same = m.samples[0].data[size_t(i)] == square(i) * 256;
    CHECK(same);
    CHECK(!renderTrackerModule(m, 44100, 0, 60, s));
    CHECK(s.frames() == kPattern + 3 * kRow && s.loopStart == 0);
    CHECK(peak(s, 0, 4 * kRow) > 1000 && peak(s, 4 * kRow, s.frames()) == 0);
    CHECK(s.samples[0] == s.samples[1]); // channel pan 32: the middle

    // Garbage and truncations of the modules: an error or a module, never a crash.
    Bytes junk(4000);
    uint32_t seed = 1;
    for (uint8_t& b : junk)
        b = uint8_t((seed = seed * 1103515245 + 12345) >> 16);
    CHECK(bool(loadTrackerModule(junk, m)));
    CHECK(bool(loadTrackerModule({}, m)));
    CHECK(bool(loadTrackerModule(Bytes(modBytes.begin(), modBytes.begin() + 1500), m))); // patterns cut
    CHECK(!loadTrackerModule(Bytes(modBytes.begin(), modBytes.end() - 100), m));         // a sample cut: kept
    for (const Bytes* whole : {&modBytes, &s3mBytes, &xmBytes, &itBytes})
        for (size_t n = 0; n < whole->size(); n += 7) {
            Bytes cut(whole->begin(), whole->begin() + long(n));
            for (size_t i = 0x40; i < cut.size(); i += 61) // and some bytes changed
                cut[i] ^= 0x5A;
            if (!loadTrackerModule(cut, m))
                renderTrackerModule(m, 8000, 0, 0.2, s);
        }
    CHECK(!loadTrackerModule(modBytes, m) && bool(renderTrackerModule(m, 44100, 99, 1, s))); // no such order
}

int main() {
    auto tmp = std::filesystem::temp_directory_path() / "reluva_tests";
    testMd5();
    testUtf16();
    testFont();
    testWav();
    testMp3();
    testMixer();
    testTracker();
    testFsAndDetection(tmp / "fs");
    testConfig(tmp / "cfg");
    testZip(tmp / "zip");
    testSearchSet(tmp / "search");
    testImage();
    testGfxMath();
    if (failures)
        std::fprintf(stderr, "%d check(s) failed\n", failures);
    else
        std::printf("All tests passed\n");
    return failures ? 1 : 0;
}
