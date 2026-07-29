#pragma array_limit 1000000
import std.core;
import std.io;
import std.math;
import std.mem;

struct UnicodeRange {
    u32 begin;
    u32 end;
};

fn get_bitmap_size(u32 index, u32 glyph_sizes_address) {
    u32 byte_index = index >> 3;
    bool wide = std::mem::read_bits(glyph_sizes_address + byte_index, index & 7, 1) == 1;
    return wide ? 32 : 16;
};

struct GlyphBitmap {
    char data[get_bitmap_size(std::core::array_index(), addressof(parent.glyph_sizes))];
};

struct NovacubeUnifont {
    char signature[4];
    u32 version;
    u32 total_glyphs;
    u8 glyph_sizes[std::math::ceil((total_glyphs) / 8.0)];
    std::mem::AlignTo<4>;
    u32 range_count;
    UnicodeRange ranges[range_count];
    GlyphBitmap bitmaps[total_glyphs];
};

NovacubeUnifont font @ 0x00;
