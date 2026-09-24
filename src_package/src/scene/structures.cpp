#include "terrain.h"
#include <cmath>

// Procedural landmarks (Milestone 3, extended): each biome grows its own
// family of hand-authored builds, placed sparsely and deterministically so
// exploration keeps turning up something new. Every builder writes in world
// space through a Stamp bound to the chunk currently being generated; blocks
// outside the chunk are clipped, so builds span chunk borders seamlessly.

namespace {

// A writer bound to one chunk. Builders think in absolute world coordinates
// and let the Stamp clip and localize each block.
struct Stamp {
    Chunk *chunk;
    int cx, cz;
    void put(int wx, int wy, int wz, BlockType t) const {
        int lx = wx - cx, lz = wz - cz;
        if (lx < 0 || lx >= 16 || lz < 0 || lz >= 16 ||
            wy < 1 || wy > WorldGen::MAX_HEIGHT) {
            return;
        }
        chunk->setLocalBlockAt(lx, wy, lz, t);
    }
};

inline float h01(int a, int b, int salt) {
    return NoiseGenerator::hash01(a, b, salt + 12345);
}

// --- Desert: a sandstone pyramid over a hollow burial chamber ----------------
void buildPyramid(const Stamp &s, int ax, int az, int base, float pick) {
    int half = 6 + static_cast<int>(pick * 4.f);
    for (int lvl = 0; lvl <= half; ++lvl) {
        int w = half - lvl;
        for (int dx = -w; dx <= w; ++dx)
            for (int dz = -w; dz <= w; ++dz)
                s.put(ax + dx, base + 1 + lvl, az + dz, SAND);
    }
    for (int dx = -2; dx <= 2; ++dx)          // chamber
        for (int dz = -2; dz <= 2; ++dz)
            for (int dy = 1; dy <= 3; ++dy)
                s.put(ax + dx, base + dy, az + dz, EMPTY);
    for (int dx = 2; dx <= half + 1; ++dx)    // corridor out to the east
        for (int dy = 1; dy <= 2; ++dy)
            s.put(ax + dx, base + dy, az, EMPTY);
    s.put(ax, base + 3, az, LAMP_ON);         // a glow deep in the tomb
}

// --- Mountains: a battlemented brick watchtower with a lit beacon -----------
void buildWatchtower(const Stamp &s, int ax, int az, int base, float pick) {
    int hgt = 9 + static_cast<int>(pick * 5.f);
    int w = 2;
    for (int dx = -w; dx <= w; ++dx)
        for (int dz = -w; dz <= w; ++dz)
            s.put(ax + dx, base, az + dz, STONE);
    for (int lvl = 1; lvl <= hgt; ++lvl) {
        for (int dx = -w; dx <= w; ++dx) {
            for (int dz = -w; dz <= w; ++dz) {
                bool edge = (std::abs(dx) == w || std::abs(dz) == w);
                if (!edge) continue;
                bool window = (lvl % 3 == 0) && (dx == 0 || dz == 0);
                s.put(ax + dx, base + lvl, az + dz, window ? EMPTY : BRICK);
            }
        }
    }
    for (int dx = -w + 1; dx <= w - 1; ++dx)  // top platform
        for (int dz = -w + 1; dz <= w - 1; ++dz)
            s.put(ax + dx, base + hgt, az + dz, PLANK);
    for (int dx = -w; dx <= w; ++dx)          // crenellations
        for (int dz = -w; dz <= w; ++dz) {
            bool edge = (std::abs(dx) == w || std::abs(dz) == w);
            if (edge && ((dx + dz) & 1) == 0)
                s.put(ax + dx, base + hgt + 1, az + dz, BRICK);
        }
    s.put(ax, base + hgt + 1, az, LAMP_ON);   // beacon
}

// --- Grassland: a standing-stone monument on a stone dais --------------------
void buildMonument(const Stamp &s, int ax, int az, int base) {
    for (int dx = -6; dx <= 6; ++dx)          // dais
        for (int dz = -6; dz <= 6; ++dz)
            if (std::abs(dx) + std::abs(dz) <= 8)
                s.put(ax + dx, base, az + dz, STONE);
    int oh = 7;                               // tapered central obelisk
    for (int lvl = 1; lvl <= oh; ++lvl) {
        int ow = (lvl <= oh - 2) ? 1 : 0;
        for (int dx = -ow; dx <= ow; ++dx)
            for (int dz = -ow; dz <= ow; ++dz)
                s.put(ax + dx, base + lvl, az + dz, BRICK);
    }
    s.put(ax, base + oh + 1, az, LAMP_ON);
    const int ring[8][2] = {{5,0},{-5,0},{0,5},{0,-5},{4,4},{4,-4},{-4,4},{-4,-4}};
    for (const auto &pr : ring) {
        for (int lvl = 1; lvl <= 4; ++lvl)
            s.put(ax + pr[0], base + lvl, az + pr[1], BRICK);
        s.put(ax + pr[0], base + 5, az + pr[1], STONE);   // capstone
    }
}

// --- Grassland / snowfield: a shape-grammar plank-and-log tower --------------
//   Start -> Foundation(w) Body(w,floors); Body -> Walls(w) [Body(w-2)|Roof];
//   Walls -> plank ring + log corners + windows (+ a door on floor one).
void buildTower(const Stamp &s, int ax, int az, int base, float pick) {
    int w = 3;
    int floors = 2 + static_cast<int>(pick * 2.f);
    int y = base;
    for (int dx = -w; dx <= w; ++dx)          // foundation slab
        for (int dz = -w; dz <= w; ++dz)
            s.put(ax + dx, y, az + dz, PLANK);
    for (int f = 0; f < floors; ++f) {
        int fw = (f >= 2) ? w - 1 : w;        // setback on the upper floors
        for (int lvl = 1; lvl <= 4; ++lvl) {
            for (int dx = -fw; dx <= fw; ++dx) {
                for (int dz = -fw; dz <= fw; ++dz) {
                    bool edge = (std::abs(dx) == fw || std::abs(dz) == fw);
                    bool corner = (std::abs(dx) == fw && std::abs(dz) == fw);
                    if (!edge) {
                        if (lvl == 4) s.put(ax + dx, y + lvl, az + dz, PLANK);
                        continue;
                    }
                    BlockType b = corner ? WOOD : PLANK;
                    if (!corner && lvl >= 2 && lvl <= 3) {
                        bool door = (f == 0 && dx == fw && dz == 0);
                        bool window = ((dx + dz) % 3 == 0) && lvl == 3;
                        if (door || window) b = EMPTY;
                    }
                    s.put(ax + dx, y + lvl, az + dz, b);
                }
            }
        }
        y += 4;
    }
    for (int lvl = 0; lvl <= w + 1; ++lvl) {  // stepped wooden roof
        int rw = w + 1 - lvl;
        if (rw < 0) break;
        for (int dx = -rw; dx <= rw; ++dx)
            for (int dz = -rw; dz <= rw; ++dz)
                s.put(ax + dx, y + 1 + lvl, az + dz, WOOD);
    }
}

// --- Grassland: a hamlet of cottages around a stone well --------------------
void buildVillage(const Stamp &s, int ax, int az, int base, float /*pick*/) {
    const int spots[3][2] = {{-7, -2}, {6, -4}, {2, 6}};
    for (const auto &sp : spots) {
        int hx = ax + sp[0], hz = az + sp[1], w = 2;
        for (int dx = -w; dx <= w; ++dx)      // packed-plank floor
            for (int dz = -w; dz <= w; ++dz)
                s.put(hx + dx, base, hz + dz, PLANK);
        for (int lvl = 1; lvl <= 3; ++lvl)    // walls with door and windows
            for (int dx = -w; dx <= w; ++dx)
                for (int dz = -w; dz <= w; ++dz) {
                    bool edge = (std::abs(dx) == w || std::abs(dz) == w);
                    if (!edge) continue;
                    bool corner = (std::abs(dx) == w && std::abs(dz) == w);
                    BlockType b = corner ? WOOD : PLANK;
                    if (!corner && lvl == 2 && (dx == 0 || dz == 0)) b = EMPTY;
                    s.put(hx + dx, base + lvl, hz + dz, b);
                }
        for (int dx = -w - 1; dx <= w + 1; ++dx)   // overhanging roof
            for (int dz = -w - 1; dz <= w + 1; ++dz)
                s.put(hx + dx, base + 4, hz + dz, WOOD);
        s.put(hx, base + 5, hz, WOOD);             // ridge
        s.put(hx + w + 1, base + 2, hz, TORCH);    // porch light
    }
    for (int dx = -1; dx <= 1; ++dx)          // central well
        for (int dz = -1; dz <= 1; ++dz) {
            bool rim = (std::abs(dx) == 1 || std::abs(dz) == 1);
            s.put(ax + dx, base, az + dz, rim ? STONE : WATER);
            if (rim) s.put(ax + dx, base + 1, az + dz, STONE);
        }
    s.put(ax, base + 3, az, WOOD);
    s.put(ax, base + 4, az, TORCH);
}

// --- Grassland / desert: weathered ruins of an ancient hall -----------------
void buildRuins(const Stamp &s, int ax, int az, int base, float /*pick*/) {
    for (int dx = -6; dx <= 6; ++dx)          // cracked flagstone court
        for (int dz = -6; dz <= 6; ++dz)
            if (std::abs(dx) + std::abs(dz) <= 8 && h01(ax + dx, az + dz, 99) > 0.22f)
                s.put(ax + dx, base, az + dz, STONE);
    const int cols[6][2] = {{-5,-3},{-4,4},{0,5},{4,-4},{5,2},{1,-5}};
    for (const auto &cpos : cols) {
        int hgt = 2 + static_cast<int>(h01(cols[0][0] + cpos[0], cpos[1], 7) * 5.f);
        for (int lvl = 1; lvl <= hgt; ++lvl)
            s.put(ax + cpos[0], base + lvl, az + cpos[1], BRICK);
        s.put(ax + cpos[0] + 1, base + 1, az + cpos[1], BRICK);   // toppled block
    }
    for (int lvl = 1; lvl <= 4; ++lvl) {      // a lone surviving archway
        s.put(ax - 2, base + lvl, az, BRICK);
        s.put(ax + 2, base + lvl, az, BRICK);
    }
    for (int dx = -2; dx <= 2; ++dx)
        s.put(ax + dx, base + 5, az, BRICK);
    s.put(ax, base + 1, az, LAMP_ON);         // a relic still glowing
}

} // namespace

