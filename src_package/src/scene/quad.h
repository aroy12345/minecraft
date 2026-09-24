#pragma once
#include "drawable.h"
#include "glm_includes.h"

// A screen-spanning quadrangle used by the post-process pipeline: the 3D
// scene is rendered to a FrameBuffer, then that texture is drawn onto this
// quad with the post shader (underwater / lava overlays).
class Quad : public Drawable {
public:
    Quad(OpenGLContext* context);
    void createVBOdata() override;
};
