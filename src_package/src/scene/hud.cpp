#include "hud.h"

namespace HudLayout {
float slotW(float aspect) { return SLOT_H / aspect; }
glm::vec2 slotCenter(int i, int slotCount, float aspect) {
    float w = slotW(aspect) * 1.12f;
    float x0 = -w * slotCount * 0.5f + w * 0.5f;
    return glm::vec2(x0 + i * w, BOTTOM + SLOT_H * 0.5f);
}
glm::vec2 craftRowCenter(int row) {
    return glm::vec2(0.55f, CRAFT_TOP - row * CRAFT_ROW_H);
}
}

namespace {
void pushQuad(std::vector<glm::vec4> &pos, std::vector<glm::vec4> &aux,
              std::vector<GLuint> &idx, glm::vec2 lo, glm::vec2 hi,
              glm::vec4 auxBL, glm::vec4 auxTR) {
    GLuint s = pos.size();
    pos.push_back(glm::vec4(lo.x, lo.y, 0.f, 1.f));
    pos.push_back(glm::vec4(hi.x, lo.y, 0.f, 1.f));
    pos.push_back(glm::vec4(hi.x, hi.y, 0.f, 1.f));
    pos.push_back(glm::vec4(lo.x, hi.y, 0.f, 1.f));
    aux.push_back(auxBL);
    aux.push_back(glm::vec4(auxTR.x, auxBL.y, auxBL.z, auxBL.w));
    aux.push_back(auxTR);
    aux.push_back(glm::vec4(auxBL.x, auxTR.y, auxBL.z, auxBL.w));
    for (GLuint i : {s, s + 1, s + 2, s, s + 2, s + 3}) idx.push_back(i);
}

// Seven-segment digit rendering for inventory counts - no font assets
// needed. Segment order: top, top-right, bottom-right, bottom, bottom-left,
// top-left, middle.
const unsigned char SEG_MAP[10] = {
    0b0111111, 0b0000110, 0b1011011, 0b1001111, 0b1100110,
    0b1101101, 0b1111101, 0b0000111, 0b1111111, 0b1101111,
};

void pushDigit(std::vector<glm::vec4> &pos, std::vector<glm::vec4> &col,
               std::vector<GLuint> &idx, glm::vec2 c, float h, float aspect,
               int digit, glm::vec4 color) {
    float w = h * 0.55f / aspect;
    float t = h * 0.14f;      // segment thickness
    float tx = t / aspect;
    unsigned char m = SEG_MAP[digit % 10];
    auto seg = [&](glm::vec2 lo, glm::vec2 hi) {
        pushQuad(pos, col, idx, lo, hi, color, color);
    };
    if (m & 1)  seg({c.x - w/2, c.y + h/2 - t}, {c.x + w/2, c.y + h/2});          // top
    if (m & 2)  seg({c.x + w/2 - tx, c.y}, {c.x + w/2, c.y + h/2});               // top-right
    if (m & 4)  seg({c.x + w/2 - tx, c.y - h/2}, {c.x + w/2, c.y});               // bottom-right
    if (m & 8)  seg({c.x - w/2, c.y - h/2}, {c.x + w/2, c.y - h/2 + t});          // bottom
    if (m & 16) seg({c.x - w/2, c.y - h/2}, {c.x - w/2 + tx, c.y});               // bottom-left
    if (m & 32) seg({c.x - w/2, c.y}, {c.x - w/2 + tx, c.y + h/2});               // top-left
    if (m & 64) seg({c.x - w/2, c.y - t/2}, {c.x + w/2, c.y + t/2});              // middle
}

void pushNumber(std::vector<glm::vec4> &pos, std::vector<glm::vec4> &col,
                std::vector<GLuint> &idx, glm::vec2 rightCenter, float h,
                float aspect, int value, glm::vec4 color) {
    value = std::min(value, 999);
    float adv = h * 0.75f / aspect;
    if (value == 0) {
        pushDigit(pos, col, idx, rightCenter, h, aspect, 0, color);
        return;
    }
    int x = 0;
    while (value > 0 && x < 3) {
        pushDigit(pos, col, idx, rightCenter - glm::vec2(adv * x, 0.f), h, aspect,
                  value % 10, color);
        value /= 10;
        x++;
    }
}

// Atlas cell (column, row from the bottom) for each block icon
glm::vec2 iconCell(BlockType t) {
    switch (t) {
    case GRASS:     return {8, 13};
    case DIRT:      return {2, 15};
    case STONE:     return {1, 15};
    case SAND:      return {2, 14};
    case PLANK:     return {4, 15};
    case WOOD:      return {4, 14};
    case LEAF:      return {5, 12};
    case WATER:     return {13, 3};
    case BRICK:     return {7, 15};
    case COAL_ORE:  return {2, 13};
    case WIRE_OFF:  return {3, 12};
    case TORCH:     return {9, 9};
    case LEVER_OFF: return {0, 14};
    case LAMP_OFF:  return {5, 13};
    case SNOW:      return {2, 11};
    default:        return {7, 1};
    }
}
}

