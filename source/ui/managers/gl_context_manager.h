#ifndef RME_GL_CONTEXT_MANAGER_H_
#define RME_GL_CONTEXT_MANAGER_H_

#include <wx/wx.h>
#include <wx/glcanvas.h>
#include <memory>

#include <set>

class GLContextManager {
public:
	GLContextManager();
	~GLContextManager();

	wxGLContext* GetGLContext(wxGLCanvas* win);

	// Tries to make the context current on the preferred canvas, or falls back to
	// other available canvases if the preferred one is hidden (to avoid assertions).
	bool EnsureContextCurrent(wxGLContext& ctx, wxGLCanvas* preferredCanvas = nullptr);

	void RegisterCanvas(wxGLCanvas* canvas);
	void UnregisterCanvas(wxGLCanvas* canvas);

	void SetFallbackCanvas(wxGLCanvas* canvas) {
		m_fallbackCanvas = canvas;
	}

	// GL_VENDOR/GL_RENDERER da Intel, lido quando o contexto é criado. O driver
	// OpenGL da Intel no Windows lê errado SSBO no vertex shader e atributos
	// inteiros de VAO DSA, que o chunk cache usa: o terreno sai em quadrados
	// brancos (upstream #1040). A leitura precisa de um contexto atual; enquanto
	// não houve nenhum, devolve false e tenta de novo na próxima chamada.
	bool IsIntelGpu();

private:
	void DetectGpuVendor();

	std::unique_ptr<wxGLContext> OGLContext;
	bool m_gpuVendorKnown = false;
	bool m_intelGpu = false;
	std::set<wxGLCanvas*> m_canvases;
	wxGLCanvas* m_fallbackCanvas = nullptr;
};

extern GLContextManager g_gl_context;

#endif
