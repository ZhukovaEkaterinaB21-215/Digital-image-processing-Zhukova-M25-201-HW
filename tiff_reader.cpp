#include "tiff_reader.h"

#include <cstdio>
#include <cstring>
#include <vector>

enum TiffType {
    T_BYTE = 1, 
    T_ASCII = 2, 
    T_SHORT = 3, 
    T_LONG = 4, 
    T_RATIONAL = 5 
};

struct TagEntry {
    uint16_t tag;
    uint16_t type;
    uint32_t count;
    uint32_t valueOrOffset;
};

static bool g_littleEndian = true;

static uint16_t read16(const uint8_t* p) {
    if (g_littleEndian) {
        return (uint16_t)(p[0] | (p[1] << 8));
    }
    else {
        return (uint16_t)((p[0] << 8) | p[1]);
    }
}

static uint32_t read32(const uint8_t* p) {
    if (g_littleEndian) {
        return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
    }
    else {
        return (uint32_t)((p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]);
    }
}

static uint32_t tagScalar(const TagEntry& e) {
    if (e.type == T_SHORT) {
        return e.valueOrOffset & 0xFFFFu;
    }
    return e.valueOrOffset;
}

static std::vector<uint32_t> readLongArray(const std::vector<uint8_t>& data, uint32_t offset, uint32_t count) {
    std::vector<uint32_t> arr(count);
    for (uint32_t i = 0; i < count; i++) {
        arr[i] = read32(&data[offset + i * 4]);
    }
    return arr;
}

static std::vector<uint16_t> readShortArray(const std::vector<uint8_t>& data, uint32_t offset, uint32_t count) {
    std::vector<uint16_t> arr(count);
    for (uint32_t i = 0; i < count; i++) {
        arr[i] = read16(&data[offset + i * 2]);
    }
    return arr;
}

bool loadTIFF(const char* filename, Image& img) {
    FILE* f = fopen(filename, "rb");
    if (!f) { 
        fprintf(stderr, "Cannot open: %s\n", filename); 
        return false; 
    }

    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);

    std::vector<uint8_t> data((size_t)fileSize);
    if ((long)fread(data.data(), 1, (size_t)fileSize, f) != fileSize) {
        fclose(f); 
        fprintf(stderr, "Read error\n"); 
        return false;
    }
    fclose(f);

    if (data[0] == 'I' && data[1] == 'I') {
        g_littleEndian = true;
    }
    else {
        if (data[0] == 'M' && data[1] == 'M') {
            g_littleEndian = false;
        }
        else { 
            fprintf(stderr, "Not a TIFF\n"); 
            return false; 
        }
    }

    if (read16(&data[2]) != 42) { 
        fprintf(stderr, "Bad TIFF magic\n"); 
        return false; 
    }

    uint32_t ifdOffset = read32(&data[4]);
    printf("Byte order : %s\n", g_littleEndian ? "Intel (LE)" : "Motorola (BE)");

    uint16_t numEntries = read16(&data[ifdOffset]);
    std::vector<TagEntry> entries((size_t)numEntries);
    uint32_t pos = ifdOffset + 2;
    for (int i = 0; i < (int)numEntries; i++) {
        entries[i].tag = read16(&data[pos]);
        entries[i].type = read16(&data[pos + 2]);
        entries[i].count = read32(&data[pos + 4]);
        entries[i].valueOrOffset = read32(&data[pos + 8]);
        pos += 12;
    }

    uint32_t width = 0, height = 0, bitsPerSample = 8, compression = 1;
    uint32_t rowsPerStrip = 0, photometric = 1;
    std::vector<uint32_t> stripOffsets, stripByteCounts;

    for (size_t i = 0; i < entries.size(); i++) {
        const TagEntry& e = entries[i];
        switch (e.tag) {
        case 256: width = tagScalar(e); break;
        case 257: height = tagScalar(e); break;
        case 258: bitsPerSample = tagScalar(e); break;
        case 259: compression = tagScalar(e); break;
        case 262: photometric = tagScalar(e); break;
        case 278: rowsPerStrip = tagScalar(e); break;
        case 273:
            if (e.count == 1) {
                stripOffsets.push_back((e.type == T_LONG) ? e.valueOrOffset : tagScalar(e));
            }
            else if (e.type == T_LONG) {
                stripOffsets = readLongArray(data, e.valueOrOffset, e.count);
            }
            else {
                std::vector<uint16_t> sv = readShortArray(data, e.valueOrOffset, e.count);
                for (size_t j = 0; j < sv.size(); j++) {
                    stripOffsets.push_back(sv[j]);
                }
            }
            break;
        case 279:
            if (e.count == 1) {
                stripByteCounts.push_back((e.type == T_LONG) ? e.valueOrOffset : tagScalar(e));
            }
            else if (e.type == T_LONG) {
                stripByteCounts = readLongArray(data, e.valueOrOffset, e.count);
            }
            else {
                std::vector<uint16_t> sv = readShortArray(data, e.valueOrOffset, e.count);
                for (size_t j = 0; j < sv.size(); j++) {
                    stripByteCounts.push_back(sv[j]);
                }
            }
            break;
        }
    }

    printf("Size: %ux%u, BPS=%u, Compression=%u, Strips=%u\n", width, height, bitsPerSample, compression, (unsigned)stripOffsets.size());

    if (compression != 1) { 
        fprintf(stderr, "Unsupported compression: %u\n", compression); 
        return false; 
    }

    if (bitsPerSample != 8) { 
        fprintf(stderr, "Only 8-bit supported\n"); 
        return false; 
    }

    img.width = (int)width;
    img.height = (int)height;
    img.pixels.clear();
    int total_pixels = width * height;
    for (int i = 0; i < total_pixels; ++i) {
        img.pixels.push_back(255);
    }

    uint32_t rowDst = 0;
    for (size_t s = 0; s < stripOffsets.size(); s++) {
        uint32_t off = stripOffsets[s];
        uint32_t bc = stripByteCounts[s];
        uint32_t rows = (s + 1 < stripOffsets.size()) ? rowsPerStrip : (height - rowDst);
        if (rowDst >= height) {
            break;
        }
        uint32_t expected = rows * width;
        if (bc < expected) {
            expected = bc;
        }
        memcpy(&img.pixels[(size_t)(rowDst * width)], &data[off], expected);
        rowDst += rows;
    }

    if (photometric == 0) {
        for (size_t i = 0; i < img.pixels.size(); i++) {
            img.pixels[i] = (uint8_t)(255u - img.pixels[i]);
        }
    }

    printf("Loaded: %dx%d px\n\n", img.width, img.height);
    return true;
}