namespace HudLayout {
glm::vec2 iconCellFor(BlockType t) { return iconCell(t); }
}

HudFrame::HudFrame(OpenGLContext* context) : Drawable(context) {}
void HudFrame::createVBOdata() {}

void HudFrame::build(int slotCount, int selected, float aspect, const int *counts) {
    std::vector<glm::vec4> pos, col;
    std::vector<GLuint> idx;
    float w = HudLayout::slotW(aspect);

    for (int i = 0; i < slotCount; ++i) {
        glm::vec2 c = HudLayout::slotCenter(i, slotCount, aspect);
        glm::vec2 half(w * 0.5f, HudLayout::SLOT_H * 0.5f);
        pushQuad(pos, col, idx, c - half, c + half,
                 glm::vec4(0.05f, 0.05f, 0.05f, 0.55f),
                 glm::vec4(0.05f, 0.05f, 0.05f, 0.55f));
        if (i == selected) {
            // bright outline: four thin quads around the slot
            float t = 0.012f, tx = t / aspect;
            glm::vec4 white(0.95f, 0.95f, 0.95f, 0.95f);
            pushQuad(pos, col, idx, c - half - glm::vec2(tx, t),
                     glm::vec2(c.x + half.x + tx, c.y - half.y), white, white);
            pushQuad(pos, col, idx, glm::vec2(c.x - half.x - tx, c.y + half.y),
                     c + half + glm::vec2(tx, t), white, white);
            pushQuad(pos, col, idx, c - half - glm::vec2(tx, 0.f),
                     glm::vec2(c.x - half.x, c.y + half.y), white, white);
            pushQuad(pos, col, idx, glm::vec2(c.x + half.x, c.y - half.y),
                     c + half + glm::vec2(tx, 0.f), white, white);
        }
        if (counts && counts[i] >= 0) {
            pushNumber(pos, col, idx, glm::vec2(c.x + half.x * 0.55f, c.y - half.y * 0.45f),
                       0.032f, aspect, counts[i], glm::vec4(1.f, 1.f, 1.f, 0.95f));
        }
    }

    indexCounts[INDEX] = idx.size();
    generateBuffer(INDEX);
    bindBuffer(INDEX);
    mp_context->glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(GLuint), idx.data(), GL_STATIC_DRAW);
    generateBuffer(POSITION);
    bindBuffer(POSITION);
    mp_context->glBufferData(GL_ARRAY_BUFFER, pos.size() * sizeof(glm::vec4), pos.data(), GL_STATIC_DRAW);
    generateBuffer(COLOR);
    bindBuffer(COLOR);
    mp_context->glBufferData(GL_ARRAY_BUFFER, col.size() * sizeof(glm::vec4), col.data(), GL_STATIC_DRAW);
}

