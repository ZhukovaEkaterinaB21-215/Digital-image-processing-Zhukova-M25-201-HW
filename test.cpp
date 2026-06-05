#include "test.h"
#include "image.h"
#include "tiff_reader.h"
#include "bmp_writer.h"
#include "deskew.h"

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <stdint.h>
#include <fstream>
#include <string>

static int g_passed = 0;
static int g_failed = 0;

static void log_to_file(const std::string& filename, const std::string& text) {
    std::ofstream log(filename, std::ios::app);
    if (log.is_open()) {
        log << text << "\n";
        log.close();
    }
}

static void check(bool condition, const char* description) {
    std::string msg = std::string(condition ? "[PASS] " : "[FAIL] ") + description;
    printf("%s\n", msg.c_str());
    log_to_file("tests/test_results.txt", msg);
    if (condition) {
        g_passed++;
    }
    else {
        g_failed++;
    }
}

static void checkNear(double got, double expected, double tolerance, const char* description) {
    bool ok = fabs(got - expected) <= tolerance;
    std::string msg;
    if (ok) {
        msg = "[PASS] " + std::string(description) + ": " +
            std::to_string(got) + " (expected " + std::to_string(expected) + " +/- " + std::to_string(tolerance) + ")";
    }
    else {
        double err = fabs(got - expected);
        msg = "[FAIL] " + std::string(description) + ": " +
            std::to_string(got) + " (expected " + std::to_string(expected) + " +/- " + std::to_string(tolerance) + ", err " + std::to_string(err) + ")";
    }
    printf("%s\n", msg.c_str());
    log_to_file("tests/test_results.txt", msg);
    ok ? g_passed++ : g_failed++;
}

static Image applyThreshold(const Image& src, uint8_t t) {
    Image res;
    res.width = src.width;
    res.height = src.height;
    res.pixels.clear();
    size_t total = static_cast<size_t>(src.width) * static_cast<size_t>(src.height);
    for (size_t i = 0; i < total; ++i) {
        res.pixels.push_back(src.pixels[i] > t ? 255u : 0u);
    }
    return res;
}

static void writeSyntheticTIFF(const char* filename, const std::vector<uint8_t>& pixels,
    int W, int H, bool littleEndian, int rowsPerStrip) {
    if (rowsPerStrip <= 0) {
        rowsPerStrip = H;
    }

    std::vector<std::vector<uint8_t>> strips;
    size_t w_size = static_cast<size_t>(W);
    for (int r = 0; r < H; r += rowsPerStrip) {
        int rend = (r + rowsPerStrip < H) ? (r + rowsPerStrip) : H;
        strips.push_back(std::vector<uint8_t>(pixels.begin() + r * w_size, pixels.begin() + rend * w_size));
    }
    int numStrips = (int)strips.size();

    auto w16 = [&](FILE* f, uint16_t v) {
        uint8_t b[2];
        if (littleEndian) {
            b[0] = (uint8_t)v;
            b[1] = (uint8_t)(v >> 8);
        }
        else {
            b[0] = (uint8_t)(v >> 8);
            b[1] = (uint8_t)v;
        }
        fwrite(b, 1, 2, f);
        };

    auto w32 = [&](FILE* f, uint32_t v) {
        uint8_t b[4];
        if (littleEndian) {
            b[0] = (uint8_t)v;
            b[1] = (uint8_t)(v >> 8);
            b[2] = (uint8_t)(v >> 16);
            b[3] = (uint8_t)(v >> 24);
        }
        else {
            b[0] = (uint8_t)(v >> 24);
            b[1] = (uint8_t)(v >> 16);
            b[2] = (uint8_t)(v >> 8);
            b[3] = (uint8_t)v;
        }
        fwrite(b, 1, 4, f);
        };

    auto wTag = [&](FILE* f, uint16_t tag, uint16_t type, uint32_t count, uint32_t value) {
        w16(f, tag);
        w16(f, type);
        w32(f, count);
        w32(f, value);
        };

    const int numTags = 8;
    const int ifdSize = 2 + numTags * 12 + 4;
    const int extraStart = 8 + ifdSize;
    int pixelStart, stripOffsetsPos = 0, stripBytecountsPos = 0;
    if (numStrips > 1) {
        stripOffsetsPos = extraStart;
        stripBytecountsPos = extraStart + numStrips * 4;
        pixelStart = extraStart + numStrips * 4 * 2;
    }
    else {
        pixelStart = extraStart;
    }

    std::vector<uint32_t> stripOffsets;
    int pos = pixelStart;
    for (int s = 0; s < numStrips; s++) {
        stripOffsets.push_back((uint32_t)pos);
        pos += (int)strips[s].size();
    }

    FILE* f = fopen(filename, "wb");
    if (!f) {
        fprintf(stderr, "Cannot create %s\n", filename);
        return;
    }

    fwrite(littleEndian ? "II" : "MM", 1, 2, f);
    w16(f, 42);
    w32(f, 8);
    w16(f, (uint16_t)numTags);
    wTag(f, 256, 3, 1, (uint32_t)W);
    wTag(f, 257, 3, 1, (uint32_t)H);
    wTag(f, 258, 3, 1, 8);
    wTag(f, 259, 3, 1, 1);
    wTag(f, 262, 3, 1, 1);
    if (numStrips == 1) {
        wTag(f, 273, 4, 1, stripOffsets[0]);
    }
    else {
        wTag(f, 273, 4, (uint32_t)numStrips, (uint32_t)stripOffsetsPos);
    }
    wTag(f, 278, 3, 1, (uint32_t)rowsPerStrip);
    if (numStrips == 1) {
        wTag(f, 279, 4, 1, (uint32_t)strips[0].size());
    }
    else {
        wTag(f, 279, 4, (uint32_t)numStrips, (uint32_t)stripBytecountsPos);
    }
    w32(f, 0);

    if (numStrips > 1) {
        for (int s = 0; s < numStrips; s++) {
            w32(f, stripOffsets[s]);
        }
        for (int s = 0; s < numStrips; s++) {
            w32(f, (uint32_t)strips[s].size());
        }
    }
    for (int s = 0; s < numStrips; s++) {
        fwrite(strips[s].data(), 1, strips[s].size(), f);
    }
    fclose(f);
}

