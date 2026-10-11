/**
 * \file tests/lib-unit-tests/test_template_psd.cpp
 * \brief Tests for the PSD templates TemplateGenerator writes (through minipsd): read back with a small
 *        reader that follows the format description, so a field written wrong fails here rather than in a
 *        drawing app. minipsd's own tests, and its independent psd-tools round trip, live in its repository.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * \copyright Copyright (c) 2026 Bartlomiej Mucha <shadobadev@gmail.com>
 */

#include <minipsd.h>   // MINIPSD_MAX_SIDE only; the implementation is compiled into the lib

#include <platemaker/core/template_generator/template_generator.hpp>

#include <gtest/gtest.h>
#include <vips/vips.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

// ---------------------------------------------------------------------------------------------------
// The reader: just enough of the format to check every field the writer sets.
// ---------------------------------------------------------------------------------------------------

struct Layer {
    std::int32_t top = 0, left = 0, bottom = 0, right = 0;
    std::string  pascalName;
    std::u16string unicodeName;
    std::uint8_t opacity = 0, flags = 0;
    std::vector<int>  channelIds;
    std::vector<int>  compression;   // per channel
    Bytes rgba;                      // decoded, interleaved; empty for an empty layer
};

struct Psd {
    std::uint16_t channels = 0, depth = 0, mode = 0;
    std::uint32_t width = 0, height = 0;
    double dpiX = 0, dpiY = 0;
    Bytes  icc;
    std::int16_t rawLayerCount = 0;
    std::vector<Layer> layers;
    int    compositeCompression = -1;
    std::vector<std::uint16_t> compositeRowCounts;
    Bytes  composite;   // decoded, interleaved RGBA
};

class Reader {
public:
    explicit Reader(const Bytes &b) : m_b(b) {}
    std::uint8_t  u8()  { need(1); return m_b[m_pos++]; }
    std::uint16_t u16() { const std::uint16_t hi = u8(); return static_cast<std::uint16_t>((hi << 8) | u8()); }
    std::uint32_t u32() { const std::uint32_t hi = u16(); return (hi << 16) | u16(); }
    std::string   tag(std::size_t n) { need(n); std::string s(m_b.begin() + m_pos, m_b.begin() + m_pos + n); m_pos += n; return s; }
    void          skip(std::size_t n) { need(n); m_pos += n; }
    std::size_t   pos() const { return m_pos; }
    void          seek(std::size_t p) { m_pos = p; }
    const std::uint8_t *at() const { return m_b.data() + m_pos; }
private:
    void need(std::size_t n) const { if (m_pos + n > m_b.size()) throw std::runtime_error("read past end"); }
    const Bytes &m_b;
    std::size_t  m_pos = 0;
};

// PackBits: n < 128 copies n + 1 bytes; n > 128 repeats the next byte 257 - n times; 128 is a no-op.
Bytes unpack(Reader &r, std::size_t packed, std::size_t expected)
{
    Bytes out;
    const std::size_t end = r.pos() + packed;
    while (r.pos() < end) {
        const std::uint8_t n = r.u8();
        if (n < 128)       for (int i = 0; i <= n; ++i) out.push_back(r.u8());
        else if (n > 128)  out.insert(out.end(), 257 - n, r.u8());
    }
    if (out.size() != expected) throw std::runtime_error("RLE row has the wrong length");
    return out;
}

// One channel's rows (raw or RLE, counts read by the caller), scattered into interleaved RGBA.
void readPlane(Reader &r, int compression, const std::vector<std::uint16_t> &counts,
               std::uint32_t w, std::uint32_t h, Bytes &rgba, int offset)
{
    for (std::uint32_t y = 0; y < h; ++y) {
        Bytes row;
        if (compression == 0) { row.assign(r.at(), r.at() + w); r.skip(w); }
        else                  row = unpack(r, counts[y], w);
        for (std::uint32_t x = 0; x < w; ++x) rgba[(y * w + x) * 4 + offset] = row[x];
    }
}

int rgbaOffset(int id) { return id == -1 ? 3 : id; }

