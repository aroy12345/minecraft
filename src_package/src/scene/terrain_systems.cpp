#include "terrain.h"

namespace {
bool isRedstoneBlock(BlockType t) {
    return t == WIRE_OFF || t == WIRE_ON || t == TORCH ||
           t == LEVER_OFF || t == LEVER_ON || t == LAMP_OFF || t == LAMP_ON;
}
}

// Minecraft-lite fluid flow: fluid falls into empty space below it, and
// spreads laterally up to a per-fluid range (lava is short and sluggish).
// A small per-tick budget makes floods advance visibly over time.
void Terrain::processFluids() {
    int budget = 40;
    while (budget-- > 0 && !m_fluidQueue.empty()) {
        FluidSpread f = m_fluidQueue.front();
        m_fluidQueue.pop_front();
        if (!hasChunkAt(f.pos.x, f.pos.z)) continue;
        BlockType src = getGlobalBlockAt(f.pos.x, f.pos.y, f.pos.z);
        if (src != WATER && src != LAVA) continue;
        int maxSpread = (src == WATER) ? 6 : 3;

        glm::ivec3 below = f.pos + glm::ivec3(0, -1, 0);
        if (below.y >= 1 && hasChunkAt(below.x, below.z) &&
            getGlobalBlockAt(below.x, below.y, below.z) == EMPTY) {
            setBlockDeferred(below.x, below.y, below.z, src);
            m_fluidQueue.push_back({below, 0}); // falling resets the range
            continue;
        }
        if (f.depth >= maxSpread) continue;
        const glm::ivec3 lat[4] = {{1,0,0}, {-1,0,0}, {0,0,1}, {0,0,-1}};
        for (const glm::ivec3 &d : lat) {
            glm::ivec3 n = f.pos + d;
            if (hasChunkAt(n.x, n.z) && getGlobalBlockAt(n.x, n.y, n.z) == EMPTY) {
                setBlockDeferred(n.x, n.y, n.z, src);
                m_fluidQueue.push_back({n, f.depth + 1});
            }
        }
    }
}

// A compact circuit solver over the region near an edit. Power sources
// (torches, switched-on levers) push current through connected wire with
// Minecraft's 15-step decay; wires and lamps flip to their ON/OFF variants
// to match. All writes batch through the deferred path, so the meshes and
// the glow (via the lighting engine) update together.
void Terrain::recomputeRedstone(const glm::ivec3 &center) {
    constexpr int R = 24;
    constexpr int MAX_POWER = 15;
    auto key = [](const glm::ivec3 &p) {
        return (static_cast<int64_t>(p.x) << 40) ^ (static_cast<int64_t>(p.y) << 20) ^ p.z;
    };
    const glm::ivec3 dirs[6] = {{1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}};

    // Gather the local circuit and seed power at the sources
    std::unordered_map<int64_t, int> power; // wire cells -> power level
    std::deque<std::pair<glm::ivec3, int>> queue;
    std::vector<glm::ivec3> wires, lamps;
    for (int x = center.x - R; x <= center.x + R; ++x) {
        for (int z = center.z - R; z <= center.z + R; ++z) {
            if (!hasChunkAt(x, z)) continue;
            for (int y = std::max(1, center.y - R); y <= std::min(255, center.y + R); ++y) {
                BlockType b = getGlobalBlockAt(x, y, z);
                if (b == WIRE_OFF || b == WIRE_ON) {
                    wires.push_back({x, y, z});
                } else if (b == LAMP_OFF || b == LAMP_ON) {
                    lamps.push_back({x, y, z});
                } else if (b == TORCH || b == LEVER_ON) {
                    queue.push_back({{x, y, z}, MAX_POWER});
                }
            }
        }
    }

    // Push power through connected wire, decaying one level per step
    while (!queue.empty()) {
        auto [p, level] = queue.front();
        queue.pop_front();
        for (const glm::ivec3 &d : dirs) {
            glm::ivec3 n = p + d;
            if (!hasChunkAt(n.x, n.z) || n.y < 1 || n.y > 255) continue;
            BlockType b = getGlobalBlockAt(n.x, n.y, n.z);
            if (b != WIRE_OFF && b != WIRE_ON) continue;
            int cand = level - 1;
            auto it = power.find(key(n));
            if (cand > 0 && (it == power.end() || it->second < cand)) {
                power[key(n)] = cand;
                queue.push_back({n, cand});
            }
        }
    }

    // Apply the new wire states, and light lamps touching powered elements
    for (const glm::ivec3 &w : wires) {
        bool on = power.count(key(w)) > 0;
        BlockType cur = getGlobalBlockAt(w.x, w.y, w.z);
        BlockType want = on ? WIRE_ON : WIRE_OFF;
        if (cur != want) setBlockDeferred(w.x, w.y, w.z, want);
    }
    for (const glm::ivec3 &l : lamps) {
        bool on = false;
        for (const glm::ivec3 &d : dirs) {
            glm::ivec3 n = l + d;
            if (!hasChunkAt(n.x, n.z) || n.y < 1 || n.y > 255) continue;
            BlockType b = getGlobalBlockAt(n.x, n.y, n.z);
            if (b == TORCH || b == LEVER_ON || (power.count(key(n)) && power[key(n)] > 0)) {
                on = true;
                break;
            }
        }
        BlockType cur = getGlobalBlockAt(l.x, l.y, l.z);
        BlockType want = on ? LAMP_ON : LAMP_OFF;
        if (cur != want) setBlockDeferred(l.x, l.y, l.z, want);
    }
}

