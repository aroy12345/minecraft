#include "weathershaderprogram.h"
#include "openglcontext.h"

WeatherShaderProgram::WeatherShaderProgram(OpenGLContext* context)
    : ShaderProgram(context)
{}

WeatherShaderProgram::~WeatherShaderProgram()
{}

void WeatherShaderProgram::setCameraPos(const glm::vec3 &pos) {
    setUnifVec3("u_CameraPos", pos);
}

void WeatherShaderProgram::setTime(float time) {
    setUnifFloat("u_Time", time);
}

void WeatherShaderProgram::setWindDirection(const glm::vec3 &dir) {
    setUnifVec3("u_WindDirection", dir);
}

void WeatherShaderProgram::setWindStrength(float strength) {
    setUnifFloat("u_WindStrength", strength);
}

void WeatherShaderProgram::setWeatherType(int type) {
    setUnifInt("u_WeatherType", type);
}

void WeatherShaderProgram::setParticleTexture(int textureSlot) {
    setUnifInt("u_ParticleTexture", textureSlot);
}
