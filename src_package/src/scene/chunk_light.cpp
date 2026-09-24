#include "chunk_internal.h"
#include <algorithm>

using namespace ChunkInternal;

const Chunk* Chunk::chunkAt(int dx, int dz) const {
    if (dx == 0 && dz == 0) return this;
    if (dz == 0) return getNeighbor(dx > 0 ? XPOS : XNEG);
    if (dx == 0) return getNeighbor(dz > 0 ? ZPOS : ZNEG);
    // Diagonal: try both two-step paths, since either link may be missing
    if (const Chunk *a = getNeighbor(dx > 0 ? XPOS : XNEG)) {
        if (const Chunk *d = a->getNeighbor(dz > 0 ? ZPOS : ZNEG)) return d;
    }
    if (const Chunk *b = getNeighbor(dz > 0 ? ZPOS : ZNEG)) {
        return b->getNeighbor(dx > 0 ? XPOS : XNEG);
    }
    return nullptr;
}

void Chunk::buildLightGrid(std::vector<unsigned char> &cells,
                           std::vector<unsigned char> &skyLight,
                           std::vector<unsigned char> &blockLight) const {
    cells.assign(LG * LG * LH, EMPTY);
    skyLight.assign(LG * LG * LH, 0);
    blockLight.assign(LG * LG * LH, 0);

    // Copy block data from this chunk and its 8 lateral neighbors.
    // Missing neighbors read as air; their chunks re-mesh once they exist.
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            const Chunk *c = chunkAt(dx, dz);
            if (!c) continue;
            for (int lz = 0; lz < 16; ++lz) {
                for (int lx = 0; lx < 16; ++lx) {
                    int gx = dx * 16 + 16 + lx;
                    int gz = dz * 16 + 16 + lz;
                    for (int y = 0; y < LH; ++y) {
                        cells[lidx(gx, y, gz)] =
                            static_cast<unsigned char>(c->getLocalBlockAt(lx, y, lz));
                    }
                }
            }
        }
    }

    // Sky light: full sunlight falls straight down until blocked, dimming
    // through water and leaves, then spreads sideways into caves via BFS
    std::deque<int> queue;
    for (int gz = 0; gz < LG; ++gz) {
        for (int gx = 0; gx < LG; ++gx) {
            int l = 15;
            for (int y = LH - 1; y >= 0 && l > 0; --y) {
                int i = lidx(gx, y, gz);
                int cost = lightCost(static_cast<BlockType>(cells[i]));
                if (cost == 0) break;
                if (cost > 1) l = std::max(0, l - cost);
                skyLight[i] = static_cast<unsigned char>(l);
                if (l > 1) queue.push_back(i);
            }
        }
    }
    propagateLight(cells, skyLight, queue);

    // Block light emitters: lava, torches, lit lamps and powered redstone
    queue.clear();
    for (int i = 0; i < LG * LG * LH; ++i) {
        int glow = 0; // ("emit" is a Qt macro)
        switch (cells[i]) {
        case LAVA:     glow = 15; break;
        case TORCH:    glow = 14; break;
        case LAMP_ON:  glow = 15; break;
        case WIRE_ON:  glow = 7;  break;
        case LEVER_ON: glow = 9;  break;
        default: break;
        }
        if (glow > 0) {
            blockLight[i] = static_cast<unsigned char>(glow);
            queue.push_back(i);
        }
    }
    propagateLight(cells, blockLight, queue);
}

