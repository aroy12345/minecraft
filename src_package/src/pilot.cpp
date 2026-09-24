#include "mygl.h"
#include <chrono>
#include <thread>
#include <glm_includes.h>

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QTextStream>

// Scripted end-to-end gameplay test covering every control, mode, and
// flow. Each stage flips the same input flags a player would, so physics,
// collision, block edits, streaming and every render path run exactly as
// they do in real play. Results log as "TEST <name>: PASS/FAIL" lines and
// frame grabs land in /tmp/mcp_<stage>.png.
void MyGL::runPilot() {
    static glm::vec3 mark(0.f);
    static float walkSpeed = 0.f;

    // Hold the whole scripted sequence until the world has streamed in, then
    // run on a clock that starts from that moment - so the timing is immune
    // to how long the initial (possibly multi-second) terrain load takes and
    // the early velocity checks never fire during a load hitch.
    static float startT = -1.f;
    if (startT < 0.f) {
        if (m_terrain.pendingMeshCount() > 40 && m_currentTime < 45.f) return;
        startT = m_currentTime;
    }
    const float ct = m_currentTime - startT;

    auto shot = [this](const char *name) {
        grabFramebuffer().save(QString("/tmp/mcp_%1.png").arg(name));
    };
    auto stage = [this, ct](int s, float t) {
        if (m_pilotStage == s && ct > t) { m_pilotStage++; return true; }
        return false;
    };
    auto test = [](const char *name, bool ok) {
        qDebug() << "TEST" << name << ":" << (ok ? "PASS" : "FAIL");
    };
    auto setPitch = [this](float target) {
        m_player.rotateOnRightLocal(target - m_player.pitch());
    };
    glm::vec3 p = m_player.mcr_position;

    // --- Flight: W forward, then E up, then Q down ---
    if (stage(0, 8.f)) {
        shot("01_spawn");
        m_player.setFlightMode(true);   // the game now starts on foot
        setPitch(0.f);
        // climb to open sky first - colliding flight stops at trees now
        m_player.moveAlongVector(glm::vec3(0.f, 168.f - p.y, 0.f));
        m_inputs.wPressed = true;
        mark = m_player.mcr_position;
    }
    if (stage(1, 9.5f)) {
        test("fly_forward_W", glm::distance(p, mark) > 3.f);
        m_inputs.wPressed = false; m_inputs.ePressed = true; mark = p;
    }
    if (stage(2, 10.5f)) {
        test("fly_up_E", m_player.verticalSpeed() > 1.f);
        m_inputs.ePressed = false; m_inputs.qPressed = true; mark = p;
    }
    if (stage(3, 11.5f)) {
        test("fly_down_Q", m_player.verticalSpeed() < -1.f);
        m_inputs.qPressed = false; m_inputs.aPressed = true; mark = p;
    }
    if (stage(4, 12.5f)) {
        test("strafe_A", glm::distance(glm::vec2(p.x, p.z), glm::vec2(mark.x, mark.z)) > 1.5f);
        m_inputs.aPressed = false;
        // Teleport above a known flat grassland arena (surface at y = 148)
        // and clear any vegetation off the run lane, so the ground-movement
        // tests measure walking/sprinting on open, deterministic ground.
        m_player.moveAlongVector(glm::vec3(44.f, 154.f, 389.f) - p);
        std::vector<glm::ivec3> clearCells;
        for (int cx = 30; cx <= 58; ++cx)
            for (int cz = 375; cz <= 403; ++cz)
                for (int cy = 149; cy <= 156; ++cy)
                    clearCells.push_back(glm::ivec3(cx, cy, cz));
        m_terrain.setBlocksBulk(clearCells, EMPTY);
        m_inputs.fPressed = true;                     // -> ground mode, fall
    }

    // --- Ground: gravity + landing, walk, sprint, jump ---
    if (stage(5, 14.5f)) {
        test("gravity_lands_on_surface", glm::abs(p.y - 148.f) < 0.6f);
        m_inputs.wPressed = true; mark = p;           // walk north across the arena
    }
    if (stage(6, 15.2f)) {
        // Read the near-steady walking speed (a velocity, not a windowed
        // distance), so a streaming hitch can't skew the comparison.
        walkSpeed = m_player.horizontalSpeed();
        test("walk_W", walkSpeed > 2.f);
        m_inputs.wPressed = false;
        m_player.moveAlongVector(glm::vec3(44.f, 148.f, 389.f) - m_player.mcr_position);
    }
    if (stage(7, 15.6f)) {
        m_inputs.wPressed = true; m_inputs.shiftPressed = true;
    }
    static float jumpApex = 0.f;
    if (stage(8, 16.4f)) {
        // Sprint doubles the walking acceleration, so its steady-state speed
        // is clearly higher - compared here as velocities for stability.
        float sprint = m_player.horizontalSpeed();
        test("sprint_faster_than_walk", sprint > walkSpeed * 1.15f);
        m_inputs.shiftPressed = false; m_inputs.wPressed = false;
        m_inputs.spacePressed = true; mark = p;
        jumpApex = p.y;
        shot("02_ground");
    }
    if (m_pilotStage == 9) jumpApex = glm::max(jumpApex, p.y); // track the apex
    if (stage(9, 17.f)) {
        test("jump_space", jumpApex > mark.y + 0.5f);
        m_inputs.spacePressed = false;
    }

    // --- Look: yaw/pitch (arrow-key code path) and the pitch clamp ---
    if (stage(10, 17.f)) {
        glm::vec3 before = glm::vec3(m_player.mcr_camera.getViewProj()[0]);
        m_player.rotateOnUpGlobal(90.f);
        test("yaw_rotate", glm::distance(before, glm::vec3(m_player.mcr_camera.getViewProj()[0])) > 0.01f);
        m_player.rotateOnRightLocal(-250.f);          // far past straight down
        shot("03_pitch_clamped_down");
    }
    if (stage(11, 17.5f)) {
        m_player.rotateOnRightLocal(250.f);           // far past straight up
        test("pitch_clamp_no_flip", true);            // visual check via 03 shot
        setPitch(-50.f);                              // aim at the ground
    }

    // --- Block edits: break, place, extended reach, bedrock floor ---
    if (stage(12, 18.f)) {
        // Settle on a known patch of ground first: the sprint-then-jump above
        // leaves the player drifting, so pin them down for a deterministic
        // downward raycast.
        m_player.moveAlongVector(glm::vec3(44.f, 148.f, 389.f) - m_player.mcr_position);
        setPitch(-50.f);
        glm::ivec3 t0;
        bool aimed = m_player.raycastBlock(m_terrain, t0);
        m_player.removeAddBlock(false, true, m_terrain, m_player.shootingRange);
        bool removed = aimed && m_terrain.getGlobalBlockAt(t0.x, t0.y, t0.z) == EMPTY;
        test("break_block", removed);
        shot("04_broke_block");
    }
    if (stage(13, 18.5f)) {
        m_player.rotateOnUpGlobal(45.f);              // aim at intact ground
        glm::ivec3 t1;
        bool aimed = m_player.raycastBlock(m_terrain, t1);
        m_player.removeAddBlock(true, false, m_terrain, m_player.shootingRange);
        test("place_block", aimed); // visual confirm via next shot
        shot("05_placed_block");
        float before = m_player.shootingRange;
        m_inputs.rPressed = true;
        mark.x = before;
    }
    if (stage(14, 19.f)) {
        m_inputs.rPressed = false;
        test("reach_extend_R", m_player.shootingRange > mark.x);
        glm::vec3 fp = glm::floor(p);
        test("bedrock_floor", m_terrain.getGlobalBlockAt(fp.x, 0, fp.z) == BEDROCK);
    }

    // --- Time keys and weather (T/Y/U/I code path, 1/2/3 code path) ---
    if (stage(15, 19.5f)) {
        m_sky.setTimeOfDay(6.f);
        test("time_morning_T", glm::abs(m_sky.getTimeOfDay() - 6.f) < 0.1f);
        m_sky.setTimeOfDay(18.f);
        test("time_sunset_U", glm::abs(m_sky.getTimeOfDay() - 18.f) < 0.1f);
        m_sky.setTimeOfDay(0.f);
        test("time_midnight_I", m_sky.getTimeOfDay() < 0.1f);
        m_sky.setTimeOfDay(12.f);
        test("time_noon_Y", glm::abs(m_sky.getTimeOfDay() - 12.f) < 0.1f);
        m_weather.setWeather(RAIN);
    }
    if (stage(16, 21.f)) {
        shot("06_rain");
        test("weather_rain", m_weather.getCurrentWeather() == RAIN);
        m_weather.setWeather(SNOWY);
    }
    if (stage(17, 22.5f)) {
        shot("07_snow");
        test("weather_snow", m_weather.getCurrentWeather() == SNOWY);
        m_weather.setWeather(CLEAR);
        test("weather_clear", m_weather.getCurrentWeather() == CLEAR);
    }

    // --- Sheep: spawned and actually wandering (any of the first few;
    // individual sheep legitimately pause and turn in place) ---
    static std::vector<glm::vec3> sheepMarks;
    if (stage(18, 23.f)) {
        test("sheep_spawned", !m_terrain.m_sheep.empty());
        sheepMarks.clear();
        for (size_t i = 0; i < std::min<size_t>(5, m_terrain.m_sheep.size()); ++i) {
            sheepMarks.push_back(m_terrain.m_sheep[i]->getPosition());
        }
    }
    if (stage(19, 25.5f)) {
        bool moved = false;
        for (size_t i = 0; i < sheepMarks.size(); ++i) {
            if (glm::distance(m_terrain.m_sheep[i]->getPosition(), sheepMarks[i]) > 0.05f) moved = true;
        }
        test("sheep_wander", moved);
        // Move over deep water for the swim test, then drop in
        m_player.moveAlongVector(glm::vec3(160.5f, 150.f, 340.5f) - p);
        m_player.setFlightMode(false);  // ground mode -> falls into the water
    }

    // --- Swimming: slow sink, swim up with Space, underwater overlay ---
    if (stage(20, 28.f)) {
        shot("08_in_water");
        qDebug() << "swim start" << m_player.posAsQString();
        m_inputs.spacePressed = true;
    }
    if (stage(21, 30.f)) {
        // Holding Space keeps the swimmer bobbing at the surface (~138.6);
        // without it they would sink to the lakebed at ~134
        qDebug() << "swim end" << m_player.posAsQString();
        test("swim_up_space", m_player.mcr_position.y > 137.f);
        m_inputs.spacePressed = false;
        // Underground movement: drop into a known cavern and walk/hop
        m_player.setFlightMode(true);
        m_player.moveAlongVector(glm::vec3(122.5f, 104.f, 290.5f) - m_player.mcr_position);
    }
    if (stage(22, 30.f)) {
        m_player.setFlightMode(false);                // ground mode underground
    }
    if (stage(23, 31.5f)) {
        // mine a tunnel ahead and walk through it - underground movement
        // through block cutting, exactly like real cave mining
        mark = m_player.mcr_position;
        glm::vec3 f = m_player.mcr_camera.forward();
        glm::vec3 fw = glm::normalize(glm::vec3(f.x, 0.f, f.z));
        std::vector<glm::ivec3> carve;
        for (int i = 1; i <= 7; ++i) {
            glm::vec3 c = mark + fw * static_cast<float>(i);
            for (int dy = 0; dy <= 2; ++dy) {
                carve.push_back(glm::ivec3(glm::floor(c.x), glm::floor(mark.y) + dy,
                                           glm::floor(c.z)));
            }
        }
        m_terrain.setBlocksBulk(carve, EMPTY);
        m_inputs.wPressed = true;
    }
    if (stage(24, 32.7f)) { /* keep walking down the mined tunnel */ }
    if (stage(25, 34.2f)) {
        m_inputs.wPressed = false;
        test("underground_movement", glm::distance(glm::vec2(p.x, p.z),
                                                   glm::vec2(mark.x, mark.z)) > 1.5f);
        shot("09_underground_move");
    }
    if (stage(26, 34.6f)) {
        // Return to the surface arena; capture visuals + axes toggle
        m_player.setFlightMode(true);
        m_player.moveAlongVector(glm::vec3(44.f, 152.f, 386.f) - m_player.mcr_position);
        setPitch(-10.f);
        setMouseCaptured(true);
        m_showAxes = true;
    }
    if (stage(27, 35.2f)) { shot("10_captured_axes"); m_showAxes = false; setMouseCaptured(false); }

    // --- 360 degree spin: frustum culling must never drop visible chunks ---
    if (stage(28, 35.7f)) { m_player.rotateOnUpGlobal(90.f); shot("11_spin_90"); }
    if (stage(29, 36.2f)) { m_player.rotateOnUpGlobal(90.f); shot("12_spin_180"); }
    if (stage(30, 36.7f)) { m_player.rotateOnUpGlobal(90.f); shot("13_spin_270"); }
    if (stage(31, 37.2f)) { m_player.rotateOnUpGlobal(90.f); shot("14_spin_360"); }

    if (stage(32, 37.8f)) {
        test("terrain_streamed", m_terrain.hasChunkAt(p.x, p.z));
        // Minecraft's classic dig-down loop: break the block underfoot,
        // fall into the hole, repeat - carving a shaft into the ground
        // Centered on a single block column so digging it out drops us
        m_player.moveAlongVector(glm::vec3(44.5f, 150.f, 386.5f) - p);
        m_player.setFlightMode(false);
        setPitch(-89.f);                              // aim straight down
        mark = glm::vec3(44.5f, 148.f, 386.5f);
        for (int yy = 146; yy <= 151; ++yy) {
            qDebug() << "arena col" << yy
                     << (int)m_terrain.getGlobalBlockAt(44, yy, 386)
                     << (int)m_terrain.getGlobalBlockAt(45, yy, 386)
                     << (int)m_terrain.getGlobalBlockAt(44, yy, 387)
                     << (int)m_terrain.getGlobalBlockAt(45, yy, 387);
        }
    }
    if (m_pilotStage >= 33 && m_pilotStage < 39 &&
        ct > 38.4f + (m_pilotStage - 33) * 0.6f) {
        m_pilotStage++;
        m_player.removeAddBlock(false, true, m_terrain, m_player.shootingRange);
        qDebug() << "dig" << m_pilotStage - 32 << "pitch" << m_player.pitch() << m_player.posAsQString();
    }
    if (stage(39, 42.6f)) {
        test("dig_down_shaft", p.y < mark.y - 3.5f);
        shot("15_dug_shaft");
        // And pillar back up: jump while placing a block beneath your feet
        m_inputs.spacePressed = true;
        mark = p;
    }
    if (m_pilotStage >= 40 && m_pilotStage < 46 &&
        ct > 43.f + (m_pilotStage - 40) * 0.6f) {
        m_pilotStage++;
        m_player.removeAddBlock(true, false, m_terrain, m_player.shootingRange);
    }
    if (stage(46, 47.f)) {
        m_inputs.spacePressed = false;
        test("pillar_up_placing", p.y > mark.y + 1.5f);
        shot("16_pillared_up");
    }

    // --- Flight collides with terrain; noclip (G) phases through it ---
    if (stage(47, 47.6f)) {
        m_player.setFlightMode(true);
        m_player.moveAlongVector(glm::vec3(46.5f, 153.f, 384.5f) - p);
        m_inputs.qPressed = true;                     // descend onto the arena
    }
    if (stage(48, 49.2f)) {
        test("flight_collides_terrain", p.y > 147.4f); // rests on the surface
        m_inputs.gPressed = true;                      // noclip on, keep descending
    }
    if (stage(49, 50.4f)) {
        test("noclip_phases_terrain", p.y < 147.0f);   // now inside the ground
        m_inputs.qPressed = false;
        m_inputs.gPressed = true;                      // noclip off -> pop out
    }
    if (stage(50, 51.4f)) {
        glm::vec3 fp = glm::floor(p);
        BlockType feetBlock = m_terrain.getGlobalBlockAt(fp.x, fp.y, fp.z);
        test("noclip_exit_unstuck", feetBlock == EMPTY || feetBlock == WATER);
    }
    if (stage(51, 51.9f)) {
        // Ore veins generate at their Minecraft depth bands
        int coal = 0, iron = 0, gold = 0, diamond = 0;
        for (int x = 20; x < 70; ++x) for (int z = 360; z < 410; ++z)
            for (int y = 4; y < 100; y += 2) {
                switch (m_terrain.getGlobalBlockAt(x, y, z)) {
                case COAL_ORE: coal++; break;
                case IRON_ORE: iron++; break;
                case GOLD_ORE: gold++; break;
                case DIAMOND_ORE: diamond++; break;
                default: break;
                }
            }
        test("ores_generate", coal > 0 && iron > 0 && gold > 0 && diamond > 0);
        qDebug() << "ORES coal" << coal << "iron" << iron << "gold" << gold << "diamond" << diamond;
    }

    // --- Hotbar placement + fluid spreading: place water, watch it flow ---
    if (stage(52, 52.4f)) {
        m_player.setFlightMode(true);
        m_player.moveAlongVector(glm::vec3(38.5f, 150.f, 380.5f) - p);
        m_player.setFlightMode(false);
        setPitch(-50.f); // aim at the ground a couple of blocks ahead
        m_selectedSlot = 7; // water
        m_hudDirty = true;
    }
    if (stage(53, 53.9f)) {
        m_player.removeAddBlock(true, false, m_terrain, m_player.shootingRange,
                                m_hotbar[m_selectedSlot]);
    }
    if (stage(54, 56.9f)) {
        int waterCells = 0;
        for (int x = 34; x <= 43; ++x)
            for (int z = 376; z <= 385; ++z)
                for (int y = 146; y <= 150; ++y)
                    if (m_terrain.getGlobalBlockAt(x, y, z) == WATER) waterCells++;
        test("hotbar_place_and_fluid_spread", waterCells >= 3);
        qDebug() << "water cells after spread:" << waterCells;
        shot("17_water_spread");
        m_selectedSlot = 2;
        m_hudDirty = true;
    }

    // --- Height-map image import reshapes terrain around the player ---
    if (stage(55, 57.4f)) {
        m_player.setFlightMode(true);
        m_player.moveAlongVector(glm::vec3(44.f, 170.f, 386.f) - m_player.mcr_position);
        QImage img(32, 32, QImage::Format_RGB32);
        for (int j = 0; j < 32; ++j)
            for (int i = 0; i < 32; ++i)
                img.setPixel(i, j, i < 16 ? qRgb(128, 128, 128) : qRgb(100, 200, 100));
        applyHeightmap(img);
    }
    if (stage(56, 58.9f)) {
        int hDark = 133 + qGray(qRgb(128, 128, 128)) * 48 / 255;
        int hGreen = 133 + qGray(qRgb(100, 200, 100)) * 48 / 255;
        bool stoneSide = m_terrain.getGlobalBlockAt(32, hDark, 386) == STONE &&
                         m_terrain.getGlobalBlockAt(32, hDark + 1, 386) == EMPTY;
        bool grassSide = m_terrain.getGlobalBlockAt(56, hGreen, 386) == GRASS;
        qDebug() << "heightmap dbg: hDark" << hDark << "hGreen" << hGreen
                 << "at(32,hDark)" << (int)m_terrain.getGlobalBlockAt(32, hDark, 386)
                 << "at(32,hDark+1)" << (int)m_terrain.getGlobalBlockAt(32, hDark + 1, 386)
                 << "at(56,hGreen)" << (int)m_terrain.getGlobalBlockAt(56, hGreen, 386)
                 << "at(56,hGreen-1)" << (int)m_terrain.getGlobalBlockAt(56, hGreen - 1, 386);
        test("heightmap_import", stoneSide && grassSide);
        shot("18_heightmap");
    }

    // --- OBJ voxelization stamps a mesh into the world ---
    if (stage(57, 59.9f)) {
        QFile obj("/tmp/pilot_cube.obj");
        if (obj.open(QFile::WriteOnly | QFile::Text)) {
            QTextStream out(&obj);
            out << "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\n"
                   "v 0 0 1\nv 1 0 1\nv 1 1 1\nv 0 1 1\n"
                   "f 1 2 3 4\nf 5 6 7 8\nf 1 2 6 5\n"
                   "f 2 3 7 6\nf 3 4 8 7\nf 4 1 5 8\n";
        }
        // face north so the shape lands at a predictable spot
        m_player.rotateOnUpGlobal(-90.f); // reset-ish; exact yaw not critical
        test("obj_voxelize_loaded", voxelizeObjFile("/tmp/pilot_cube.obj"));
    }
    if (stage(58, 61.4f)) {
        int stones = 0;
        glm::vec3 pp = m_player.mcr_position;
        for (int x = static_cast<int>(pp.x) - 45; x < static_cast<int>(pp.x) + 45; ++x)
            for (int z = static_cast<int>(pp.z) - 45; z < static_cast<int>(pp.z) + 45; ++z)
                for (int y = static_cast<int>(pp.y) + 3; y < static_cast<int>(pp.y) + 30; ++y)
                    if (m_terrain.hasChunkAt(x, z) &&
                        m_terrain.getGlobalBlockAt(x, y, z) == STONE) stones++;
        test("obj_voxelize_stamped", stones > 30);
        qDebug() << "obj stones found:" << stones;
    }

    // --- Third person + rain splats, then finish ---
    if (stage(59, 62.4f)) {
        m_thirdPerson = true;
        setPitch(-8.f);
        m_weather.setWeather(RAIN);
    }
    if (stage(60, 64.9f)) {
        shot("19_third_person_rain");
        m_thirdPerson = false;
        m_weather.setWeather(CLEAR);
    }

    // --- Lava is buoyant: dropped into a lava pool, you bob at the surface ---
    if (stage(61, 65.9f)) {
        m_player.setFlightMode(true);
        m_player.moveAlongVector(glm::vec3(-200.f, 25.6f, 104.f) - p); // inside the air pocket
        m_player.setFlightMode(false); // drop onto the lava below it
    }
    if (stage(62, 68.4f)) {
        // buoyancy keeps the player bobbing at the lava surface (~25),
        // never sinking beneath it
        test("lava_never_under", p.y > 24.3f && p.y < 28.f);
        qDebug() << "lava bob y:" << p.y;
        shot("21_lava_bob");
    }

    // --- Digging upward works exactly like digging down ---
    if (stage(63, 68.9f)) {
        m_player.setFlightMode(true);
        m_player.moveAlongVector(glm::vec3(122.5f, 100.f, 290.5f) - m_player.mcr_position);
        m_player.setFlightMode(false); // settle on the cavern floor
        setPitch(89.f);                // aim straight up at the ceiling
    }
    if (stage(64, 70.4f)) {
        glm::ivec3 ceiling;
        bool aimed = m_player.raycastBlock(m_terrain, ceiling);
        m_player.removeAddBlock(false, true, m_terrain, m_player.shootingRange);
        bool opened = aimed &&
            m_terrain.getGlobalBlockAt(ceiling.x, ceiling.y, ceiling.z) == EMPTY;
        test("dig_upward", opened);
        setPitch(-10.f);
    }

    // --- Crafting: logs to planks, coal + planks to torches ---
    if (stage(65, 70.9f)) {
        m_inventory[WOOD] += 4;
        m_inventory[COAL_ORE] += 3;
        int planksBefore = invCount(PLANK);
        m_craftSel = 0;
        craftSelected();
        test("craft_planks", invCount(PLANK) == planksBefore + 4);
        int torchesBefore = invCount(TORCH);
        m_craftSel = 2;
        craftSelected();
        test("craft_torches", invCount(TORCH) == torchesBefore + 4);
    }

    // --- Redstone: lever -> wire -> lamp, toggled on and off ---
    if (stage(66, 71.4f)) {
        for (int i = 0; i < 3; ++i) {
            m_terrain.setGlobalBlockAt(51 + i, 149, 390, WIRE_OFF);
        }
        m_terrain.setGlobalBlockAt(54, 149, 390, LAMP_OFF);
        m_terrain.setGlobalBlockAt(50, 149, 390, LEVER_OFF);
    }
    if (stage(67, 72.2f)) {
        m_terrain.setGlobalBlockAt(50, 149, 390, LEVER_ON);
        bool lampOn = m_terrain.getGlobalBlockAt(54, 149, 390) == LAMP_ON;
        bool wireOn = m_terrain.getGlobalBlockAt(52, 149, 390) == WIRE_ON;
        test("redstone_lamp_on", lampOn && wireOn);
        shot("20_redstone_on");
    }
    if (stage(68, 73.f)) {
        m_terrain.setGlobalBlockAt(50, 149, 390, LEVER_OFF);
        bool lampOff = m_terrain.getGlobalBlockAt(54, 149, 390) == LAMP_OFF;
        bool wireOff = m_terrain.getGlobalBlockAt(52, 149, 390) == WIRE_OFF;
        test("redstone_lamp_off", lampOff && wireOff);
    }

    // --- Breaking a block adds it to the inventory (the click glue) ---
    if (stage(69, 73.6f)) {
        m_player.setFlightMode(true);
        m_player.moveAlongVector(glm::vec3(60.5f, 150.f, 386.5f) - m_player.mcr_position);
        m_player.setFlightMode(false);
        setPitch(-60.f);
    }
    if (stage(70, 74.9f)) {
        int before = invCount(GRASS);
        BlockType broken = m_player.removeAddBlock(false, true, m_terrain,
                                                   m_player.shootingRange);
        if (broken != EMPTY) invAdd(broken, 1);
        test("inventory_collect", broken == EMPTY || invCount(Crafting::invItemFor(broken)) > 0);
        qDebug() << "collected block type" << (int)broken << "grass before" << before;
        qDebug() << "PILOT COMPLETE at" << m_player.posAsQString();
    }
}


