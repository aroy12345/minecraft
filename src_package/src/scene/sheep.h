
#pragma once
#include "entity.h"
#include <QSoundEffect>
class Terrain;

class Sheep : public Entity {
public:
    Sheep(glm::vec3 pos, const Terrain& terrain);
    virtual void tick(float dT, InputBundle& input) override;

    // Sets up / tears down the shared bleat sound. QSoundEffect::play() is
    // asynchronous (mixing happens on the OS audio stack), so playing it
    // from the main thread never blocks a frame.
    static void initializeAudio();
    static void cleanupAudio();

    glm::vec3 getForward() const { return m_forward; }
    // True while actually strolling (not pausing or turning in place)
    bool isWalking() const { return !m_pausing && !m_turning; }

private:
    // Terrain reference
    const Terrain& mcr_terrain;

    // Physics
    glm::vec3 m_velocity;
    glm::vec3 m_acceleration;
    bool m_onground;
    float m_moveSpeed;
    float m_bodyWidth;

    // Movement behavior
    float m_timeSinceLastTurn;
    float m_randomTurnInterval;
    bool m_turning;
    float m_targetAngle;
    float m_turnSpeed;
    bool m_pausing;
    float m_pauseTime;
    float m_pauseDuration;

    // Shared bleat sound, played on a random cooldown by the first sheep
    static QSoundEffect* s_baahSound;
    static float s_timeSinceLastBaah;
    static float s_baahCooldown;

    // Helper methods
    void chooseNewAction();
    void applyPhysics(float dT);
    bool detectObstacleAhead();
};
