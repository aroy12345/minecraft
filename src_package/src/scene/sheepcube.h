#pragma once
#include "drawable.h"
#include "openglcontext.h"
#include <glm_includes.h>

class SheepCube : public Drawable {
public:
    SheepCube(OpenGLContext* context);

    void createVBOdata() override;
    GLenum drawMode() override;
};
