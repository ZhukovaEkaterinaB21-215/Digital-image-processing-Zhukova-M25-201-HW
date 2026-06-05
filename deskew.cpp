#include "deskew.h"

#include <cmath>
#include <cstdio>
#include <vector>
#include <algorithm>

static const double PI = 3.14159265358979323846;

uint8_t otsuThreshold(const Image& img) {
    long hist[256] = {};
    for (size_t i = 0; i < img.pixels.size(); i++) {
        hist[img.pixels[i]]++;
    }

    long total = (long)img.pixels.size();
    long sum = 0;
    for (int i = 0; i < 256; i++) {
        sum += (long)i * hist[i];
    }

    long sumB = 0, wB = 0;
    double varMax = 0.0;
    uint8_t thresh = 0;

    for (int t = 0; t < 256; t++) {
        wB += hist[t];
        if (wB == 0) {
            continue;
        }
        long wF = total - wB;
        if (wF == 0) {
            break;
        }
        sumB += (long)t * hist[t];
        double mB = (double)sumB / (double)wB;
        double mF = (double)(sum - sumB) / (double)wF;
        double var = (double)wB * (double)wF * (mB - mF) * (mB - mF);
        if (var > varMax) { 
            varMax = var; 
            thresh = (uint8_t)t; 
        }
    }

    printf("Otsu threshold: %d\n", (int)thresh);
    return thresh;
}

struct UnionFind {
    std::vector<int> parent, rnk;
    explicit UnionFind(int n) : parent(n), rnk(n, 0) {
        for (int i = 0; i < n; i++) {
            parent[i] = i;
        }
    }
    int find(int x) {
        while (parent[x] != x) { 
            parent[x] = parent[parent[x]]; 
            x = parent[x]; 
        }
        return x;
    }
    void unite(int a, int b) {
        a = find(a); 
        b = find(b);
        if (a == b) {
            return;
        }
        if (rnk[a] < rnk[b]) { 
            int t = a; 
            a = b; 
            b = t; 
        }
        parent[b] = a;
        if (rnk[a] == rnk[b]) {
            rnk[a]++;
        }
    }
};

struct ComponentInfo {
    int minX, maxX, minY, maxY, area;
};

static std::vector<int> connectedComponents(const Image& bin, uint8_t thresh,
    std::vector<ComponentInfo>& comps) {
    int W = bin.width, H = bin.height;
    std::vector<int> label((size_t)(W * H), -1);
    UnionFind uf(W * H);

    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            if (bin.at(y, x) > thresh) {
                continue;
            }
            int id = y * W + x;
            label[id] = id;
            if (x > 0 && label[y * W + x - 1] >= 0) {
                uf.unite(id, y * W + x - 1);
            }
            if (y > 0 && label[(y - 1) * W + x] >= 0) {
                uf.unite(id, (y - 1) * W + x);
            }
        }

    std::vector<int> rootToIndex((size_t)(W * H), -1);
    int numComps = 0;
    for (int i = 0; i < W * H; i++) {
        if (label[i] < 0) {
            continue;
        }
        int r = uf.find(i);
        if (rootToIndex[r] < 0) {
            rootToIndex[r] = numComps++;
        }
        label[i] = rootToIndex[r];
    }

    printf("Components: %d\n", numComps);

    comps.assign((size_t)numComps, { 1 << 30, 0, 1 << 30, 0, 0 });
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            int id = y * W + x;
            if (label[id] < 0) {
                continue;
            }
            ComponentInfo& c = comps[label[id]];
            c.area++;
            if (x < c.minX) {
                c.minX = x;
            }
            if (x > c.maxX) {
                c.maxX = x;
            }
            if (y < c.minY) {
                c.minY = y;
            }
            if (y > c.maxY) {
                c.maxY = y;
            }
        }

    return label;
}
void custom_sort(std::vector<int>& arr, int count) {
    for (int i = 1; i < count; ++i) {
        int key = arr[i];
        int j = i - 1;

        while (j >= 0 && arr[j] > key) {
            arr[j + 1] = arr[j];
            j = j - 1;
        }
        arr[j + 1] = key;
    }
}

