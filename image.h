#pragma once
#define _CRT_SECURE_NO_WARNINGS

#include <vector>
#include <stdint.h>

struct Image {
    int width;
    int height;
    std::vector<uint8_t> pixels;

    uint8_t& at(int y, int x) { return pixels[y * width + x]; }
    uint8_t  at(int y, int x) const { return pixels[y * width + x]; }
};