static const double T_PI = 3.14159265358979323846;

static std::vector<uint8_t> makeTextPage(double angleDeg, int W, int H) {
    std::vector<uint8_t> page;
    size_t total = static_cast<size_t>(W) * H;
    for (size_t i = 0; i < total; ++i) {
        page.push_back(255u);
    }

    int textRows[] = { H / 4, H / 2, (H * 3) / 4 };
    int startX = W / 10;
    int endX = (W * 9) / 10;

    for (int k = 0; k < 3; k++) {
        for (int tx = startX; tx < endX; tx++) {
            for (int dy = 0; dy < 10; dy++) {
                if (textRows[k] + dy < H) {
                    page[static_cast<size_t>((textRows[k] + dy) * W + tx)] = 0;
                }
            }
        }
    }

    if (angleDeg == 0.0) {
        return page;
    }

    double cx = W / 2.0, cy = H / 2.0;
    double a = angleDeg * T_PI / 180.0, ca = cos(a), sa = sin(a);
    std::vector<uint8_t> rotated;
    for (size_t i = 0; i < total; ++i) {
        rotated.push_back(255u);
    }

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            double dx = x - cx, dy2 = y - cy;
            int ix = (int)(dx * ca + dy2 * sa + cx + 0.5);
            int iy = (int)(-dx * sa + dy2 * ca + cy + 0.5);
            if (ix >= 0 && ix < W && iy >= 0 && iy < H) {
                rotated[static_cast<size_t>(y * W + x)] = page[static_cast<size_t>(iy * W + ix)];
            }
        }
    }
    return rotated;
}

static void test_tiff_intel_1strip() {
    log_to_file("tests/test_results.txt", "\n=== Test: TIFF Intel 1 Strip ===");
    const int W = 800, H = 600;
    std::vector<uint8_t> pixels;
    size_t total = static_cast<size_t>(W) * H;
    for (size_t i = 0; i < total; i++) {
        pixels.push_back((uint8_t)(i % 256));
    }
    writeSyntheticTIFF("tests/t1.tif", pixels, W, H, true, 0);

    Image img;
    bool ok = loadTIFF("tests/t1.tif", img);
    check(ok, "loaded");
    check(img.width == W, "width");
    check(img.height == H, "height");

    saveBMP("tests/out_tiff_intel_1strip_result.bmp", img);

    bool pixOk = true;
    for (size_t i = 0; i < total; i++) {
        if (img.pixels[i] != pixels[i]) {
            pixOk = false;
            break;
        }
    }
    check(pixOk, "pixels match");
}

