

#include "sheep.h"
#include <glm/gtc/random.hpp>
#include "terrain.h"
#include <cmath>

// Static member initialization
QSoundEffect* Sheep::s_baahSound = nullptr;
float Sheep::s_timeSinceLastBaah = 0.f;
float Sheep::s_baahCooldown = glm::linearRand(30.f, 60.f);

void Sheep::initializeAudio() {
    if (!s_baahSound) {
        s_baahSound = new QSoundEffect();
        s_baahSound->setSource(QUrl("qrc:/sounds/baah.wav"));
        s_baahSound->setLoopCount(1);
        s_baahSound->setVolume(0.15f);
    }
}

void Sheep::cleanupAudio() {
    delete s_baahSound;
    s_baahSound = nullptr;
}


Sheep::Sheep(glm::vec3 pos, const Terrain& terrain)
    : Entity(pos), mcr_terrain(terrain),
    m_velocity(0.f), m_acceleration(0.f),
    m_onground(true),
    m_moveSpeed(1.2f),
    m_bodyWidth(1.4f * 0.5f),
    m_timeSinceLastTurn(0.f),
    m_randomTurnInterval(2.f),
    m_turning(false),
    m_targetAngle(0.f),
    m_turnSpeed(0.f),
    m_pausing(false),
    m_pauseTime(0.f),
    m_pauseDuration(0.f)
{}

void Sheep::tick(float dT, InputBundle&) {
    // The first sheep drives the shared bleat cooldown
    if (s_baahSound && !mcr_terrain.m_sheep.empty() && this == mcr_terrain.m_sheep[0].get()) {
        s_timeSinceLastBaah += dT;
        if (s_timeSinceLastBaah >= s_baahCooldown) {
            s_baahSound->play();
            s_timeSinceLastBaah = 0.f;
            s_baahCooldown = glm::linearRand(10.f, 20.f);
        }
    }

    // Handle pausing (standing still)
    if (m_pausing) {
        m_pauseTime += dT;
        if (m_pauseTime >= m_pauseDuration) {
            m_pausing = false;
            m_timeSinceLastTurn = 0.f;
            m_randomTurnInterval = glm::linearRand(5.f, 10.f);
        }
        return;
    }

    // Handle random direction choosing
    if (!m_turning) {
        m_timeSinceLastTurn += dT;
        if (m_timeSinceLastTurn >= m_randomTurnInterval) {
            chooseNewAction();
            m_timeSinceLastTurn = 0.f;
        }
    }

    // Handle turning
    if (m_turning) {
        float turnStep = m_turnSpeed * dT;

        if (std::abs(m_targetAngle) <= turnStep) {
            rotateOnUpGlobal(m_targetAngle);
            m_turning = false;
            m_targetAngle = 0.f;

            // After a turn, reset velocity forward
            glm::vec3 forwardDir = glm::normalize(glm::vec3(m_forward.x, 0.f, m_forward.z));
            m_velocity = forwardDir * m_moveSpeed;
        } else {
            rotateOnUpGlobal((m_targetAngle > 0 ? 1.f : -1.f) * turnStep);
            m_targetAngle -= (m_targetAngle > 0 ? 1.f : -1.f) * turnStep;
        }
    }

    // Handle physics
    if (!m_turning && !m_pausing) {
        if (m_onground && detectObstacleAhead()) {
            m_velocity.y = 6.f;
            m_onground = false;
        }
        applyPhysics(dT);
    }
}

// Rest of methods unchanged
void Sheep::chooseNewAction() {
    float actionChance = glm::linearRand(0.f, 1.f);

    if (actionChance < 0.2f) {
        // 20% chance to pause (stand still cutely)
        m_pausing = true;
        m_pauseTime = 0.f;
        m_pauseDuration = glm::linearRand(1.5f, 4.f);
        m_velocity = glm::vec3(0.f);
    } else {
        // 80% chance to turn
        m_turning = true;
        float turnOptions[] = {90.f, -90.f, 180.f};
        int randomIndex = static_cast<int>(glm::linearRand(0.f, 2.99f));
        m_targetAngle = turnOptions[randomIndex];
        m_turnSpeed = glm::linearRand(45.f, 90.f);
    }
}

