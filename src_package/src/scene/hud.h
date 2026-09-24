#pragma once
#include "drawable.h"
#include "chunk.h"
#include "glm_includes.h"
#include <array>

// The inventory hotbar (Milestone 3 GUI): a row of slots along the bottom
// of the screen showing which block right-click will place. HudFrame draws
// the translucent slot backgrounds plus a bright outline around the
// selected slot (flat shader); HudIcons draws each block's atlas texture
// (hud shader). Both live in NDC and rebuild when the selection or the
// window aspect changes.
class HudFrame : public Drawable {
public:
    HudFrame(OpenGLContext* context);
    void createVBOdata() override;
    // counts[i] is drawn under slot i as seven-segment digits; pass -1 for
    // an unlimited slot (no digits shown)
    void build(int slotCount, int selected, float aspect, const int *counts);
};

class HudIcons : public Drawable {
public:
    HudIcons(OpenGLContext* context);
    void createVBOdata() override;
    void build(const BlockType *blocks, int slotCount, float aspect);
};

// A crafting recipe: up to two ingredient stacks in, one stack out
struct CraftRecipe {
    BlockType in1; int n1;
    BlockType in2; int n2;   // in2 == EMPTY means single-ingredient
    BlockType out; int nOut;
};

// The crafting book and inventory-item normalization shared by the HUD,
// the input layer, and the self-test
namespace Crafting {
extern const CraftRecipe RECIPES[];
extern const int COUNT;
// A broken ON-state redstone block stacks as its OFF-state item
BlockType invItemFor(BlockType t);
}

// The crafting menu (Milestone 3): opened with B, it lists the recipes
// with their ingredient and result icons plus live inventory counts,
// rendered with the same two-drawable split as the hotbar.
class CraftFrame : public Drawable {
public:
    CraftFrame(OpenGLContext* context);
    void createVBOdata() override;
    void build(const CraftRecipe *recipes, int count, int selected,
               const int *have1, const int *have2, float aspect);
};

class CraftIcons : public Drawable {
public:
    CraftIcons(OpenGLContext* context);
    void createVBOdata() override;
    void build(const CraftRecipe *recipes, int count, float aspect);
};

// Shared layout so the frame and icons agree
namespace HudLayout {
constexpr float SLOT_H = 0.11f;   // slot height in NDC
constexpr float BOTTOM = -0.95f;  // bottom edge of the bar
constexpr float CRAFT_TOP = 0.55f;
constexpr float CRAFT_ROW_H = 0.14f;
float slotW(float aspect);
glm::vec2 slotCenter(int i, int slotCount, float aspect);
glm::vec2 craftRowCenter(int row);
glm::vec2 iconCellFor(BlockType t);
}
