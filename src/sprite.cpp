#include "sprite.h"

#include <cstring>

#include "dance_data.h"

static uint16_t rd16(const uint8_t* p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

bool Sprite::load(const uint8_t* data, uint32_t size) {
    if (size < 20 || std::memcmp(data, "GDNC", 4) != 0) return false;
    uint16_t version = rd16(data + 4);
    if (version != 1) return false;

    width = rd16(data + 6);
    height = rd16(data + 8);
    palette_count = rd16(data + 10);
    frame_count = (int)rd32(data + 12);
    frame_delay_us = (int)rd32(data + 16);

    if (width <= 0 || height <= 0 || palette_count <= 0 || frame_count <= 0) return false;
    uint64_t pal_end = 20ull + (uint64_t)palette_count * 4;
    if (pal_end > size) return false;
    palette = data + 20;

    uint64_t pos = pal_end;
    frames.reserve(frame_count);
    frame_sizes.reserve(frame_count);
    for (int i = 0; i < frame_count; ++i) {
        if (pos + 4 > size) return false;
        uint32_t n = rd32(data + pos);
        pos += 4;
        if (pos + n > size) return false;
        frames.push_back(data + pos);
        frame_sizes.push_back(n);
        pos += n;
    }
    return true;
}

bool load_embedded_sprite(Sprite& sprite) {
    return sprite.load(kDanceData, kDanceSize);
}

void Sprite::decode(int index, std::vector<uint8_t>& rgba) const {
    const size_t pixels = (size_t)width * height;
    rgba.assign(pixels * 4, 0);
    if (index < 0 || index >= frame_count) return;

    const uint8_t* p = frames[index];
    const uint8_t* end = p + frame_sizes[index];
    size_t at = 0;
    while (p + 2 <= end && at < pixels) {
        uint8_t count = p[0];
        uint8_t color = p[1];
        p += 2;
        if (color >= palette_count) {
            at += count;
            continue;
        }
        const uint8_t* c = palette + (size_t)color * 4;
        for (uint8_t i = 0; i < count && at < pixels; ++i, ++at) {
            std::memcpy(&rgba[at * 4], c, 4);
        }
    }
}