void Sheep::applyPhysics(float dT) {
    const float GRAVITY = -9.8f;
    // const float DRAG = 0.95f; // slight slowdown over time

    // Apply gravity
    // if (!m_onground) {
    m_velocity.y += GRAVITY * dT;
    // }

    // Apply gentle drag to slow sheep naturally
    // m_velocity.x *= DRAG;
    // m_velocity.z *= DRAG;

    // De-embed: if streaming terrain grew around the sheep, or it wandered
    // into a wall it couldn't resolve, lift it to the surface so it is never
    // left wedged inside blocks.
    auto solidCell = [this](glm::vec3 q) -> bool {
        if (!mcr_terrain.hasChunkAt(floor(q.x), floor(q.z))) return false;
        BlockType b = mcr_terrain.getGlobalBlockAt(floor(q.x), floor(q.y), floor(q.z));
        return b != EMPTY && b != WATER && b != LAVA;
    };
    int embedGuard = 0;
    while (embedGuard++ < 40 && (solidCell(m_position + glm::vec3(0.f, -0.6f, 0.f)) ||
                                 solidCell(m_position + glm::vec3(0.f, 0.3f, 0.f)))) {
        m_position.y += 1.f;
        m_velocity.y = 0.f;
    }

    // Predict displacement
    glm::vec3 displacement = m_velocity * dT;

    // Build bounding box relative to center
    float minX = -0.35f, maxX = 0.75f;
    float minY = -0.7f, maxY = 0.5f;
    float minZ = -0.45f, maxZ = 0.45f;

    glm::vec3 offsets[] = {
        {minX, minY, minZ}, {minX, minY, maxZ},
        {maxX, minY, minZ}, {maxX, minY, maxZ},
        {minX, maxY, minZ}, {minX, maxY, maxZ},
        {maxX, maxY, minZ}, {maxX, maxY, maxZ},
        {0.f, 0.f, 0.f}
    };

    glm::vec3 computedPosition = m_position;
    bool stuck = true;
    float moveUpAmount = 0.f;

    for (int axis = 0; axis < 3; ++axis) {
        float resolution = 0.05f * glm::sign(displacement[axis]);
        float distanceToTravel = displacement[axis];
        float actualDistance = distanceToTravel;

        for (const auto& offset : offsets) {
            float distanceTravelled = 0.f;

            while (std::abs(distanceTravelled) < std::abs(distanceToTravel)) {
                glm::vec3 testPoint = computedPosition + offset;
                testPoint[axis] += distanceTravelled + resolution;

                if (!mcr_terrain.hasChunkAt(floor(testPoint.x), floor(testPoint.z))) {
                    distanceTravelled += resolution;
                    continue;
                }

                BlockType block = mcr_terrain.getGlobalBlockAt(
                    floor(testPoint.x),
                    floor(testPoint.y),
                    floor(testPoint.z)
                    );

                // Water counts as solid for sheep so they can't wander in
                // and sink; the obstacle check below turns them away instead
                if (block == EMPTY) {
                    distanceTravelled += resolution;
                    continue;
                } else {
                    if (std::abs(distanceTravelled) < std::abs(actualDistance)) {
                        actualDistance = distanceTravelled;
                    }
                    break;
                }
            }
        }

        if (actualDistance != 0) {
            stuck = false;
        } else if (axis == 1) {
            m_onground = true;
            m_velocity.y = 0.f;
        }

        computedPosition[axis] += actualDistance;
        if (axis == 1) moveUpAmount = actualDistance;
    }

    // If genuinely wedged against a wall, turn around next tick instead of
    // grinding upward into the terrain (the old 0.01 nudge slowly climbed
    // sheep through solid blocks).
    if (stuck && glm::length(glm::vec3(m_velocity.x, 0.f, m_velocity.z)) > 0.01f) {
        m_timeSinceLastTurn = m_randomTurnInterval;   // provoke a turn
    }

    moveUpLocal(moveUpAmount);
    m_position.x = computedPosition.x;
    m_position.z = computedPosition.z;
}

bool Sheep::detectObstacleAhead() {
    float lookDistance = m_bodyWidth + 0.5f;
    glm::vec3 forwardDir = glm::normalize(glm::vec3(m_forward.x, 0.f, m_forward.z));
    glm::vec3 forwardPos = m_position + forwardDir * lookDistance;

    if (!mcr_terrain.hasChunkAt(floor(forwardPos.x), floor(forwardPos.z)))
        return false;

    // Turn away from water rather than jumping onto it
    BlockType ahead = mcr_terrain.getGlobalBlockAt(
        floor(forwardPos.x), floor(m_position.y) - 1, floor(forwardPos.z));
    if (ahead == WATER || ahead == LAVA) {
        chooseNewAction();
        return false;
    }

    for (int y = 0; y <= 1; ++y) {
        BlockType block = mcr_terrain.getGlobalBlockAt(
            floor(forwardPos.x),
            floor(m_position.y) + y,
            floor(forwardPos.z)
            );

        if (block == WATER || block == LAVA) {
            chooseNewAction(); // never wade in - turn
            return false;
        }
        if (block != EMPTY) {
            BlockType above = mcr_terrain.getGlobalBlockAt(
                floor(forwardPos.x),
                floor(m_position.y) + y + 1,
                floor(forwardPos.z)
                );
            if (above == EMPTY) {
                return true; // jump over
            } else {
                chooseNewAction(); // can't jump - turn
                return false;
            }
        }
    }

    return false;
}
