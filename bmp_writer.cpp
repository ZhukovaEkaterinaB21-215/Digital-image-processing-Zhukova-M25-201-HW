#include "bmp_writer.h"

#include <cstdio>
#include <vector>

static void writeU32LE(FILE* f, uint32_t v) {
    uint8_t b[4] = { 
        (uint8_t)v, 
        (uint8_t)(v >> 8), 
        (uint8_t)(v >> 16), 
        (uint8_t)(v >> 24) 
    };
    fwrite(b, 1, 4, f);
}

static void writeU16LE(FILE* f, uint16_t v) {
    uint8_t b[2] = { 
        (uint8_t)v, 
        (uint8_t)(v >> 8) 
    };
    fwrite(b, 1, 2, f);
}

bool saveBMP(const char* filename, const Image& img) {
    int W = img.width;
    int H = img.height;
    int rowStride = (W + 3) & ~3;
    int pixelBytes = rowStride * H;
    int paletteBytes = 256 * 4;
    int headerBytes = 14 + 40;
    int offsetToPixels = headerBytes + paletteBytes;
    int fileSize = offsetToPixels + pixelBytes;

    FILE* f = fopen(filename, "wb");
    if (!f) { 
        fprintf(stderr, "Cannot write: %s\n", filename); 
        return false; 
    }

    fwrite("BM", 1, 2, f);
    writeU32LE(f, (uint32_t)fileSize);
    writeU16LE(f, 0);
    writeU16LE(f, 0);
    writeU32LE(f, (uint32_t)offsetToPixels);

    writeU32LE(f, 40);
    writeU32LE(f, (uint32_t)W);
    writeU32LE(f, (uint32_t)H);
    writeU16LE(f, 1);
    writeU16LE(f, 8);
    writeU32LE(f, 0);
    writeU32LE(f, (uint32_t)pixelBytes);
    writeU32LE(f, 2835);
    writeU32LE(f, 2835);
    writeU32LE(f, 256);
    writeU32LE(f, 256);

    for (int i = 0; i < 256; i++) {
        uint8_t entry[4] = { 
            (uint8_t)i, 
            (uint8_t)i, 
            (uint8_t)i, 
            0 
        };
        fwrite(entry, 1, 4, f);
    }

    std::vector<uint8_t> row((size_t)rowStride, 255u);
    for (int y = H - 1; y >= 0; y--) {
        for (int x = 0; x < W; x++) {
            row[x] = img.at(y, x);
        }
        fwrite(row.data(), 1, (size_t)rowStride, f);
    }

    fclose(f);
    return true;
}

bool saveBinaryBMP(const char* filename, const Image& img, uint8_t thresh) {
    Image out;
    out.width = img.width;
    out.height = img.height;
    out.pixels.clear();
    for (int i = 0; i < img.width * img.height; ++i) {
        out.pixels.push_back(0);
    }
    for (size_t i = 0; i < img.pixels.size(); i++) {
        out.pixels[i] = (img.pixels[i] <= thresh) ? 0u : 255u;
    }
    return saveBMP(filename, out);
}
