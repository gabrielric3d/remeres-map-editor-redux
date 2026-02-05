#ifndef RME_UTIL_NANOVG_CANVAS_H_
#define RME_UTIL_NANOVG_CANVAS_H_

#include "app/main.h"
#include <QtOpenGLWidgets/QOpenGLWidget>
#include <QtGui/QOpenGLFunctions>
#include <QtGui/QWheelEvent>
#include <QtGui/QMouseEvent>

#include <map>
#include <cstdint>
#include <memory>

#include "rendering/core/graphics.h"

struct NVGcontext;

/**
 * @class NanoVGCanvas
 * @brief Qt-based OpenGL widget using NanoVG for rendering.
 */
class NanoVGCanvas : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT

public:
    NanoVGCanvas(QWidget* parent = nullptr);
    virtual ~NanoVGCanvas();

    // Non-copyable
    NanoVGCanvas(const NanoVGCanvas&) = delete;
    NanoVGCanvas& operator=(const NanoVGCanvas&) = delete;

    [[nodiscard]] int GetScrollPosition() const {
        return m_scrollPos;
    }

    void SetScrollPosition(int pos);

    [[nodiscard]] NVGcontext* GetNVGContext() const {
        return m_nvg.get();
    }

    // Refresh the canvas (trigger repaint)
    void Refresh() { update(); }

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;

    // Custom NanoVG painting
    virtual void OnNanoVGPaint(NVGcontext* vg, int width, int height) = 0;

    // Events
    void wheelEvent(QWheelEvent* event) override;
    // void mouseMoveEvent(QMouseEvent* event) override; // Implement if needed

    // Helpers
    int GetOrCreateImage(uint32_t id, const uint8_t* data, int width, int height);
    void DeleteCachedImage(uint32_t id);
    void ClearImageCache();
    [[nodiscard]] int GetCachedImage(uint32_t id) const;

    void UpdateScrollbar(int contentHeight);

    // Context is always current in paintGL
    bool MakeContextCurrent() { makeCurrent(); return true; }

    float m_bgRed = 45.0f / 255.0f;
    float m_bgGreen = 45.0f / 255.0f;
    float m_bgBlue = 45.0f / 255.0f;

private:
    std::unique_ptr<NVGcontext, NVGDeleter> m_nvg;

    // Texture cache: ID -> NanoVG image handle
    std::map<uint32_t, int> m_imageCache;

    // Scroll state
    int m_scrollPos = 0;
    int m_contentHeight = 0;
    int m_scrollStep = 40;

protected:
    void SetScrollStep(int step) {
        m_scrollStep = step;
    }
};

#endif // RME_UTIL_NANOVG_CANVAS_H_