static void test_tiff_intel_multistrip() {
    log_to_file("tests/test_results.txt", "\n=== Test: TIFF Intel Multi-Strip ===");
    const int W = 800, H = 600;
    std::vector<uint8_t> pixels;
    size_t total = static_cast<size_t>(W) * H;
    for (size_t i = 0; i < total; i++) {
        pixels.push_back((uint8_t)(i % 256));
    }
    writeSyntheticTIFF("tests/t2.tif", pixels, W, H, true, 50);

    Image img;
    bool ok = loadTIFF("tests/t2.tif", img);
    check(ok, "loaded");
    check(img.width == W, "width");
    check(img.height == H, "height");

    saveBMP("tests/out_tiff_intel_multistrip_result.bmp", img);

    bool pixOk = true;
    for (size_t i = 0; i < total; i++) {
        if (img.pixels[i] != pixels[i]) {
            pixOk = false;
            break;
        }
    }
    check(pixOk, "pixels match (multi-strip)");
}

static void test_tiff_motorola_1strip() {
    log_to_file("tests/test_results.txt", "\n=== Test: TIFF Motorola 1 Strip ===");
    const int W = 800, H = 600;
    std::vector<uint8_t> pixels;
    size_t total = static_cast<size_t>(W) * H;
    for (size_t i = 0; i < total; i++) {
        pixels.push_back((uint8_t)(255 - (i % 256)));
    }
    writeSyntheticTIFF("tests/t3.tif", pixels, W, H, false, 0);

    Image img;
    bool ok = loadTIFF("tests/t3.tif", img);
    check(ok, "loaded");
    check(img.width == W, "width (BE tag)");
    check(img.height == H, "height (BE tag)");

    saveBMP("tests/out_tiff_motorola_1strip_result.bmp", img);

    bool pixOk = true;
    for (size_t i = 0; i < total; i++) {
        if (img.pixels[i] != pixels[i]) {
            pixOk = false;
            break;
        }
    }
    check(pixOk, "pixels match (BE)");
}

static void test_tiff_motorola_multistrip() {
    log_to_file("tests/test_results.txt", "\n=== Test: TIFF Motorola Multi-Strip ===");
    const int W = 800, H = 600;
    std::vector<uint8_t> pixels;
    size_t total = static_cast<size_t>(W) * H;
    for (size_t i = 0; i < total; i++) {
        pixels.push_back((uint8_t)((i % 200) + 10));
    }
    writeSyntheticTIFF("tests/t4.tif", pixels, W, H, false, 50);

    Image img;
    bool ok = loadTIFF("tests/t4.tif", img);
    check(ok, "loaded");
    check(img.width == W, "width");
    check(img.height == H, "height");

    saveBMP("tests/out_tiff_motorola_multistrip_result.bmp", img);

    bool pixOk = true;
    for (size_t i = 0; i < total; i++) {
        if (img.pixels[i] != pixels[i]) {
            pixOk = false;
            break;
        }
    }
    check(pixOk, "pixels match (BE, multi-strip)");
}

static void test_otsu() {
    log_to_file("tests/test_results.txt", "\n=== Test: Otsu Threshold ===");
    Image img;
    img.width = 500;
    img.height = 500;
    img.pixels.clear();

    size_t total_pixels = static_cast<size_t>(img.width) * img.height;
    for (size_t i = 0; i < total_pixels / 2; ++i) {
        img.pixels.push_back(50);
    }
    for (size_t i = total_pixels / 2; i < total_pixels; ++i) {
        img.pixels.push_back(200);
    }

    uint8_t t = otsuThreshold(img);
    log_to_file("tests/test_results.txt", "Otsu threshold found: " + std::to_string(t));
    check(t >= 50 && t < 200, "threshold between two classes");

    Image binResult1 = applyThreshold(img, t);
    saveBMP("tests/out_otsu_result1.bmp", binResult1);

    Image bin;
    bin.width = 100;
    bin.height = 100;
    bin.pixels.clear();

    size_t total_bin = static_cast<size_t>(bin.width) * bin.height;
    for (size_t i = 0; i < total_bin / 2; ++i) {
        bin.pixels.push_back(0);
    }
    for (size_t i = total_bin / 2; i < total_bin; ++i) {
        bin.pixels.push_back(255);
    }

    uint8_t tb = otsuThreshold(bin);
    log_to_file("tests/test_results.txt", "Binary Otsu threshold found: " + std::to_string(tb));
    check(tb < 255, "threshold != 255 on binary image");

    Image binResult2 = applyThreshold(bin, tb);
    saveBMP("tests/out_otsu_result2.bmp", binResult2);
}

