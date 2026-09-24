#include "mygl.h"
#include <glm_includes.h>

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QTextStream>

// Height-map import (Milestone 3): an image reshapes the terrain around
// the player - pixel brightness sets the column height and pixel color
// picks the surface block (greyscale images simply read as stone).
void MyGL::applyHeightmap(const QImage &image) {
    QImage img = image.convertToFormat(QImage::Format_RGB32);
    if (img.width() > 96 || img.height() > 96) {
        img = img.scaled(96, 96, Qt::KeepAspectRatio);
    }
    struct Entry { BlockType b; int r, g, bl; };
    static const Entry palette[] = {
        {GRASS, 96, 192, 96}, {DIRT, 134, 96, 67}, {STONE, 128, 128, 128},
        {SAND, 219, 211, 160}, {SNOW, 240, 240, 240}, {WATER, 60, 100, 220},
        {WOOD, 102, 81, 49}, {LAVA, 245, 90, 20},
    };
    int x0 = static_cast<int>(glm::floor(m_player.mcr_position.x)) - img.width() / 2;
    int z0 = static_cast<int>(glm::floor(m_player.mcr_position.z)) - img.height() / 2;
    for (int j = 0; j < img.height(); ++j) {
        for (int i = 0; i < img.width(); ++i) {
            QRgb px = img.pixel(i, j);
            int h = 133 + qGray(px) * 48 / 255;
            BlockType best = STONE;
            int bestD = INT_MAX;
            for (const Entry &e : palette) {
                int d = (qRed(px) - e.r) * (qRed(px) - e.r) +
                        (qGreen(px) - e.g) * (qGreen(px) - e.g) +
                        (qBlue(px) - e.bl) * (qBlue(px) - e.bl);
                if (d < bestD) { bestD = d; best = e.b; }
            }
            m_terrain.setColumn(x0 + i, z0 + j, h, best);
        }
    }
}

void MyGL::importHeightmapDialog() {
    setMouseCaptured(false);
    QString f = QFileDialog::getOpenFileName(this, "Height map image", QDir::homePath(),
                                             "Images (*.png *.jpg *.jpeg *.bmp)");
    if (f.isEmpty()) return;
    QImage img(f);
    if (!img.isNull()) {
        makeCurrent();
        applyHeightmap(img);
    }
}

// OBJ voxelization (Milestone 3): the mesh surface is point-sampled per
// triangle and stamped into the world as stone, scaled to ~28 blocks and
// placed a short distance ahead of the player.
bool MyGL::voxelizeObjFile(const QString &path) {
    QFile file(path);
    if (!file.open(QFile::ReadOnly | QFile::Text)) return false;
    std::vector<glm::vec3> verts;
    std::vector<glm::ivec3> tris;
    QTextStream in(&file);
    while (!in.atEnd()) {
        QStringList tok = in.readLine().split(' ', Qt::SkipEmptyParts);
        if (tok.isEmpty()) continue;
        if (tok[0] == "v" && tok.size() >= 4) {
            verts.push_back(glm::vec3(tok[1].toFloat(), tok[2].toFloat(), tok[3].toFloat()));
        } else if (tok[0] == "f" && tok.size() >= 4) {
            auto idx = [&](int t) {
                int i = tok[t].split('/')[0].toInt();
                return i > 0 ? i - 1 : static_cast<int>(verts.size()) + i;
            };
            for (int t = 3; t < tok.size(); ++t) { // triangle fan
                tris.push_back(glm::ivec3(idx(1), idx(t - 1), idx(t)));
            }
        }
    }
    if (verts.empty() || tris.empty()) return false;

    glm::vec3 lo(1e9f), hi(-1e9f);
    for (const glm::vec3 &v : verts) { lo = glm::min(lo, v); hi = glm::max(hi, v); }
    glm::vec3 extent = glm::max(hi - lo, glm::vec3(1e-5f));
    float scale = 28.f / glm::max(extent.x, glm::max(extent.y, extent.z));
    glm::vec3 fwdRaw(m_player.mcr_camera.forward().x, 0.f, m_player.mcr_camera.forward().z);
    glm::vec3 fwd = glm::length(fwdRaw) > 0.1f ? glm::normalize(fwdRaw)
                                               : glm::vec3(0.f, 0.f, -1.f);
    glm::vec3 origin = m_player.mcr_position + fwd * (extent.x * scale * 0.5f + 8.f)
                       - (lo + extent * 0.5f) * scale;
    origin.y = m_player.mcr_position.y - lo.y * scale;

    std::vector<glm::ivec3> cells;
    for (const glm::ivec3 &t : tris) {
        glm::vec3 a = verts[t.x] * scale + origin;
        glm::vec3 b = verts[t.y] * scale + origin;
        glm::vec3 c = verts[t.z] * scale + origin;
        float maxEdge = glm::max(glm::distance(a, b),
                                 glm::max(glm::distance(a, c), glm::distance(b, c)));
        int n = glm::clamp(static_cast<int>(maxEdge / 0.35f) + 1, 1, 160);
        for (int i = 0; i <= n; ++i) {
            for (int j = 0; j <= n - i; ++j) {
                glm::vec3 p = a + (b - a) * (float(i) / n) + (c - a) * (float(j) / n);
                cells.push_back(glm::ivec3(glm::floor(p)));
            }
        }
    }
    m_terrain.setBlocksBulk(cells, STONE);
    if (qEnvironmentVariableIsSet("MC_PILOT")) {
        glm::ivec3 c0 = cells.empty() ? glm::ivec3(0) : cells[0];
        qDebug() << "obj dbg: cells" << cells.size() << "origin" << origin.x << origin.y << origin.z
                 << "first" << c0.x << c0.y << c0.z << "readback"
                 << (m_terrain.hasChunkAt(c0.x, c0.z)
                         ? (int)m_terrain.getGlobalBlockAt(c0.x, c0.y, c0.z) : -1);
    }
    return true;
}

void MyGL::voxelizeObjDialog() {
    setMouseCaptured(false);
    QString f = QFileDialog::getOpenFileName(this, "OBJ mesh", QDir::homePath(),
                                             "OBJ meshes (*.obj)");
    if (!f.isEmpty()) {
        makeCurrent();
        voxelizeObjFile(f);
    }
}

