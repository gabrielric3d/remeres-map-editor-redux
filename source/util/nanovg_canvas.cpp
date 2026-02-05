#include "util/nanovg_canvas.h"
#include <glad/glad.h>

#ifdef NANOVG_GL3
	#include "nanovg_gl.h"
#else
    #error "NanoVG GL3 implementation required"
#endif

#include <iostream>

NanoVGCanvas::NanoVGCanvas(QWidget* parent) : QOpenGLWidget(parent) {
    // Enable mouse tracking if needed for hover effects
    setMouseTracking(true);
}

NanoVGCanvas::~NanoVGCanvas() {
    MakeContextCurrent();
    // NanoVG context is deleted by unique_ptr with deleter
    // Images are destroyed when context is destroyed
}

void NanoVGCanvas::initializeGL() {
    initializeOpenGLFunctions();

    // Initialize glad if not already done globally
    // gladLoadGL(); // Assuming global init or handled by Qt's context

    // Initialize NanoVG
    // We use nvgCreateGL3 with appropriate flags
    int flags = NVG_ANTIALIAS | NVG_STENCIL_STROKES;
#ifdef __DEBUG__
    flags |= NVG_DEBUG;
#endif

    m_nvg.reset(nvgCreateGL3(flags));
    if (!m_nvg) {
        std::cerr << "Could not init nanovg." << std::endl;
    }
}

void NanoVGCanvas::resizeGL(int w, int h) {
    // Viewport handled by QOpenGLWidget
}

void NanoVGCanvas::paintGL() {
    if (!m_nvg) return;

    int width = this->width();
    int height = this->height();
    float ratio = this->devicePixelRatio();

    // Clear background
    glClearColor(m_bgRed, m_bgGreen, m_bgBlue, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    nvgBeginFrame(m_nvg.get(), width, height, ratio);

    // Apply scroll translation
    nvgSave(m_nvg.get());
    nvgTranslate(m_nvg.get(), 0, -m_scrollPos);

    OnNanoVGPaint(m_nvg.get(), width, height);

    nvgRestore(m_nvg.get());

    nvgEndFrame(m_nvg.get());
}

void NanoVGCanvas::wheelEvent(QWheelEvent* event) {
    int delta = event->angleDelta().y();
    if (delta > 0) {
        m_scrollPos -= m_scrollStep;
    } else {
        m_scrollPos += m_scrollStep;
    }

    // Clamp
    int maxScroll = std::max(0, m_contentHeight - height());
    m_scrollPos = std::max(0, std::min(m_scrollPos, maxScroll));

    update(); // Schedule repaint
    event->accept();
}

void NanoVGCanvas::SetScrollPosition(int pos) {
    m_scrollPos = pos;
    update();
}

void NanoVGCanvas::UpdateScrollbar(int contentHeight) {
    m_contentHeight = contentHeight;
    // Qt scrollbar logic would go here if we were using QAbstractScrollArea
    // For now, we just clamp internal state
    int maxScroll = std::max(0, m_contentHeight - height());
    m_scrollPos = std::max(0, std::min(m_scrollPos, maxScroll));
}

int NanoVGCanvas::GetOrCreateImage(uint32_t id, const uint8_t* data, int width, int height) {
    if (!m_nvg) return 0;

    auto it = m_imageCache.find(id);
    if (it != m_imageCache.end()) {
        return it->second;
    }

    int img = nvgCreateImageRGBA(m_nvg.get(), width, height, 0, (const unsigned char*)data);
    if (img != 0) {
        m_imageCache[id] = img;
    }
    return img;
}

void NanoVGCanvas::DeleteCachedImage(uint32_t id) {
    if (!m_nvg) return;
    auto it = m_imageCache.find(id);
    if (it != m_imageCache.end()) {
        nvgDeleteImage(m_nvg.get(), it->second);
        m_imageCache.erase(it);
    }
}

void NanoVGCanvas::ClearImageCache() {
    if (!m_nvg) return;
    for (auto& pair : m_imageCache) {
        nvgDeleteImage(m_nvg.get(), pair.second);
    }
    m_imageCache.clear();
}

int NanoVGCanvas::GetCachedImage(uint32_t id) const {
    auto it = m_imageCache.find(id);
    if (it != m_imageCache.end()) {
        return it->second;
    }
    return 0;
}