HudIcons::HudIcons(OpenGLContext* context) : Drawable(context) {}
void HudIcons::createVBOdata() {}

void HudIcons::build(const BlockType *blocks, int slotCount, float aspect) {
    std::vector<glm::vec4> pos, uv;
    std::vector<GLuint> idx;
    float w = HudLayout::slotW(aspect);

    for (int i = 0; i < slotCount; ++i) {
        glm::vec2 c = HudLayout::slotCenter(i, slotCount, aspect);
        glm::vec2 half(w * 0.36f, HudLayout::SLOT_H * 0.36f);
        glm::vec2 cell = iconCell(blocks[i]);
        glm::vec2 uvLo = cell / 16.f;
        glm::vec2 uvHi = uvLo + glm::vec2(1.f / 16.f);
        pushQuad(pos, uv, idx, c - half, c + half,
                 glm::vec4(uvLo, 0.f, 0.f), glm::vec4(uvHi, 0.f, 0.f));
    }

    indexCounts[INDEX] = idx.size();
    generateBuffer(INDEX);
    bindBuffer(INDEX);
    mp_context->glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(GLuint), idx.data(), GL_STATIC_DRAW);
    generateBuffer(POSITION);
    bindBuffer(POSITION);
    mp_context->glBufferData(GL_ARRAY_BUFFER, pos.size() * sizeof(glm::vec4), pos.data(), GL_STATIC_DRAW);
    generateBuffer(UV);
    bindBuffer(UV);
    mp_context->glBufferData(GL_ARRAY_BUFFER, uv.size() * sizeof(glm::vec4), uv.data(), GL_STATIC_DRAW);
}

CraftFrame::CraftFrame(OpenGLContext* context) : Drawable(context) {}
void CraftFrame::createVBOdata() {}

void CraftFrame::build(const CraftRecipe *recipes, int count, int selected,
                       const int *have1, const int *have2, float aspect) {
    std::vector<glm::vec4> pos, col;
    std::vector<GLuint> idx;
    float w = 0.42f, rh = HudLayout::CRAFT_ROW_H * 0.46f;

    // panel backdrop
    glm::vec2 top = HudLayout::craftRowCenter(0), bot = HudLayout::craftRowCenter(count - 1);
    pushQuad(pos, col, idx, glm::vec2(top.x - w, bot.y - rh - 0.02f),
             glm::vec2(top.x + w, top.y + rh + 0.02f),
             glm::vec4(0.05f, 0.05f, 0.08f, 0.6f), glm::vec4(0.05f, 0.05f, 0.08f, 0.6f));

    for (int i = 0; i < count; ++i) {
        glm::vec2 c = HudLayout::craftRowCenter(i);
        bool can = have1[i] >= recipes[i].n1 &&
                   (recipes[i].in2 == EMPTY || have2[i] >= recipes[i].n2);
        glm::vec4 digitCol = can ? glm::vec4(0.8f, 1.f, 0.8f, 1.f)
                                 : glm::vec4(1.f, 0.45f, 0.45f, 1.f);
        if (i == selected) {
            pushQuad(pos, col, idx, glm::vec2(c.x - w + 0.01f, c.y - rh),
                     glm::vec2(c.x + w - 0.01f, c.y + rh),
                     glm::vec4(0.9f, 0.9f, 0.9f, 0.22f), glm::vec4(0.9f, 0.9f, 0.9f, 0.22f));
        }
        // required counts beside each ingredient, result count at the end
        float ix = c.x - w + 0.10f;
        pushNumber(pos, col, idx, glm::vec2(ix + 0.055f, c.y - 0.02f), 0.035f, aspect,
                   recipes[i].n1, digitCol);
        if (recipes[i].in2 != EMPTY) {
            pushNumber(pos, col, idx, glm::vec2(ix + 0.21f, c.y - 0.02f), 0.035f, aspect,
                       recipes[i].n2, digitCol);
        }
        pushNumber(pos, col, idx, glm::vec2(c.x + w - 0.035f, c.y - 0.02f), 0.035f, aspect,
                   recipes[i].nOut, glm::vec4(1.f, 1.f, 1.f, 1.f));
    }

    indexCounts[INDEX] = idx.size();
    generateBuffer(INDEX);
    bindBuffer(INDEX);
    mp_context->glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(GLuint), idx.data(), GL_STATIC_DRAW);
    generateBuffer(POSITION);
    bindBuffer(POSITION);
    mp_context->glBufferData(GL_ARRAY_BUFFER, pos.size() * sizeof(glm::vec4), pos.data(), GL_STATIC_DRAW);
    generateBuffer(COLOR);
    bindBuffer(COLOR);
    mp_context->glBufferData(GL_ARRAY_BUFFER, col.size() * sizeof(glm::vec4), col.data(), GL_STATIC_DRAW);
}

