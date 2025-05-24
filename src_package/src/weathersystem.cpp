#include "weathersystem.h"
#include <algorithm>
#include <ctime>
#include "weathershaderprogram.h"
#include "scene/terrain.h"
#include <QDebug>

WeatherSystem::WeatherSystem(OpenGLContext* context)
    : mp_context(context),
    m_currentWeather(CLEAR),
    m_weatherIntensity(0.0f),
    m_weatherChangeTimer(0.0f),
    m_maxParticles(5000),
    m_activeParticles(0),
    m_currentTime(0.0f)
{
    // Initialize random number generator with current time
    m_rng.seed(static_cast<unsigned int>(std::time(nullptr)));
    m_dist = std::uniform_real_distribution<float>(0.0f, 1.0f);
}

WeatherSystem::~WeatherSystem() {
    // Clean up OpenGL resources
    mp_context->glDeleteVertexArrays(1, &m_particleVAO);
    mp_context->glDeleteBuffers(1, &m_particleVBO);
}

void WeatherSystem::initialize() {
    // Create VAO and VBO for particles
    mp_context->glGenVertexArrays(1, &m_particleVAO);
    mp_context->glGenBuffers(1, &m_particleVBO);

    // Initialize particle buffer
    mp_context->glBindVertexArray(m_particleVAO);
    mp_context->glBindBuffer(GL_ARRAY_BUFFER, m_particleVBO);
    mp_context->glBufferData(GL_ARRAY_BUFFER, m_maxParticles * sizeof(float) * 8, nullptr, GL_DYNAMIC_DRAW);

    // Position (x, y, z)
    mp_context->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 8, nullptr);
    mp_context->glEnableVertexAttribArray(0);

    // Velocity (vx, vy, vz)
    mp_context->glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 8, (void*)(sizeof(float) * 3));
    mp_context->glEnableVertexAttribArray(1);

    // Size and life (size, life)
    mp_context->glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 8, (void*)(sizeof(float) * 6));
    mp_context->glEnableVertexAttribArray(2);

}

void WeatherSystem::update(float deltaTime, const glm::vec3& playerPos, const Terrain* terrain) {
    // Update weather change timer
    m_currentTime += deltaTime;
    m_weatherChangeTimer -= deltaTime;

    // Randomly change weather if timer expires
    if (m_weatherChangeTimer <= 0.0f) {
        float rand = m_dist(m_rng);

        if (rand < 0.6f) {
            // 60% chance of clear weather
            setWeather(CLEAR);
        } else if (rand < 0.8f) {
            // 20% chance of rain
            setWeather(RAIN);
        } else {
            // 20% chance of snow
            setWeather(SNOWY);
        }

        // Set new timer (between 30-120 seconds)
        m_weatherChangeTimer = 30.0f + m_dist(m_rng) * 90.0f;
    }

    // Generate particles if we have weather
    if (m_currentWeather != CLEAR) {
        generateParticles(playerPos, terrain);
    } else {
        // Clear particles if weather is clear
        m_particleData.clear();
        m_activeParticles = 0;
    }
}

bool WeatherSystem::checkParticleCollision(const glm::vec3& position, const Terrain* terrain) {
    // Check if the particle is inside a block
    // We use terrain's getBlockAt function to check if there's a block at this position
    if (terrain) {
        // Round position to nearest block coordinates
        int x = static_cast<int>(std::floor(position.x));
        int y = static_cast<int>(std::floor(position.y));
        int z = static_cast<int>(std::floor(position.z));

        // Get the block type at this position
        BlockType block = terrain->getGlobalBlockAt(x, y, z);

        // Return true if it's not an air block (EMPTY = 0)
        return block != EMPTY;
    }

    return false;
}

