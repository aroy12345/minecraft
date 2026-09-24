#include "mygl.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QImage>
#include <vector>

// MC_SHOTS=<dir>: while the scripted demo plays in real time, grab a
// full-resolution PNG at a handful of curated moments and save it to <dir>,
// then quit once the last one is captured. The demo itself builds the world
// features (placed blocks, a redstone line, a lava cavern) and drives the
// camera to each vantage, so every shot is a real in-engine frame rather than
// a staged pose. grabFramebuffer() re-renders the widget on demand, so setting
// m_hudVisible (and optionally re-aiming the pitch) immediately before the grab
// is enough to control exactly how that shot is framed.
void MyGL::captureShots() {
    static const QString dir = qEnvironmentVariable("MC_SHOTS");
    if (dir.isEmpty()) return;

    // pitch == 0 keeps the demo's own framing; a non-zero value re-aims the
    // camera to that absolute pitch for the grab (used to turn the steep
    // fly-over into wide, horizon-holding vistas during the golden finale).
    struct Shot { float t; const char *name; bool hud; float pitch; };
    static const std::vector<Shot> shots = {
        {  4.5f, "01_spawn_coast",   false,   0.f},
        { 55.0f, "02_redstone",      true,    0.f},
        { 66.5f, "03_third_person",  true,    0.f},
        {112.0f, "04_snow_forest",   false,  -6.f},
        {131.0f, "05_lava_cavern",   false,   0.f},
        {133.5f, "06_lava_cavern_b", false,   0.f},
        {150.0f, "07_golden_top",    false, -38.f},
        {152.5f, "08_golden_horizon",false, -22.f},
        {160.0f, "09_biome_top",     false, -40.f},
        {163.0f, "10_biome_horizon", false, -20.f},
        {165.0f, "11_finale_wide",   false, -16.f},
    };

    static size_t idx = 0;
    static bool ready = false;
    if (!ready) { QDir().mkpath(dir); ready = true; }
    if (idx >= shots.size()) { QCoreApplication::quit(); return; }

    const Shot &s = shots[idx];
    m_hudVisible = s.hud;   // the grab below re-renders with this state

    // Grab as soon as the vantage is reached and the world around it has
    // streamed in, with a short fallback so a heavily loading frame still
    // captures rather than stalling the tour.
    bool arrived = m_currentTime >= s.t;
    bool settled = m_terrain.pendingMeshCount() < 30;
    if (arrived && (settled || m_currentTime >= s.t + 1.2f)) {
        if (s.pitch != 0.f) m_player.rotateOnRightLocal(s.pitch - m_player.pitch());
        QImage img = grabFramebuffer();
        const QString path = dir + "/" + s.name + ".png";
        img.save(path);
        qDebug() << "shot" << s.name << img.width() << "x" << img.height()
                 << "t=" << m_currentTime;
        ++idx;
    }
}