static void test_segmentation() {
    log_to_file("tests/test_results.txt", "\n=== Test: Segmentation (Large Object Removal) ===");
    const int W = 800, H = 800;
    Image img;
    img.width = W;
    img.height = H;
    img.pixels.clear();
    size_t total_pixels = static_cast<size_t>(W) * H;
    for (size_t i = 0; i < total_pixels; ++i) {
        img.pixels.push_back(255u);
    }

    int lyArr[3] = { 50, 100, 150 }, lxArr[3] = { 50, 50, 50 };
    for (int k = 0; k < 3; k++) {
        for (int y = lyArr[k]; y < lyArr[k] + 15; y++) {
            for (int x = lxArr[k]; x < lxArr[k] + 15; x++) {
                img.at(y, x) = 0;
            }
        }
    }

    for (int y = 400; y < 700; y++) {
        for (int x = 400; x < 700; x++) {
            img.at(y, x) = 0;
        }
    }

    uint8_t thresh = 128;
    Image result = removeNonTextObjects(img, thresh);
    saveBMP("tests/out_segmentation_result.bmp", result);

    int lettersRemain = 0;
    for (int k = 0; k < 3; k++) {
        for (int y = lyArr[k]; y < lyArr[k] + 15; y++) {
            for (int x = lxArr[k]; x < lxArr[k] + 15; x++) {
                if (result.at(y, x) <= thresh) {
                    lettersRemain++;
                }
            }
        }
    }

    int bigRemains = 0;
    for (int y = 500; y < 600; y++) {
        for (int x = 500; x < 600; x++) {
            if (result.at(y, x) <= thresh) {
                bigRemains++;
            }
        }
    }

    log_to_file("tests/test_results.txt", "Small letters preserved: " + std::to_string(lettersRemain) + " / 675");
    check(lettersRemain == 3 * 15 * 15, "small letters preserved");

    log_to_file("tests/test_results.txt", "Large block pixels remaining: " + std::to_string(bigRemains) + " (expected 0)");
    check(bigRemains == 0, "large block removed");
    check(result.width == W && result.height == H, "image size unchanged");
}

static void test_rotation() {
    log_to_file("tests/test_results.txt", "\n=== Test: Image Rotation ===");
    Image src;
    src.width = 500;
    src.height = 500;

    src.pixels.clear();
    size_t total_pixels = static_cast<size_t>(src.width) * src.height;
    for (size_t i = 0; i < total_pixels; ++i) {
        src.pixels.push_back(255u);
    }

    src.at(250, 250) = 0;

    Image rot0 = rotateImage(src, 0.0);
    Image rot360 = rotateImage(src, 360.0);

    saveBMP("tests/out_rotation_0_result.bmp", rot0);
    saveBMP("tests/out_rotation_360_result.bmp", rot360);

    check(rot0.at(250, 250) <= 10, "center pixel dark at 0 deg");
    check(rot360.at(250, 250) <= 10, "center pixel dark at 360 deg");
    check(rot0.width == src.width && rot0.height == src.height, "size preserved");

    bool bgOk = true;
    for (int y = 0; y < 500; ++y) {
        for (int x = 0; x < 500; ++x) {
            if (!(y == 250 && x == 250) && rot0.at(y, x) < 200) {
                bgOk = false;
                break;
            }
        }
        if (!bgOk) {
            break;
        }
    }
    check(bgOk, "background remains light");
}

static void test_bmp_roundtrip() {
    log_to_file("tests/test_results.txt", "\n=== Test: BMP Roundtrip ===");
    Image orig;
    orig.width = 500;
    orig.height = 500;
    orig.pixels.clear();

    size_t total_pixels = static_cast<size_t>(orig.width) * orig.height;
    for (size_t i = 0; i < total_pixels; ++i) {
        orig.pixels.push_back((uint8_t)(i % 256));
    }

    saveBMP("tests/out_bmp_write_result.bmp", orig);

    std::ifstream file("tests/out_bmp_write_result.bmp", std::ios::binary);
    check(file.is_open(), "BMP file created");
    if (!file.is_open()) {
        return;
    }

    file.seekg(0, std::ios::end);
    std::streamsize sz = file.tellg();
    file.seekg(0, std::ios::beg);

    int expected_size = 14 + 40 + 1024 + 250000;
    check((int)sz == expected_size, "file size correct for 500x500");

    uint8_t sig[2];
    file.read(reinterpret_cast<char*>(sig), 2);
    check(file.gcount() == 2 && sig[0] == 'B' && sig[1] == 'M', "BMP signature 'BM'");

    file.seekg(28, std::ios::beg);
    uint8_t bpp[2];
    file.read(reinterpret_cast<char*>(bpp), 2);

    uint16_t bitCount = 0;
    if (file.gcount() == 2) {
        bitCount = (uint16_t)(bpp[0] | (bpp[1] << 8));
    }

    check(bitCount == 8, "biBitCount = 8");
    file.close();
}

