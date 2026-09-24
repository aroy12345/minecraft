#pragma once
#include "drawable.h"
#include "glm_includes.h"

// A wireframe unit cube drawn around the block the crosshair is targeting,
// so the player can see exactly what a click will break or build against.
class BlockHighlight : public Drawable {
public:
    BlockHighlight(OpenGLContext* context);
    void createVBOdata() override;
    GLenum drawMode() override;
};