Image removeNonTextObjects(const Image& bin, uint8_t thresh) {
    std::vector<ComponentInfo> comps;
    std::vector<int> labels = connectedComponents(bin, thresh, comps);
    int W = bin.width, H = bin.height;

    std::vector<int> areas;
    int comp_count = 0;
    for (auto it = comps.begin(); it != comps.end(); ++it) {
        comp_count++;
    }

    areas.clear();
    for (int i = 0; i < comp_count; ++i) {
        areas.push_back(0);
    }

    for (size_t i = 0; i < comps.size(); i++) {
        areas.push_back(comps[i].area);
    }
    custom_sort(areas, areas.size());

    int totalComps = (int)areas.size();
    int pct95 = (totalComps > 0) ? areas[(int)(totalComps * 0.95)] : 99999;
    int areaThresh = (pct95 > 5000) ? pct95 : 5000;

    std::vector<bool> doRemove(comps.size(), false);
    for (size_t i = 0; i < comps.size(); i++) {
        const ComponentInfo& c = comps[i];
        int bbW = c.maxX - c.minX + 1;
        int bbH = c.maxY - c.minY + 1;
        if (c.area > areaThresh) {
            doRemove[i] = true;
        }
        if (bbW > 200 && bbH > 200 && c.area > 3000) {
            doRemove[i] = true;
        }
    }

    Image out = bin;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            int id = y * W + x;
            if (labels[id] >= 0 && doRemove[(size_t)labels[id]]) {
                out.at(y, x) = 255u;
            }
        }
    return out;
}

static double projectionEnergy(const Image& bin, uint8_t thresh, double angleDeg) {
    int W = bin.width, H = bin.height;
    double angle = angleDeg * PI / 180.0;
    double cosA = cos(angle), sinA = sin(angle);
    double cx = W / 2.0, cy = H / 2.0;

    int outH = H + (int)(fabs(sinA) * W) + 2;
    std::vector<long> projection((size_t)(outH + 2), 0L);

    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            if (bin.at(y, x) > thresh) {
                continue;
            }
            double dx = (double)x - cx, dy = (double)y - cy;
            int row = (int)(-dx * sinA + dy * cosA + cy);
            if (row >= 0 && row < outH) {
                projection[row]++;
            }
        }

    double energy = 0.0;
    for (int i = 0; i < outH; i++) {
        energy += (double)projection[i] * (double)projection[i];
    }
    return energy;
}

static double ternarySearchMax(const Image& bin, uint8_t thresh,
    double lo, double hi, int iterations) {
    for (int i = 0; i < iterations; i++) {
        double m1 = lo + (hi - lo) / 3.0;
        double m2 = hi - (hi - lo) / 3.0;
        if (projectionEnergy(bin, thresh, m1) < projectionEnergy(bin, thresh, m2)) {
            lo = m1;
        }
        else {
            hi = m2;
        }
    }
    return (lo + hi) / 2.0;
}

double detectSkewAngle(const Image& bin, uint8_t thresh) {

    double bestAngle = 0.0, bestEnergy = -1.0;
    const double step = 0.5;
    for (double a = -15.0; a <= 15.0; a += step) {
        double e = projectionEnergy(bin, thresh, a);
        if (e > bestEnergy) { 
            bestEnergy = e; 
            bestAngle = a; 
        }
    }
    printf("Coarse: %.2f deg\n", bestAngle);

    double fineAngle = ternarySearchMax(bin, thresh, bestAngle - step, bestAngle + step, 60);
    printf("Fine  : %.4f deg\n", fineAngle);
    return fineAngle;
}

Image rotateImage(const Image& src, double angleDeg) {
    int W = src.width, H = src.height;
    double angle = angleDeg * PI / 180.0;
    double cosA = cos(angle), sinA = sin(angle);
    double cx = W / 2.0, cy = H / 2.0;

    Image dst;
    dst.width = W;
    dst.height = H;
    dst.pixels.clear();
    int total_pixels = W * H;
    for (int i = 0; i < total_pixels; ++i) {
        dst.pixels.push_back(255u);
    }

    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            double dx = (double)x - cx, dy = (double)y - cy;
            double sx = dx * cosA + dy * sinA + cx;
            double sy = -dx * sinA + dy * cosA + cy;

            int x0 = (int)floor(sx), x1 = x0 + 1;
            int y0 = (int)floor(sy), y1 = y0 + 1;
            double tx = sx - (double)x0, ty = sy - (double)y0;

            auto safeAt = [&](int r, int c) -> double {
                if (r < 0 || r >= H || c < 0 || c >= W) {
                    return 255.0;
                }
                return (double)src.at(r, c);
            };

            double val = (1.0 - tx) * (1.0 - ty) * safeAt(y0, x0) + tx * (1.0 - ty) * safeAt(y0, x1)
                + (1.0 - tx) * ty * safeAt(y1, x0) + tx * ty * safeAt(y1, x1);
            dst.at(y, x) = (uint8_t)(val + 0.5);
        }
    return dst;
}