void Terrain::stampStructures(Chunk *chunk) {
    constexpr int REGION = 112;               // one landmark per ~112 blocks
    Stamp s{chunk, chunk->getMinX(), chunk->getMinZ()};

    int r0x = static_cast<int>(glm::floor((s.cx - 24) / float(REGION)));
    int r1x = static_cast<int>(glm::floor((s.cx + 40) / float(REGION)));
    int r0z = static_cast<int>(glm::floor((s.cz - 24) / float(REGION)));
    int r1z = static_cast<int>(glm::floor((s.cz + 40) / float(REGION)));

    for (int rx = r0x; rx <= r1x; ++rx) {
        for (int rz = r0z; rz <= r1z; ++rz) {
            if (h01(rx * 3 + 11, rz * 5 + 7, 0x51ee) > 0.62f) continue;
            int ax = rx * REGION + 24 + static_cast<int>(h01(rx, rz * 9 + 1, 0) * (REGION - 48));
            int az = rz * REGION + 24 + static_cast<int>(h01(rx * 9 + 2, rz, 0) * (REGION - 48));
            if (ax < s.cx - 22 || ax > s.cx + 16 + 22 ||
                az < s.cz - 22 || az > s.cz + 16 + 22) continue;

            ColumnInfo c = m_biomeGenerator.getColumnInfo(ax, az);
            if (c.height <= WorldGen::WATER_LEVEL + 1) continue;
            int hmin = c.height, hmax = c.height;   // require a fairly flat pad
            for (int d = -5; d <= 5; d += 5)
                for (int e = -5; e <= 5; e += 5) {
                    int h = m_biomeGenerator.getColumnInfo(ax + d, az + e).height;
                    hmin = std::min(hmin, h);
                    hmax = std::max(hmax, h);
                }
            if (hmax - hmin > 4) continue;
            float pick = h01(ax, az, 777);
            int base = hmax;

            switch (c.dominant) {
            case Biome::DESERT:
                if (pick < 0.6f) buildPyramid(s, ax, az, base, pick);
                else             buildRuins(s, ax, az, base, pick);
                break;
            case Biome::MOUNTAINS:
                buildWatchtower(s, ax, az, base, pick);
                break;
            case Biome::GRASSLAND:
            case Biome::SNOWFIELD:
            default:
                if (pick < 0.28f)      buildVillage(s, ax, az, base, pick);
                else if (pick < 0.50f) buildRuins(s, ax, az, base, pick);
                else if (pick < 0.74f) buildTower(s, ax, az, base, pick);
                else                   buildMonument(s, ax, az, base);
                break;
            }
        }
    }
}