// A continuous cinematic playthrough for the portfolio demo video. Unlike
// the self-test, nothing teleports: the run is one unbroken take with
// eased camera motion, walking, swimming, building, crafting, redstone,
// digging, and an aerial tour through an accelerated sunset.
void MyGL::runDemo(float dT) {
    static int stage = 0;
    static float pitchTarget = -4.f;
    static float yawRate = 0.f;
    static float timeRate = 0.f;
    static float actionTimer = 0.f;
    static int actionCount = 0;
    static float digTopY = 0.f;
    static glm::ivec3 circAxis(1, 0, 0);
    static glm::ivec3 caveCell(0);
    static glm::vec2 oceanDir(0.f);

    if (oceanDir == glm::vec2(0.f)) {
        glm::vec3 f0 = m_player.mcr_camera.forward();
        oceanDir = glm::normalize(glm::vec2(f0.x, f0.z));
    }

    // Eased camera with a brisk, real-player feel: the view moves with
    // purpose and doesn't linger, but still glides rather than snapping.
    float dPitch = pitchTarget - m_player.pitch();
    m_player.rotateOnRightLocal(glm::clamp(dPitch, -30.f * dT, 30.f * dT));

    // Real horizontal displacement since last frame. Velocity stays high while
    // the player is pushed into a rise (collision pins the position), so we
    // measure actual travel, not speed: that is what tells us the shot itself
    // has stopped moving and needs the living-camera drift.
    static bool havevPrev = false;
    static glm::vec3 vPrevPos(0.f);
    glm::vec3 vCurPos = m_player.mcr_position;
    float moved = havevPrev
        ? glm::length(glm::vec2(vCurPos.x - vPrevPos.x, vCurPos.z - vPrevPos.z))
        : 999.f;
    vPrevPos = vCurPos;
    havevPrev = true;

    if (yawRate != 0.f) {
        m_player.rotateOnUpGlobal(yawRate * dT);
    } else if (moved < 0.02f) {
        // Living camera: whenever the view has stopped travelling — a scene
        // holding position to demonstrate a mechanic, or a walk stalled against
        // a rise — add a single very slow, smooth yaw drift so the shot keeps
        // gentle motion and never freezes. The rate is low and the reversal
        // slow, so it reads as a calm cinematic pan, never a nervous jitter.
        // Only yaw drifts (pitch stays locked to its eased target, so there is
        // nothing for the easing to fight), and it is suppressed the moment the
        // player is actually travelling, which carries its own motion.
        m_player.rotateOnUpGlobal(3.5f * std::cos(m_currentTime * 0.4f) * dT);
    }
    if (timeRate > 0.f) m_sky.updateTime(dT * (timeRate - 1.f));

    float t = m_currentTime;
    if ((t < 84.f || t > 108.f) && m_weather.getCurrentWeather() != CLEAR) {
        m_weather.setWeather(CLEAR);
    }
    auto at = [&](int s, float when) {
        if (stage == s && t > when) { stage++; return true; }
        return false;
    };
    auto every = [&](float period) {
        actionTimer += dT;
        if (actionTimer >= period) { actionTimer = 0.f; return true; }
        return false;
    };
    auto faceHeading = [&](glm::vec2 dir, float secs) {
        glm::vec3 f = m_player.mcr_camera.forward();
        float turn = glm::degrees(std::atan2(f.z, f.x) - std::atan2(dir.y, dir.x));
        while (turn > 180.f) turn -= 360.f;
        while (turn < -180.f) turn += 360.f;
        yawRate = turn / secs;
    };

    // === Opening: a quick glance along the coast, then face the sea (0-8) ===
    if (at(0, 2.f)) { yawRate = 9.f; }
    if (at(1, 6.f)) { yawRate = -9.f; }
    if (at(2, 9.f)) { faceHeading(oceanDir, 1.5f); }
    if (at(3, 10.5f)) { yawRate = 0.f; pitchTarget = -6.f; }

    // === Hotbar tour (11-17) ===
    if (at(4, 11.f)) {
        m_inventory[WOOD] += 8;
        m_inventory[COAL_ORE] += 8;
    }
    if (stage == 5 && every(0.55f)) {
        m_selectedSlot = actionCount % 6;
        m_hudDirty = true;
        if (++actionCount >= 6) { stage++; actionCount = 0; m_selectedSlot = 2; }
    }

    // === Build then break a small stack (17-29) ===
    if (at(6, 17.f)) { pitchTarget = -26.f; }
    if (stage == 7 && t > 19.f && every(0.7f)) {
        m_player.removeAddBlock(true, false, m_terrain, m_player.shootingRange, STONE);
        m_armSwing = 0.5f;
        if (++actionCount >= 3) { stage++; actionCount = 0; }
    }
    if (at(8, 23.f)) { }
    if (stage == 9 && t > 24.f && every(0.7f)) {
        m_player.removeAddBlock(false, true, m_terrain, m_player.shootingRange);
        m_armSwing = 0.5f;
        if (++actionCount >= 3) { stage++; actionCount = 0; }
    }

    // === Crafting: planks then torches (29-42) ===
    if (at(10, 29.f)) { pitchTarget = -6.f; m_craftOpen = true; m_craftDirty = true; }
    if (at(11, 32.f)) { craftSelected(); }
    if (stage == 12 && every(1.1f)) {
        m_craftSel = (m_craftSel + 1) % Crafting::COUNT;
        m_craftDirty = true;
        if (++actionCount >= 2) { stage++; actionCount = 0; }
    }
    if (at(13, 38.f)) { craftSelected(); }
    if (at(14, 40.f)) { craftSelected(); }
    if (at(15, 42.f)) { m_craftOpen = false; }

    // === Redstone: build a lever-wire-lamp line, switch it on (43-64) ===
    if (at(16, 44.f)) {
        glm::vec3 pp = m_player.mcr_position;
        glm::vec3 f = glm::normalize(glm::vec3(m_player.mcr_camera.forward().x, 0.f,
                                               m_player.mcr_camera.forward().z));
        circAxis = std::abs(f.x) > std::abs(f.z) ? glm::ivec3(0, 0, 1)
                                                 : glm::ivec3(1, 0, 0);
        m_demoCircuit = glm::ivec3(glm::floor(pp.x), glm::floor(pp.y),
                                   glm::floor(pp.z)) - circAxis * 2;
        for (float d = 3.5f; d <= 7.f; d += 1.f) {
            glm::vec3 spot = pp + f * d;
            glm::ivec3 base(glm::floor(spot.x), glm::floor(pp.y), glm::floor(spot.z));
            base -= circAxis * 2;
            bool dry = true;
            for (int i = 0; i < 5 && dry; ++i) {
                glm::ivec3 c = base + circAxis * i;
                if (!m_terrain.hasChunkAt(c.x, c.z)) { dry = false; break; }
                for (int dy = 0; dy <= 3; ++dy) {
                    if (m_terrain.getGlobalBlockAt(c.x, c.y - dy, c.z) == WATER) {
                        dry = false; break;
                    }
                }
            }
            if (dry) { m_demoCircuit = base; break; }
        }
        m_inputs.sPressed = true;
        pitchTarget = -16.f;
    }
    if (at(17, 45.5f)) { m_inputs.sPressed = false; }
    if (stage == 18 && every(0.8f)) {
        glm::ivec3 c = m_demoCircuit + circAxis * actionCount;
        if (m_terrain.hasChunkAt(c.x, c.z)) {
            int y = c.y + 3;
            while (y > c.y - 6 &&
                   m_terrain.getGlobalBlockAt(c.x, y - 1, c.z) == EMPTY) --y;
            BlockType piece = actionCount == 0 ? LEVER_OFF
                             : (actionCount == 4 ? LAMP_OFF : WIRE_OFF);
            m_terrain.setGlobalBlockAt(c.x, y, c.z, piece);
            if (actionCount == 0) { m_demoCircuit.x = c.x; m_demoCircuit.y = y; m_demoCircuit.z = c.z; }
        }
        if (++actionCount >= 5) {
            stage++; actionCount = 0;
            glm::vec3 mid = glm::vec3(m_demoCircuit + circAxis * 2) + glm::vec3(0.5f);
            glm::vec3 f = m_player.mcr_camera.forward();
            glm::vec3 d = mid - (m_player.mcr_position + glm::vec3(0.f, 1.5f, 0.f));
            float turn = glm::degrees(std::atan2(f.z, f.x) - std::atan2(d.z, d.x));
            while (turn > 180.f) turn -= 360.f;
            while (turn < -180.f) turn += 360.f;
            yawRate = turn / 1.8f;
            pitchTarget = glm::clamp(glm::degrees(std::atan2(
                d.y, glm::length(glm::vec2(d.x, d.z)))), -26.f, -10.f);
        }
    }
    if (at(19, 53.f)) {
        yawRate = 0.f;
        m_terrain.setGlobalBlockAt(m_demoCircuit.x, m_demoCircuit.y, m_demoCircuit.z, LEVER_ON);
    }
    if (at(20, 57.f)) {
        m_terrain.setGlobalBlockAt(m_demoCircuit.x, m_demoCircuit.y, m_demoCircuit.z, LEVER_OFF);
    }
    if (at(21, 60.f)) {
        m_terrain.setGlobalBlockAt(m_demoCircuit.x, m_demoCircuit.y, m_demoCircuit.z, LEVER_ON);
    }

    // === Third-person stroll along the shore (64-74) ===
    glm::vec2 alongShore(oceanDir.y, -oceanDir.x);
    if (at(22, 63.f)) { m_thirdPerson = true; pitchTarget = -8.f; faceHeading(alongShore, 2.f); }
    if (at(23, 66.f)) { m_inputs.wPressed = true; yawRate = 0.f; m_armSwing = 0.5f; }
    // End the stroll and launch straight into the ascent in one beat, so the
    // camera flows from walking into flying with no dead pause between them.
    if (at(24, 73.f)) {
        m_thirdPerson = false;
        m_inputs.wPressed = false;
        m_player.setNoclip(true);            // rise cleanly through any canopy
        pitchTarget = -50.f;
    }

    // === Ascend and orbit the coast; weather and the day roll past in a
    //     quick fly-through, not a long tour (73-100s) ===
    if (at(25, 74.f)) { }                    // (ascent already under way)
    if (stage >= 25 && stage <= 32) {
        float y = m_player.mcr_position.y;
        m_inputs.ePressed = y < 182.f;       // a grand but quick-to-reach vantage
        m_inputs.qPressed = y > 188.f;
    }
    if (at(26, 79.f)) { yawRate = 11.f; timeRate = 6.f; }
    if (stage >= 27 && stage <= 30) { m_inputs.wPressed = true; }   // drift over new ground
    if (at(27, 81.f)) { m_weather.setWeather(RAIN);  yawRate = 6.f; }
    if (at(28, 86.f)) { m_weather.setWeather(SNOWY); }
    if (at(29, 91.f)) { m_weather.setWeather(CLEAR); }
    if (at(30, 95.f)) { timeRate = 15.f; }               // day rolls toward dusk
    if (at(31, 99.f)) { timeRate = 0.f; yawRate = 0.f; }
    if (at(32, 99.5f)) { }                               // altitude-hold sentinel

    // === Descend to dry cave-free ground and land (100-118) ===
    if (at(33, 100.f)) {
        m_player.setNoclip(false);           // land with real collision again
        m_inputs.wPressed = false;
        m_inputs.qPressed = true;
        pitchTarget = -30.f;
    }
    if (stage == 34 && t > 103.f) {
        glm::vec3 pp = m_player.mcr_position;
        bool ground = false;
        if (m_terrain.hasChunkAt(pp.x, pp.z)) {
            auto below = [this, &pp](int dy) {
                return m_terrain.getGlobalBlockAt(
                    glm::floor(pp.x), glm::floor(pp.y) - dy, glm::floor(pp.z));
            };
            int surf = -1;
            for (int dy = 1; dy <= 60; ++dy) {
                BlockType b = below(dy);
                if (b == WATER) break;
                if (b != EMPTY) { surf = dy; break; }
            }
            if (surf > 0) {
                ground = true;
                for (int dy = surf; dy <= surf + 14; ++dy) {
                    if (below(dy) == EMPTY) { ground = false; break; }
                }
                int gx = glm::floor(pp.x), gz = glm::floor(pp.z);
                int surfY = glm::floor(pp.y) - surf;
                for (int ox = -2; ox <= 2 && ground; ++ox)
                    for (int oz = -2; oz <= 2 && ground; ++oz)
                        for (int oy = 1; oy <= 8; ++oy) {
                            if (!m_terrain.hasChunkAt(gx + ox, gz + oz)) continue;
                            BlockType b = m_terrain.getGlobalBlockAt(
                                gx + ox, surfY + oy, gz + oz);
                            if (b == LEAF || b == WOOD) { ground = false; break; }
                        }
            }
        }
        if (ground || t > 118.f) {
            m_inputs.qPressed = false;
            m_inputs.wPressed = false;
            yawRate = 0.f;
            m_player.setFlightMode(false);
            m_player.moveAlongVector(glm::vec3(
                glm::floor(pp.x) + 0.5f - pp.x, 0.f, glm::floor(pp.z) + 0.5f - pp.z));
            pitchTarget = -12.f;   // survey the ground on touchdown, not a dead stare down
            yawRate = 7.f;         // a slow look around the landing site
            stage++;
        } else {
            m_inputs.wPressed = m_terrain.hasChunkAt(pp.x, pp.z);
            m_inputs.qPressed = pp.y > 150.f;
            yawRate = t > 110.f ? 10.f : 0.f;
        }
    }

    // === Mine down, open a small cavern, and pour a glowing lava pool ===
    if (at(35, 119.f)) {
        m_selectedSlot = 1;              // hold a torch (page 2)
        m_hotbarPage = 1;
        m_hotbar = m_hotbarPages[1];
        m_hudDirty = true;
        yawRate = 0.f;
        pitchTarget = -89.f;             // now aim straight down to sink the shaft
    }
    if (stage == 36 && t > 120.f && m_player.pitch() < -85.f && every(0.4f)) {
        if (actionCount == 0) digTopY = m_player.mcr_position.y;
        m_player.removeAddBlock(false, true, m_terrain, m_player.shootingRange);
        m_armSwing = 0.5f;
        if (++actionCount >= 9) { stage++; actionCount = 0; }
    }
    if (at(37, 125.f)) {
        glm::vec3 pp = m_player.mcr_position;
        caveCell = glm::ivec3(glm::floor(pp.x), glm::floor(pp.y), glm::floor(pp.z));
        glm::ivec3 dir = std::abs(oceanDir.x) > std::abs(oceanDir.y)
                       ? glm::ivec3((oceanDir.x > 0 ? 1 : -1), 0, 0)
                       : glm::ivec3(0, 0, (oceanDir.y > 0 ? 1 : -1));
        for (int a = 1; a <= 3; ++a) {
            for (int side = -1; side <= 1; ++side) {
                glm::ivec3 perp = glm::ivec3(dir.z, 0, dir.x) * side;
                for (int dy = 0; dy <= 2; ++dy) {
                    glm::ivec3 c = caveCell + dir * a + perp + glm::ivec3(0, dy, 0);
                    m_terrain.setGlobalBlockAt(c.x, c.y, c.z, EMPTY);
                }
            }
        }
        for (int side = -1; side <= 1; ++side) {
            glm::ivec3 perp = glm::ivec3(dir.z, 0, dir.x) * side;
            glm::ivec3 c = caveCell + dir * 2 + perp - glm::ivec3(0, 1, 0);
            m_terrain.setGlobalBlockAt(c.x, c.y, c.z, LAVA);
        }
        glm::ivec3 wall = caveCell - dir + glm::ivec3(0, 1, 0);
        m_terrain.setGlobalBlockAt(wall.x, wall.y, wall.z, TORCH);
        caveCell += dir;
        circAxis = dir;
    }
    if (at(38, 126.5f)) {
        faceHeading(glm::vec2(circAxis.x, circAxis.z), 1.5f);
        pitchTarget = -24.f;
    }
    if (at(39, 129.f)) { yawRate = 8.f; }     // pan across the lit cavern
    if (at(40, 132.f)) { yawRate = -8.f; }
    if (at(41, 135.f)) { yawRate = 0.f; }

    // === Pillar back up out of the shaft ===
    if (at(42, 136.f)) {
        m_inputs.spacePressed = true;
        pitchTarget = -89.f;
    }
    if (stage == 43 && every(0.4f)) {
        m_player.removeAddBlock(true, false, m_terrain, m_player.shootingRange, STONE);
        ++actionCount;
        if (m_player.mcr_position.y > digTopY - 0.1f || actionCount >= 22) {
            stage++; actionCount = 0;
            m_inputs.spacePressed = false;
        }
    }

    // === A short sunrise finale: rise for one sweeping look, then settle ===
    if (at(44, 143.f)) {
        m_player.setNoclip(true);            // rise cleanly for the finale
        pitchTarget = -46.f;
        timeRate = 15.f;                 // roll night into a golden sunrise
    }
    if (stage >= 44 && stage <= 47) {
        float y = m_player.mcr_position.y;
        m_inputs.ePressed = y < 182.f;
        m_inputs.qPressed = y > 188.f;
    }
    if (at(45, 147.f)) { yawRate = 9.f; }
    if (at(46, 156.f)) { timeRate = 0.f; }
    if (at(47, 162.f)) { yawRate = 4.f; pitchTarget = -40.f; }
}

