#pragma once
#include "shaderprogram.h"
#include "glm_includes.h"

class SkyShaderProgram : public ShaderProgram {
public:
    SkyShaderProgram(OpenGLContext* context);
    virtual ~SkyShaderProgram();

    // Set sky-specific uniforms
    void setSunDirection(const glm::vec3 &dir);
    void setSkyZenithColor(const glm::vec3 &color);
    void setSkyHorizonColor(const glm::vec3 &color);
    void setSunColor(const glm::vec3 &color);
    void setInvViewProj(const glm::mat4 &invViewProj);

    // // Weather-specific uniforms
    void setFogDistance(float distance);
    void setWeatherType(int type);
    void setWeatherIntensity(float intensity);
    void setTime(float time);
};
