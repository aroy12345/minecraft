#pragma once
#include "shaderprogram.h"
#include "glm_includes.h"

class WeatherShaderProgram : public ShaderProgram {
public:
    WeatherShaderProgram(OpenGLContext* context);
    virtual ~WeatherShaderProgram();

    // Weather shader uniforms
    void setCameraPos(const glm::vec3 &pos);
    void setTime(float time);
    void setWindDirection(const glm::vec3 &dir);
    void setWindStrength(float strength);
    void setWeatherType(int type);
    void setParticleTexture(int textureSlot);
};