void WeatherSystem::generateParticles(const glm::vec3& playerPos, const Terrain* terrain) {
    // Calculate how many particles to generate based on weather intensity
    int particlesToGenerate = static_cast<int>(m_maxParticles * m_weatherIntensity);

    // Prepare particle data buffer
    m_particleData.clear();
    m_particleData.reserve(particlesToGenerate * 8); // 8 floats per particle

    // Distribution for random positions
    std::uniform_real_distribution<float> posDist(-40.0f, 40.0f);
    std::uniform_real_distribution<float> heightDist(0.0f, 20.0f);
    std::uniform_real_distribution<float> sizeDist(0.1f, 0.3f);
    std::uniform_real_distribution<float> lifeDist(0.3f, 1.0f);

    m_activeParticles = 0;

    for (int i = 0; i < particlesToGenerate; ++i) {
        // Random position around the player
        float x = playerPos.x + posDist(m_rng);
        float z = playerPos.z + posDist(m_rng);
        float y = playerPos.y + heightDist(m_rng);

        // Skip if particle would be inside a block
        if (checkParticleCollision(glm::vec3(x, y, z), terrain)) {
            continue;
        }

        // Velocity and size based on weather type
        float vx = 0.0f;
        float vy = 0.0f;
        float vz = 0.0f;
        float size = 0.0f;

        if (m_currentWeather == RAIN) {
            // Rain falls straight down rapidly
            vx = 0.0f;
            vy = -15.0f - m_dist(m_rng) * 5.0f;
            vz = 0.0f;
            size = sizeDist(m_rng) * 0.15f; // Thin rain drops
        } else if (m_currentWeather == SNOWY) {
            // Snow falls more slowly with some drift
            vx = (m_dist(m_rng) * 2.0f - 1.0f) * 2.0f;
            vy = -2.0f - m_dist(m_rng) * 1.0f;
            vz = (m_dist(m_rng) * 2.0f - 1.0f) * 2.0f;
            size = sizeDist(m_rng) * 0.4f; // Larger snowflakes
        }

        // Randomize life so not all particles appear/disappear at the same time
        float life = lifeDist(m_rng);

        // Add particle data
        m_particleData.push_back(x);     // Position X
        m_particleData.push_back(y);     // Position Y
        m_particleData.push_back(z);     // Position Z
        m_particleData.push_back(vx);    // Velocity X
        m_particleData.push_back(vy);    // Velocity Y
        m_particleData.push_back(vz);    // Velocity Z
        m_particleData.push_back(size);  // Size
        m_particleData.push_back(life);  // Life

        m_activeParticles++;
    }

    // Update VBO with new particle data
    if (m_activeParticles > 0) {
        mp_context->glBindBuffer(GL_ARRAY_BUFFER, m_particleVBO);
        mp_context->glBufferData(GL_ARRAY_BUFFER,
                                 m_particleData.size() * sizeof(float),
                                 m_particleData.data(),
                                 GL_DYNAMIC_DRAW);
    }
}


void WeatherSystem::drawParticles(ShaderProgram* shader, const glm::mat4& viewProj, const glm::vec3& cameraPos) {
    // Skip if weather is clear or no particles
    if (m_currentWeather == CLEAR || m_activeParticles <= 0) {
        return;
    }

    // Cast to WeatherShaderProgram to access specific methods
    WeatherShaderProgram* weatherShader = static_cast<WeatherShaderProgram*>(shader);

    // Set view projection matrix
    weatherShader->setUnifMat4("u_ViewProj", viewProj);

    // Set camera position for billboard calculation
    weatherShader->setCameraPos(cameraPos);

    // Set weather type for shader - CORRECTED!
    int weatherTypeValue = (m_currentWeather == RAIN) ? 0 : 1; // 0 for rain, 1 for snow
    weatherShader->setWeatherType(weatherTypeValue);

    // Set time for animation
    weatherShader->setTime(m_currentTime);

    // Bind vertex array
    mp_context->glBindVertexArray(m_particleVAO);

    // Draw particles using instanced rendering
    mp_context->glDrawArraysInstanced(GL_TRIANGLES, 0, 6, m_activeParticles);
}



void WeatherSystem::setWeather(WeatherType type) {
    m_currentWeather = type;

    // Set intensity based on weather type
    if (type == CLEAR) {
        m_weatherIntensity = 0.0f;
    } else {
        // 50-100% intensity for rain/snow
        m_weatherIntensity = 0.5f + m_dist(m_rng) * 0.5f;
    }

    qDebug() << "Weather changed to:" << (type == CLEAR ? "Clear" : (type == RAIN ? "Rain" : "Snow"))
             << "with intensity:" << m_weatherIntensity;
}

glm::vec3 WeatherSystem::getWeatherSkyFactor() const {
    // Return a factor to darken the sky based on weather
    switch (m_currentWeather) {
    case RAIN:
        // Darker grey sky for rain
        return glm::vec3(1.0f) - (glm::vec3(0.5f, 0.5f, 0.5f) * m_weatherIntensity);
    case SNOWY:
        // Light grey sky for snow
        return glm::vec3(1.0f) - (glm::vec3(0.3f, 0.3f, 0.3f) * m_weatherIntensity);
    case CLEAR:
    default:
        // No modification for clear weather
        return glm::vec3(1.0f);
    }
}

float WeatherSystem::getWeatherFogDistance() const {
    // Return fog distance based on weather
    switch (m_currentWeather) {
    case RAIN:
        // Medium fog for rain
        return 100.0f / (0.3f + 0.7f * m_weatherIntensity);
    case SNOWY:
        // Denser fog for snow
        return 80.0f / (0.3f + 0.7f * m_weatherIntensity);
    case CLEAR:
    default:
        // No fog for clear weather
        return 1000.0f;
    }
}
