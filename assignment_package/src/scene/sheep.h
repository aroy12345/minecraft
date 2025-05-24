
#pragma once
#include "entity.h"
#include <QSoundEffect>
#include <QThread>
#include <QWaitCondition>
#include <atomic>
class Terrain;

// Audio thread class to handle sound operations off the main thread
class AudioThread : public QThread {
public:
    AudioThread();
    ~AudioThread();

    void run() override;
    void requestPlay();

private:
    QSoundEffect* m_sound;
    bool m_running;
    QWaitCondition m_waitCondition;
};

class Sheep : public Entity {
public:
    Sheep(glm::vec3 pos, const Terrain& terrain);
    virtual void tick(float dT, InputBundle& input) override;

    // Static methods for audio thread management
    static void initializeAudioThread();
    static void cleanupAudioThread();

    // Allow AudioThread to access s_playRequested
    friend class AudioThread;
    glm::vec3 getForward() const { return m_forward; }




private:
    // Terrain reference
    const Terrain& mcr_terrain;

    // Physics
    glm::vec3 m_velocity;
    glm::vec3 m_acceleration;
    bool m_onground;
    float m_moveSpeed;
    float m_bodyWidth;
    float m_bodyDepth;

    // Movement behavior
    float m_timeSinceLastTurn;
    float m_randomTurnInterval;
    bool m_turning;
    float m_targetAngle;
    float m_turnSpeed;
    bool m_pausing;
    float m_pauseTime;
    float m_pauseDuration;

    // Audio system (thread-safe)
    static QSoundEffect* s_baahSound;
    static float s_timeSinceLastBaah;
    static float s_baahCooldown;
    static bool s_soundInitialized;
    static std::atomic<bool> s_playRequested;
    static AudioThread* s_audioThread;

    // Helper methods
    void chooseNewAction();
    void applyPhysics(float dT);
    bool detectObstacleAhead();
};