Psd read(const Bytes &b)
{
    Reader r(b);
    Psd p;
    if (r.tag(4) != "8BPS" || r.u16() != 1) throw std::runtime_error("not a PSD");
    r.skip(6);
    p.channels = r.u16(); p.height = r.u32(); p.width = r.u32(); p.depth = r.u16(); p.mode = r.u16();
    r.skip(r.u32());                                         // colour mode data

    const std::size_t resLen = r.u32();
    const std::size_t resEnd = r.pos() + resLen;   // two statements: operand order is unspecified
    while (r.pos() < resEnd) {
        if (r.tag(4) != "8BIM") throw std::runtime_error("bad resource signature");
        const std::uint16_t id = r.u16();
        const std::uint8_t nameLen = r.u8(); r.skip(nameLen + ((nameLen + 1) & 1 ? 1 : 0));
        const std::uint32_t size = r.u32();
        const std::size_t start = r.pos();
        if (id == 1005) {
            p.dpiX = r.u32() / 65536.0; r.skip(4);
            p.dpiY = r.u32() / 65536.0;
        } else if (id == 1039) {
            p.icc.assign(r.at(), r.at() + size);
        }
        r.seek(start + size + (size & 1));
    }

    const std::size_t lmLen = r.u32();
    const std::size_t lmEnd = r.pos() + lmLen;   // two statements: operand order is unspecified
    const std::size_t infoLen = r.u32();
    if (infoLen % 2) throw std::runtime_error("layer info length is odd");
    p.rawLayerCount = static_cast<std::int16_t>(r.u16());
    const int count = p.rawLayerCount < 0 ? -p.rawLayerCount : p.rawLayerCount;
    std::vector<std::vector<std::uint32_t>> lengths(count);
    for (int i = 0; i < count; ++i) {
        Layer l;
        l.top = static_cast<std::int32_t>(r.u32()); l.left = static_cast<std::int32_t>(r.u32());
        l.bottom = static_cast<std::int32_t>(r.u32()); l.right = static_cast<std::int32_t>(r.u32());
        const std::uint16_t nch = r.u16();
        for (int c = 0; c < nch; ++c) { l.channelIds.push_back(static_cast<std::int16_t>(r.u16())); lengths[i].push_back(r.u32()); }
        if (r.tag(8) != "8BIMnorm") throw std::runtime_error("bad blend signature");
        l.opacity = r.u8(); r.skip(1); l.flags = r.u8(); r.skip(1);
        const std::size_t extraLen = r.u32();
        const std::size_t extraEnd = r.pos() + extraLen;   // two statements: operand order is unspecified
        r.skip(r.u32());                                     // mask
        r.skip(r.u32());                                     // blending ranges
        const std::uint8_t n = r.u8();
        l.pascalName = r.tag(n);
        r.skip((4 - (n + 1) % 4) % 4);
        while (r.pos() < extraEnd) {
            if (r.tag(4) != "8BIM") throw std::runtime_error("bad additional-info signature");
            const std::string key = r.tag(4);
            const std::uint32_t size = r.u32();
            const std::size_t start = r.pos();
            if (key == "luni") {
                const std::uint32_t units = r.u32();
                for (std::uint32_t u = 0; u < units; ++u) l.unicodeName.push_back(static_cast<char16_t>(r.u16()));
            }
            r.seek(start + size);
        }
        if (r.pos() != extraEnd) throw std::runtime_error("layer extra data overran");
        p.layers.push_back(l);
    }
    for (int i = 0; i < count; ++i) {
        Layer &l = p.layers[i];
        const auto w = static_cast<std::uint32_t>(l.right - l.left), h = static_cast<std::uint32_t>(l.bottom - l.top);
        if (w && h) l.rgba.assign(std::size_t(w) * h * 4, 0);
        for (std::size_t c = 0; c < l.channelIds.size(); ++c) {
            const std::size_t start = r.pos();
            const int comp = r.u16();
            l.compression.push_back(comp);
            std::vector<std::uint16_t> counts;
            if (comp == 1) for (std::uint32_t y = 0; y < h; ++y) counts.push_back(r.u16());
            if (w && h) readPlane(r, comp, counts, w, h, l.rgba, rgbaOffset(l.channelIds[c]));
            if (r.pos() - start != lengths[i][c]) throw std::runtime_error("channel length does not match its record");
        }
    }
    r.seek(lmEnd - 4);
    if (r.u32() != 0) throw std::runtime_error("expected an empty global layer mask");

    p.compositeCompression = r.u16();
    if (p.compositeCompression == 1)
        for (std::uint32_t i = 0; i < p.height * p.channels; ++i) p.compositeRowCounts.push_back(r.u16());
    p.composite.assign(std::size_t(p.width) * p.height * 4, 0);
    for (int c = 0; c < p.channels; ++c) {
        const std::vector<std::uint16_t> counts(p.compositeRowCounts.begin() + (p.compositeCompression == 1 ? c * p.height : 0),
                                                p.compositeRowCounts.begin() + (p.compositeCompression == 1 ? (c + 1) * p.height : 0));
        readPlane(r, p.compositeCompression, counts, p.width, p.height, p.composite, c);
    }
    if (r.pos() != b.size()) throw std::runtime_error("trailing bytes");
    return p;
}


} // namespace