static void testAngle(const char* name, const char* path, double angleDeg,
    bool littleEndian, int rps, double expectedCorr) {
    log_to_file("tests/test_results.txt", "\n=== Test Angle: " + std::string(name) + " ===");
    const int W = 800, H = 600;
    std::vector<uint8_t> pix = makeTextPage(angleDeg, W, H);
    writeSyntheticTIFF(path, pix, W, H, littleEndian, rps);

    Image img;
    loadTIFF(path, img);

    uint8_t thresh = otsuThreshold(img);
    Image textOnly = removeNonTextObjects(img, thresh);

    double corr = -detectSkewAngle(textOnly, thresh);

    log_to_file("tests/test_results.txt", "Detected correction angle: " + std::to_string(corr) + " (expected: " + std::to_string(expectedCorr) + ")");
    checkNear(corr, expectedCorr, 0.5, "correction angle");

    Image deskewed = rotateImage(img, corr);

    std::string outPath = std::string(path);
    size_t pos = outPath.find(".tif");
    if (pos != std::string::npos) {
        outPath.replace(pos, 4, "_deskewed_result.bmp");
    }
    else {
        outPath += "_deskewed_result.bmp";
    }

    saveBMP(outPath.c_str(), deskewed);
}

static void test_full_pipeline_synthetic() {
    log_to_file("tests/test_results.txt", "\n=== Full Pipeline Synthetic Test ===");
    const int W = 800, H = 600;
    double true_angle = 3.5; 

    std::vector<uint8_t> pix = makeTextPage(true_angle, W, H);
    Image skewed;
    skewed.width = W;
    skewed.height = H;
    skewed.pixels.clear();
    size_t total = static_cast<size_t>(W) * H;
    for (size_t i = 0; i < total; ++i) {
        skewed.pixels.push_back(pix[i]);
    }
    saveBMP("tests/synthetic_skewed_result.bmp", skewed);

    uint8_t thresh = otsuThreshold(skewed);
    log_to_file("tests/test_results.txt", "Otsu threshold: " + std::to_string(thresh));

    Image textOnly = removeNonTextObjects(skewed, thresh);
    saveBMP("tests/synthetic_segmented_result.bmp", textOnly);

    double detected_angle = detectSkewAngle(textOnly, thresh);
    double corr = -detected_angle;
    log_to_file("tests/test_results.txt", "Detected correction angle: " + std::to_string(corr));
    checkNear(corr, -true_angle, 0.5, "correction angle matches true skew");

    Image deskewed = rotateImage(skewed, corr);
    saveBMP("tests/synthetic_deskewed_result.bmp", deskewed);

    std::vector<long> pBefore(H, 0), pAfter(H, 0);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            if (skewed.at(y, x) <= thresh) pBefore[y]++;
            if (deskewed.at(y, x) <= thresh) pAfter[y]++;
        }
    }

    double eBefore = 0, eAfter = 0;
    for (int y = 0; y < H; y++) {
        eBefore += (double)pBefore[y] * pBefore[y];
        eAfter += (double)pAfter[y] * pAfter[y];
    }

    log_to_file("tests/test_results.txt", "Projection Energy Before: " + std::to_string(eBefore));
    log_to_file("tests/test_results.txt", "Projection Energy After: " + std::to_string(eAfter));

    double improvement = (eAfter - eBefore) / eBefore * 100.0;
    log_to_file("tests/test_results.txt", "Energy improvement: +" + std::to_string(improvement) + "%");

    check(eAfter > eBefore, "projection energy increased after correction");
}

int runAllTests() {
    g_passed = 0;
    g_failed = 0;
    std::ofstream clear_log("tests/test_results.txt", std::ios::trunc);
    clear_log.close();

    test_tiff_intel_1strip();
    test_tiff_intel_multistrip();
    test_tiff_motorola_1strip();
    test_tiff_motorola_multistrip();

    test_otsu();
    test_segmentation();
    test_rotation();
    test_bmp_roundtrip();

    testAngle("Test 9:  LE 1strip  +2deg", "tests/t9.tif", 2.0, true, 0, -2.0);
    testAngle("Test 10: LE multi   -3deg", "tests/t10.tif", -3.0, true, 60, 3.0);
    testAngle("Test 11: BE 1strip  +5deg", "tests/t11.tif", 5.0, false, 0, -5.0);
    testAngle("Test 12: BE multi  -1.5deg", "tests/t12.tif", -1.5, false, 50, 1.5);
    testAngle("Test 13: no skew    0deg", "tests/t13.tif", 0.0, true, 0, 0.0);
    testAngle("Test 14: large     +8deg", "tests/t14.tif", 8.0, true, 0, -8.0);

    test_full_pipeline_synthetic();

    return g_failed;
}