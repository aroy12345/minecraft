#include "mygl.h"
#include <glm_includes.h>

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QKeyEvent>

void MyGL::keyPressEvent(QKeyEvent *e) {
    // Held keys are tracked as state; auto-repeat events would only
    // re-toggle things like flight mode
    if (e->isAutoRepeat()) return;

    float amount = 2.0f;
    if(e->modifiers() & Qt::ShiftModifier){
        amount = 10.0f;
    }
    // While the crafting menu is open, arrows navigate it and Enter crafts
    if (m_craftOpen) {
        if (e->key() == Qt::Key_Up) {
            m_craftSel = (m_craftSel + Crafting::COUNT - 1) % Crafting::COUNT;
            m_craftDirty = true;
            return;
        } else if (e->key() == Qt::Key_Down) {
            m_craftSel = (m_craftSel + 1) % Crafting::COUNT;
            m_craftDirty = true;
            return;
        } else if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
            craftSelected();
            return;
        } else if (e->key() == Qt::Key_B || e->key() == Qt::Key_Escape) {
            m_craftOpen = false;
            return;
        }
    }
    if (e->key() == Qt::Key_B) {
        m_craftOpen = true;
        m_craftDirty = true;
    } else if (e->key() == Qt::Key_Tab) {
        m_hotbarPage = (m_hotbarPage + 1) % HOTBAR_PAGES;
        m_hotbar = m_hotbarPages[m_hotbarPage];
        m_hudDirty = true;
    } else if (e->key() == Qt::Key_Escape) {
        // First Escape frees the mouse; a second one quits
        if (m_mouseCaptured) {
            setMouseCaptured(false);
        } else {
            QApplication::quit();
        }
    } else if (e->key() == Qt::Key_Shift) {
        m_inputs.shiftPressed = true;
    } else if (e->key() == Qt::Key_O) {
        m_showAxes = !m_showAxes;
    } else if (e->key() == Qt::Key_V) {
        m_thirdPerson = !m_thirdPerson;
    } else if (e->key() == Qt::Key_Right) {
        m_player.rotateOnUpGlobal(-amount);
    } else if (e->key() == Qt::Key_Left) {
        m_player.rotateOnUpGlobal(amount);
    } else if (e->key() == Qt::Key_Up) {
        m_player.rotateOnRightLocal(-amount);
    } else if (e->key() == Qt::Key_Down) {
        m_player.rotateOnRightLocal(amount);
    } else if (e->key() == Qt::Key_W) {
        m_inputs.wPressed = true;
    } else if (e->key() == Qt::Key_S) {
        m_inputs.sPressed = true;
    } else if (e->key() == Qt::Key_D) {
        m_inputs.dPressed = true;
    } else if (e->key() == Qt::Key_A) {
        m_inputs.aPressed = true;
    } else if (e->key() == Qt::Key_Q) {
        m_inputs.qPressed = true;
    } else if (e->key() == Qt::Key_E) {
        m_inputs.ePressed = true;
    } else if (e->key() == Qt::Key_F) {
        m_inputs.fPressed = true;
    } else if (e->key() == Qt::Key_G) {
        m_inputs.gPressed = true;
    } else if (e->key() == Qt::Key_Space) {
        m_inputs.spacePressed = true;
    } else if (e->key() == Qt::Key_R){
        m_inputs.rPressed = true;
    } else if (e->key() == Qt::Key_T) {
        // Set time to morning when T is pressed
        m_sky.setTimeOfDay(6.0f);
    } else if (e->key() == Qt::Key_Y) {
        // Set time to noon when Y is pressed
        m_sky.setTimeOfDay(12.0f);
    } else if (e->key() == Qt::Key_U) {
        // Set time to sunset when U is pressed
        m_sky.setTimeOfDay(18.0f);
    } else if (e->key() == Qt::Key_I) {
        // Set time to midnight when I is pressed
        m_sky.setTimeOfDay(0.0f);
    } else if (e->key() >= Qt::Key_1 && e->key() <= Qt::Key_8) {
        // Hotbar selection
        m_selectedSlot = e->key() - Qt::Key_1;
        m_hudDirty = true;
    } else if (e->key() == Qt::Key_Z) {
        m_weather.setWeather(CLEAR);
    } else if (e->key() == Qt::Key_X) {
        m_weather.setWeather(RAIN);
    } else if (e->key() == Qt::Key_C) {
        m_weather.setWeather(SNOWY);
    } else if (e->key() == Qt::Key_H) {
        importHeightmapDialog();
    } else if (e->key() == Qt::Key_J) {
        voxelizeObjDialog();
    } else if (e->key() == Qt::Key_P) {
        // Save a screenshot of the current frame to the desktop
        QString path = QDir::homePath() + QString("/Desktop/minecraft_screenshot_%1.png")
                       .arg(QDateTime::currentMSecsSinceEpoch());
        grabFramebuffer().save(path);
    }
}

void MyGL::keyReleaseEvent(QKeyEvent *e) {
    if (e->isAutoRepeat()) return;
    switch(e->key()) {
    case Qt::Key_W: m_inputs.wPressed = false; break;
    case Qt::Key_S: m_inputs.sPressed = false; break;
    case Qt::Key_D: m_inputs.dPressed = false; break;
    case Qt::Key_A: m_inputs.aPressed = false; break;
    case Qt::Key_Space: m_inputs.spacePressed = false; break;
    case Qt::Key_Shift: m_inputs.shiftPressed = false; break;
    case Qt::Key_E: m_inputs.ePressed = false; break;
    case Qt::Key_Q: m_inputs.qPressed = false; break;
    case Qt::Key_F: m_inputs.fPressed = false; break;
    case Qt::Key_G: m_inputs.gPressed = false; break;
    case Qt::Key_R: m_inputs.rPressed = false; break;
    }
}

void MyGL::mouseMoveEvent(QMouseEvent *e) {
#ifdef Q_OS_MACOS
    // Captured-mouse look is handled with raw deltas in tick()
    Q_UNUSED(e);
#else
    // The mouse only drives the camera while captured; otherwise it is a
    // normal desktop cursor
    if (!m_mouseCaptured || !isActiveWindow()) return;
    QPoint center(width() / 2, height() / 2);
    QPoint delta = e->pos() - center;
    float sensitivity = 0.1f;
    m_player.rotateOnUpGlobal(-delta.x() * sensitivity);
    m_player.rotateOnRightLocal(-delta.y() * sensitivity);
    moveMouseToCenter();
#endif
}

void MyGL::mousePressEvent(QMouseEvent *e) {
    // The first click captures the mouse for camera look; while captured,
    // clicks break and place blocks
    if (!m_mouseCaptured) {
        setMouseCaptured(true);
        return;
    }
    makeCurrent(); // block edits re-upload chunk VBOs immediately
    m_armSwing = 0.3f;
    if (e->button() == Qt::RightButton) {
        BlockType held = m_hotbar[m_selectedSlot];
        if (invCount(held) != 0) { // -1 means infinite
            BlockType did = m_player.removeAddBlock(true, false, m_terrain,
                                                    m_player.shootingRange, held);
            if (did == held) invTake(held, 1); // lever toggles don't consume
        }
    } else if (e->button() == Qt::LeftButton) {
        m_inputs.leftpressed = true;
        BlockType broken = m_player.removeAddBlock(false, true, m_terrain,
                                                   m_player.shootingRange);
        if (broken != EMPTY) invAdd(broken, 1);
    }
    update();
}

void MyGL::mouseReleaseEvent(QMouseEvent *e) {
    if (e->button() == Qt::LeftButton) {
        m_inputs.leftpressed = false;
    }
}