CraftIcons::CraftIcons(OpenGLContext* context) : Drawable(context) {}
void CraftIcons::createVBOdata() {}

void CraftIcons::build(const CraftRecipe *recipes, int count, float aspect) {
    std::vector<glm::vec4> pos, uv;
    std::vector<GLuint> idx;
    float s = 0.045f;

    auto icon = [&](glm::vec2 c, BlockType t) {
        glm::vec2 half(s / aspect, s);
        glm::vec2 lo = iconCell(t) / 16.f;
        glm::vec2 hi = lo + glm::vec2(1.f / 16.f);
        pushQuad(pos, uv, idx, c - half, c + half,
                 glm::vec4(lo, 0.f, 0.f), glm::vec4(hi, 0.f, 0.f));
    };
    for (int i = 0; i < count; ++i) {
        glm::vec2 c = HudLayout::craftRowCenter(i);
        float ix = c.x - 0.32f;
        icon(glm::vec2(ix, c.y), recipes[i].in1);
        if (recipes[i].in2 != EMPTY) {
            icon(glm::vec2(ix + 0.155f, c.y), recipes[i].in2);
        }
        icon(glm::vec2(c.x + 0.27f, c.y), recipes[i].out);
    }

    indexCounts[INDEX] = idx.size();
    generateBuffer(INDEX);
    bindBuffer(INDEX);
    mp_context->glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(GLuint), idx.data(), GL_STATIC_DRAW);
    generateBuffer(POSITION);
    bindBuffer(POSITION);
    mp_context->glBufferData(GL_ARRAY_BUFFER, pos.size() * sizeof(glm::vec4), pos.data(), GL_STATIC_DRAW);
    generateBuffer(UV);
    bindBuffer(UV);
    mp_context->glBufferData(GL_ARRAY_BUFFER, uv.size() * sizeof(glm::vec4), uv.data(), GL_STATIC_DRAW);
}

namespace Crafting {
// Classic Minecraft conversions plus the recipes that feed the redstone
// system (coal is mined underground)
const CraftRecipe RECIPES[] = {
    {WOOD, 1, EMPTY, 0, PLANK, 4},
    {STONE, 4, EMPTY, 0, BRICK, 4},
    {PLANK, 1, COAL_ORE, 1, TORCH, 4},
    {COAL_ORE, 1, EMPTY, 0, WIRE_OFF, 8},
    {STONE, 1, PLANK, 1, LEVER_OFF, 2},
    {STONE, 4, TORCH, 1, LAMP_OFF, 2},
};
const int COUNT = static_cast<int>(sizeof(RECIPES) / sizeof(RECIPES[0]));

BlockType invItemFor(BlockType t) {
    switch (t) {
    case WIRE_ON:  return WIRE_OFF;
    case LEVER_ON: return LEVER_OFF;
    case LAMP_ON:  return LAMP_OFF;
    default:       return t;
    }
}
}
