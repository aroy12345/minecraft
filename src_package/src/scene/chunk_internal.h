#pragma once
#include "chunk.h"
#include <deque>

// Internals shared by the chunk mesher (chunk.cpp) and the light engine
// (chunk_light.cpp).

namespace ChunkInternal {
// The light grid spans this chunk plus a 16-block border from each lateral
// neighbor (3x3 chunks = 48x48 columns), so light and ambient occlusion are
// seamless across chunk boundaries.
constexpr int LG = 48;
constexpr int LH = 256;
inline int lidx(int x, int y, int z) { return (z * LG + x) * LH + y; }

// Cost of light passing into a block; 0 means opaque to light. Water and
// leaves let light through but dim it, like Minecraft.
inline int lightCost(BlockType t) {
    switch (t) {
    case EMPTY: return 1;
    case WATER: return 3;
    case LEAF:  return 2;
    default:    return 0;
    }
}

inline bool solidForAO(BlockType t) {
    // Lava counts as open space here so pool surfaces get no dark rim and
    // the walls around a pool sample its full glow
    return t != EMPTY && t != WATER && t != LAVA;
}

// Breadth-first light propagation: light spreads to transparent neighbors,
// losing that block's attenuation per step, exactly like Minecraft's 0-15
// light levels.
inline void propagateLight(const std::vector<unsigned char> &cells,
                    std::vector<unsigned char> &light,
                    std::deque<int> &queue) {
    while (!queue.empty()) {
        int i = queue.front();
        queue.pop_front();
        int l = light[i];
        if (l <= 1) continue;
        int y = i % LH;
        int rest = i / LH;
        int x = rest % LG;
        int z = rest / LG;
        const int nx[6] = {x + 1, x - 1, x, x, x, x};
        const int ny[6] = {y, y, y + 1, y - 1, y, y};
        const int nz[6] = {z, z, z, z, z + 1, z - 1};
        for (int k = 0; k < 6; ++k) {
            if (nx[k] < 0 || nx[k] >= LG || ny[k] < 0 || ny[k] >= LH ||
                nz[k] < 0 || nz[k] >= LG) continue;
            int ni = lidx(nx[k], ny[k], nz[k]);
            int cost = lightCost(static_cast<BlockType>(cells[ni]));
            if (cost == 0) continue;
            int cand = l - cost;
            if (cand > light[ni]) {
                light[ni] = static_cast<unsigned char>(cand);
                queue.push_back(ni);
            }
        }
    }
}
}
