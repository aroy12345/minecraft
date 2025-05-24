#include "skyshaderprogram.h"

SkyShaderProgram::SkyShaderProgram(OpenGLContext* context)
    : ShaderProgram(context)
{}

SkyShaderProgram::~SkyShaderProgram()
{}

void SkyShaderProgram::setSunDirection(const glm::vec3 &dir) {
    setUnifVec3("u_SunDirection", dir);
}

void SkyShaderProgram::setSkyZenithColor(const glm::vec3 &color) {
    setUnifVec3("u_SkyZenithColor", color);
}

void SkyShaderProgram::setSkyHorizonColor(const glm::vec3 &color) {
    setUnifVec3("u_SkyHorizonColor", color);
}

void SkyShaderProgram::setSunColor(const glm::vec3 &color) {
    setUnifVec3("u_SunColor", color);
}

void SkyShaderProgram::setInvViewProj(const glm::mat4 &invViewProj) {
    setUnifMat4("u_InvViewProj", invViewProj);
}


void SkyShaderProgram::setFogDistance(float distance) {
    setUnifFloat("u_FogDistance", distance);
}

void SkyShaderProgram::setWeatherType(int type) {
    setUnifInt("u_WeatherType", type);
}

void SkyShaderProgram::setWeatherIntensity(float intensity) {
    setUnifFloat("u_WeatherIntensity", intensity);
}

void SkyShaderProgram::setTime(float time) {
    setUnifFloat("u_Time", time);
}
