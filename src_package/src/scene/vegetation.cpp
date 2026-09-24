#include "terrain.h"
#include <cmath>

// Vegetation pass: procedural trees (a distinct species per biome), rock
// boulders, and low ground shrubs. Every feature is a pure function of world
// (x, z), so each chunk independently rebuilds the same vegetation its
// neighbours see - canopies span chunk borders seamlessly with no shared
// state. Species and placement are chosen so each biome reads at a glance.

namespace {

// A writer bound to one chunk; builders think in absolute world coordinates.
struct Stamp {
    Chunk *chunk;
    int cx, cz;
    void put(int wx, int wy, int wz, BlockType t, bool onlyIfEmpty) const {
        int lx = wx - cx, lz = wz - cz;
        if (lx < 0 || lx >= 16 || lz < 0 || lz >= 16 ||
            wy < 0 || wy > WorldGen::MAX_HEIGHT) {
            return;
        }
        if (onlyIfEmpty && chunk->getLocalBlockAt(lx, wy, lz) != EMPTY) return;
        chunk->setLocalBlockAt(lx, wy, lz, t);
    }
};

inline float h01(int a, int b, int salt) { return NoiseGenerator::hash01(a, b, salt); }

// Broad rounded oak: two rounded leaf layers, a 3x3, and a plus-shaped cap.
// `big` oaks widen to radius 3 for the occasional canopy giant.
void buildOak(const Stamp &s, int tx, int tz, int base, int trunkH, bool big) {
    int top = base + trunkH;
    int r = big ? 3 : 2;
    float rr = (r + 0.5f) * (r + 0.5f);
    for (int dx = -r; dx <= r; ++dx)
        for (int dz = -r; dz <= r; ++dz) {
            if (dx * dx + dz * dz > rr) continue;      // round off the corners
            s.put(tx + dx, top - 1, tz + dz, LEAF, true);
            s.put(tx + dx, top, tz + dz, LEAF, true);
        }
    for (int dx = -1; dx <= 1; ++dx)
        for (int dz = -1; dz <= 1; ++dz)
            s.put(tx + dx, top + 1, tz + dz, LEAF, true);
    s.put(tx, top + 2, tz, LEAF, true);
    s.put(tx + 1, top + 2, tz, LEAF, true);
    s.put(tx - 1, top + 2, tz, LEAF, true);
    s.put(tx, top + 2, tz + 1, LEAF, true);
    s.put(tx, top + 2, tz - 1, LEAF, true);
    for (int y = base + 1; y <= top; ++y) s.put(tx, y, tz, WOOD, false);
}

// Tall snow-dusted conifer: diamond skirts stacked up a straight trunk, a
// leaf spire, and a snow cap - the signature tree of the snowfields.
void buildConifer(const Stamp &s, int tx, int tz, int base, int trunkH) {
    int th = trunkH + 3;                               // conifers stand taller
    int top = base + th;
    for (int layer = 0; layer < 4; ++layer) {
        int ly = top - 1 - layer * 2;
        int r = layer;                                 // widest skirt at the base
        for (int dx = -r; dx <= r; ++dx)
            for (int dz = -r; dz <= r; ++dz)
                if (std::abs(dx) + std::abs(dz) <= r)
                    s.put(tx + dx, ly, tz + dz, LEAF, true);
    }
    s.put(tx, top, tz, LEAF, true);                    // spire
    s.put(tx, top + 1, tz, SNOW, true);                // snow cap
    for (int y = base + 1; y <= top - 1; ++y) s.put(tx, y, tz, WOOD, false);
}

// Low bush: a stubby stem under a small leaf mound.
void buildBush(const Stamp &s, int tx, int tz, int base) {
    s.put(tx, base + 1, tz, WOOD, true);
    for (int dx = -1; dx <= 1; ++dx)
        for (int dz = -1; dz <= 1; ++dz)
            s.put(tx + dx, base + 2, tz + dz, LEAF, true);
    s.put(tx, base + 3, tz, LEAF, true);
}

// A small rounded boulder with a coal seam, for rocky ground detail.
void buildBoulder(const Stamp &s, int bx, int bz, int by) {
    for (int dx = -1; dx <= 1; ++dx)
        for (int dz = -1; dz <= 1; ++dz) {
            s.put(bx + dx, by + 1, bz + dz, STONE, true);
            if (std::abs(dx) + std::abs(dz) <= 1)
                s.put(bx + dx, by + 2, bz + dz, STONE, true);
        }
    s.put(bx, by + 1, bz, COAL_ORE, false);
}

} // namespace

void Terrain::stampTrees(Chunk *chunk) {
    constexpr int R = BiomeGenerator::TREE_CANOPY_RADIUS + 1;   // widest canopy
    Stamp s{chunk, chunk->getMinX(), chunk->getMinZ()};

    // --- Trees: a biome-specific species, occasional giants and bushes ---
    for (int tx = s.cx - R; tx < s.cx + 16 + R; ++tx) {
        for (int tz = s.cz - R; tz < s.cz + 16 + R; ++tz) {
            if (!m_biomeGenerator.couldHaveTree(tx, tz)) continue;
            ColumnInfo info = m_biomeGenerator.getColumnInfo(tx, tz);
            if (!m_biomeGenerator.hasTreeAt(tx, tz, info)) continue;
            if (m_biomeGenerator.isCave(tx, info.height, tz)) continue;
            int trunkH = m_biomeGenerator.getTreeTrunkHeight(tx, tz);
            float kind = h01(tx * 7 + 1, tz * 7 + 3, 4242);
            if (info.dominant == Biome::SNOWFIELD) {
                buildConifer(s, tx, tz, info.height, trunkH);
            } else if (kind < 0.12f) {
                buildBush(s, tx, tz, info.height);
            } else {
                buildOak(s, tx, tz, info.height, trunkH, kind > 0.82f);
            }
        }
    }

    // --- Boulders: scattered rock outcrops on non-desert ground ---
    for (int bx = s.cx - 2; bx < s.cx + 16 + 2; ++bx) {
        for (int bz = s.cz - 2; bz < s.cz + 16 + 2; ++bz) {
            if (h01(bx, bz, 0xB07 + 12345) > 0.0038f) continue;
            ColumnInfo info = m_biomeGenerator.getColumnInfo(bx, bz);
            if (info.dominant == Biome::DESERT) continue;
            if (info.height <= WorldGen::WATER_LEVEL) continue;
            if (m_biomeGenerator.isCave(bx, info.height, bz)) continue;
            buildBoulder(s, bx, bz, info.height);
        }
    }

    // --- Ground shrubs: leaf tufts sprinkled across grassland floors ---
    for (int gx = s.cx - 1; gx < s.cx + 16 + 1; ++gx) {
        for (int gz = s.cz - 1; gz < s.cz + 16 + 1; ++gz) {
            if (h01(gx, gz, 0x5b + 999) > 0.014f) continue;
            ColumnInfo info = m_biomeGenerator.getColumnInfo(gx, gz);
            if (info.dominant != Biome::GRASSLAND) continue;
            if (info.height <= WorldGen::WATER_LEVEL + 1) continue;
            if (m_biomeGenerator.isCave(gx, info.height, gz)) continue;
            s.put(gx, info.height + 1, gz, LEAF, true);
        }
    }
}
