#pragma once

#include <cstdint>
#include <vector>

struct Sprite {
    int width = 0;
    int height = 0;
    int frame_count = 0;
    int frame_delay_us = 60000;
    int palette_count = 0;
    const uint8_t* palette = nullptr;
    std::vector<const uint8_t*> frames;
    std::vector<uint32_t> frame_sizes;

    bool load(const uint8_t* data, uint32_t size);
    void decode(int index, std::vector<uint8_t>& rgba) const;
};

bool load_embedded_sprite(Sprite& sprite);