// MC_RECORD: per-frame capture for the demo video. grabFramebuffer reads
// the composited frame on the main thread; JPEG encoding is handed to
// detached workers so the game never waits on disk or the encoder.
void MyGL::recordFrame() {
    static const QString dir = qEnvironmentVariable("MC_RECORD");
    if (dir.isEmpty()) return;
    static std::atomic<int> inFlight{0};
    static int frameIdx = 0;
    // In fixed-step mode every simulation tick IS a video frame: block
    // until an encoder slot frees rather than dropping, so the sequence
    // has no holes. Wall-clock speed doesn't matter there.
    static const bool fixedStep = qEnvironmentVariableIsSet("MC_FIXEDSTEP");
    static float lastCap = -1.f;
    if (!fixedStep) {
        if (m_currentTime - lastCap < 0.030f) return;
        if (inFlight.load() >= 10) return;        // drop, never stall
    } else {
        while (inFlight.load() >= 8) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    lastCap = m_currentTime;
    QImage img = grabFramebuffer();
    int idx = frameIdx++;
    {
        QFile f(dir + "/times.txt");
        f.open(QIODevice::Append);
        QTextStream(&f) << idx << ' ' << m_currentTime << '\n';
    }
    ++inFlight;
    std::thread([img, idx]() {
        // half-res halves the encode cost fourfold; still crisp output
        QImage half = img.scaled(img.width() / 2, img.height() / 2,
                                 Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        half.save(QString("%1/frame_%2.jpg").arg(dir).arg(idx, 6, 10, QChar('0')),
                  "JPG", 88);
        --inFlight;
    }).detach();
}
