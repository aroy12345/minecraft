
#pragma once
#include "openglcontext.h"
#include "drawable.h"
#include "glm_includes.h"
#include "shaderprogram.h"
#include <random>

enum WeatherType {
    CLEAR,
    RAIN,
    SNOWY
};

// A simplified class that manages weather state and renders weather particles
class WeatherSystem {
private:
    OpenGLContext* mp_context;

    // Weather state
    WeatherType m_currentWeather;
    float m_weatherIntensity; // 0.0 to 1.0
    float m_weatherChangeTimer; // Timer for randomly changing weather

    // Random number generation
    std::mt19937 m_rng;
    std::uniform_real_distribution<float> m_dist;

    // Particle system data
    GLuint m_particleVAO;
    GLuint m_particleVBO;
    int m_maxParticles;
    int m_activeParticles;

    // Particle data storage
    std::vector<float> m_particleData;

    // Helper to check if a particle hits a block
    bool checkParticleCollision(const glm::vec3& position, const class Terrain* terrain);

    // Create particles around player
    void generateParticles(const glm::vec3& playerPos, const class Terrain* terrain);



    float m_currentTime;

public:
    WeatherSystem(OpenGLContext* context);
    ~WeatherSystem();

    // Initialize the weather system
    void initialize();

    // Update the weather system
    void update(float deltaTime, const glm::vec3& playerPos, const class Terrain* terrain);

    // Draw weather particles
    void drawParticles(ShaderProgram* shader, const glm::mat4& viewProj, const glm::vec3& cameraPos);

    // Weather control functions
    void setWeather(WeatherType type);
    WeatherType getCurrentWeather() const { return m_currentWeather; }
    float getWeatherIntensity() const { return m_weatherIntensity; }

    // Sky modification parameters
    glm::vec3 getWeatherSkyFactor() const;
    float getWeatherFogDistance() const;
};