// ---------------------------------------------------------------------------------------------------
// TemplateGenerator writing PSD
// ---------------------------------------------------------------------------------------------------

namespace {

using Platemaker::Core::TemplateFormat;
using Platemaker::Core::TemplateGenerator;
using Platemaker::Core::TemplateOptions;

Platemaker::Models::CanvasProfile templateProfile(int w, int h)
{
    Platemaker::Models::CanvasProfile cp;
    cp.name       = "T";
    cp.canvasSize = {w, h};
    cp.margins    = {10, 12, 14, 16};
    return cp;
}

Bytes readFile(const std::filesystem::path &file)
{
    std::ifstream in(file, std::ios::binary);
    return Bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

class TemplatePsdTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_dir = std::filesystem::temp_directory_path() / "pm-template-psd";
        std::filesystem::create_directories(m_dir);
    }
    void TearDown() override { std::filesystem::remove_all(m_dir); }

    std::string path(const char *name) const { return (m_dir / name).string(); }

    std::filesystem::path m_dir;
};

} // namespace

TEST_F(TemplatePsdTest, PsdLayerHoldsThePngPixels)
{
    const auto cp = templateProfile(64, 48);
    const Platemaker::Models::OutputProfile op;
    TemplateOptions png;
    TemplateOptions psd;
    psd.format = TemplateFormat::Psd;
    TemplateGenerator{}.generate(cp, op, path("t.png"), png);
    TemplateGenerator{}.generate(cp, op, path("t.psd"), psd);

    VipsImage *im = vips_image_new_from_file(path("t.png").c_str(), nullptr);
    ASSERT_NE(im, nullptr) << vips_error_buffer();
    size_t size = 0;
    void *mem = vips_image_write_to_memory(im, &size);
    const Bytes pngPixels(static_cast<std::uint8_t *>(mem), static_cast<std::uint8_t *>(mem) + size);
    g_free(mem);
    g_object_unref(im);

    const Psd p = read(readFile(m_dir / "t.psd"));
    EXPECT_EQ(p.width, 64u);
    EXPECT_EQ(p.height, 48u);
    EXPECT_DOUBLE_EQ(p.dpiX, 300.0);
    ASSERT_EQ(p.layers.size(), 1u) << "no empty layer unless asked";
    EXPECT_EQ(p.layers[0].pascalName, "Template");
    EXPECT_EQ(p.layers[0].rgba, pngPixels) << "the same template, in either format";
}

TEST_F(TemplatePsdTest, EmptyLayerAndDpiAreApplied)
{
    TemplateOptions o;
    o.format     = TemplateFormat::Psd;
    o.emptyLayer = true;
    o.dpi        = 72.0;
    TemplateGenerator{}.generate(templateProfile(40, 40), {}, path("t.psd"), o);
    const Psd p = read(readFile(m_dir / "t.psd"));
    ASSERT_EQ(p.layers.size(), 2u);
    EXPECT_EQ(p.layers[1].unicodeName, u"Layer");
    EXPECT_TRUE(p.layers[1].rgba.empty());
    EXPECT_DOUBLE_EQ(p.dpiX, 72.0);
}

TEST(TemplateOptionsTest, OptionsForReadsTheProfile)
{
    // The profile's resolution and template settings are the options; nothing else decides them.
    Platemaker::Models::CanvasProfile cp;
    cp.dpi              = 144.0;
    cp.templateSettings = {Platemaker::Models::TemplateFormat::Psd, true};
    const TemplateOptions o = TemplateGenerator::optionsFor(cp);
    EXPECT_EQ(o.dpi, 144.0);
    EXPECT_EQ(o.format, TemplateFormat::Psd);
    EXPECT_TRUE(o.emptyLayer);
}

TEST_F(TemplatePsdTest, OversizePsdIsAnInvalidArgument)
{
    TemplateOptions o;
    o.format = TemplateFormat::Psd;
    EXPECT_THROW(TemplateGenerator{}.generate(templateProfile(100, MINIPSD_MAX_SIDE + 1), {}, path("t.psd"), o),
                 std::invalid_argument);
    EXPECT_FALSE(std::filesystem::exists(m_dir / "t.psd")) << "refused before the file is opened";
}
