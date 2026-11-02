#include "app/main.h"
#include "ui/managers/gl_context_manager.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cctype>
#include <string>

GLContextManager g_gl_context;

GLContextManager::GLContextManager() = default;
GLContextManager::~GLContextManager() = default;

wxGLContext* GLContextManager::GetGLContext(wxGLCanvas* win) {
	if (win) {
		RegisterCanvas(win);
	}

	if (!OGLContext) {
#ifdef __WXOSX__
		OGLContext = std::make_unique<wxGLContext>(win, nullptr);
#else
		wxGLContextAttrs ctxAttrs;
		ctxAttrs.PlatformDefaults().CoreProfile().MajorVersion(4).MinorVersion(5).EndList();
		OGLContext = std::make_unique<wxGLContext>(win, nullptr, &ctxAttrs);
		spdlog::info("GLContextManager: Created new OpenGL 4.5 Core Profile context");
#endif
		// Initialize GLAD for the new context
		if (win) {
			if (EnsureContextCurrent(*OGLContext, win)) {
				if (!gladLoadGL()) {
					spdlog::error("GLContextManager: Failed to initialize GLAD!");
				} else {
					spdlog::info("GLContextManager: GLAD initialized successfully");
					DetectGpuVendor();
				}
			}
		}
	}

	return OGLContext.get();
}

bool GLContextManager::IsIntelGpu() {
	if (!m_gpuVendorKnown) {
		DetectGpuVendor();
	}
	return m_intelGpu;
}

void GLContextManager::DetectGpuVendor() {
	if (m_gpuVendorKnown || !glad_glGetString) {
		return; // GLAD ainda não carregado
	}
	const auto* vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
	if (!vendor) {
		return; // sem contexto atual
	}
	const auto* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
	m_gpuVendorKnown = true;
	const std::string vendor_str = vendor;
	const std::string renderer_str = renderer ? renderer : "";
	spdlog::info("GLContextManager: GPU vendor '{}', renderer '{}'", vendor_str, renderer_str);

	std::string probe = vendor_str + " " + renderer_str;
	std::ranges::transform(probe, probe.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	m_intelGpu = probe.find("intel") != std::string::npos;
	if (m_intelGpu) {
		spdlog::warn("GLContextManager: Intel GPU detected; the GPU terrain cache stays off unless chunk_cache_on_intel = true in config.toml");
	}
}

void GLContextManager::RegisterCanvas(wxGLCanvas* canvas) {
	if (canvas) {
		m_canvases.insert(canvas);
	}
}

void GLContextManager::UnregisterCanvas(wxGLCanvas* canvas) {
	m_canvases.erase(canvas);
	if (m_fallbackCanvas == canvas) {
		m_fallbackCanvas = nullptr;
	}
}

bool GLContextManager::EnsureContextCurrent(wxGLContext& ctx, wxGLCanvas* preferredCanvas) {
	// 1. Try preferred canvas if valid and shown
	if (preferredCanvas && preferredCanvas->IsShownOnScreen()) {
		if (preferredCanvas->SetCurrent(ctx)) {
			return true;
		}
	}

	// 2. Try the designated fallback canvas if it's shown
	if (m_fallbackCanvas && m_fallbackCanvas != preferredCanvas && m_fallbackCanvas->IsShownOnScreen()) {
		wxLogNull logNo; // Suppress "Invalid Pixel Format" or other errors if incompatible
		if (m_fallbackCanvas->SetCurrent(ctx)) {
			return true;
		}
	}

	// 3. Try any other registered canvas as a last resort
	wxLogNull logNo; // Suppress errors during fallback attempts
	for (auto* canvas : m_canvases) {
		if (canvas != preferredCanvas && canvas != m_fallbackCanvas && canvas->IsShownOnScreen()) {
			if (canvas->SetCurrent(ctx)) {
				return true;
			}
		}
	}

	return false;
}
