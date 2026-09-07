//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_UI_CLAUDE_PANEL_H_
#define RME_UI_CLAUDE_PANEL_H_

#include "app/main.h"
#include "ai/assistant_backend.h"
#include "ai/claude_agent.h"

#include <map>
#include <memory>
#include <wx/wx.h>

class wxChoice;

// Dockable chat with Claude (Window > Claude Assistant). The assistant sees and
// edits the open map through ClaudeAgent's tools; this panel only renders the
// conversation and holds the model/effort/API-key settings.
class ClaudePanel : public wxPanel, private AssistantListener {
public:
	explicit ClaudePanel(wxWindow* parent);
	~ClaudePanel() override;

	// Puts the caret in the message box (used when the pane is shown).
	void FocusInput();

private:
	void BuildUi();
	// (Re)creates the conversation engine for the selected backend.
	void CreateBackend();
	void ApplySettingsToAgent();
	void LoadApiKey();
	void SendCurrentInput();
	void SetBusy(bool busy);
	void AppendStyled(const wxString& text, const wxColour& colour, bool bold = false, bool italic = false);
	void AppendLine(const wxString& text, const wxColour& colour, bool bold = false);
	void EnsureAssistantHeader();
	void UpdateStatus();

	// AssistantListener
	void onAssistantText(const std::string& delta) override;
	void onThinking(const std::string& delta) override;
	void onToolCall(const std::string& name, const nlohmann::json& input) override;
	void onToolResult(const std::string& name, const ClaudeToolResult& result) override;
	void onTurnEnd(const std::string& stop_reason) override;
	void onError(const std::string& message) override;
	void onUsage(const AssistantUsage& usage) override;
	void onRateLimit(const AssistantRateLimit& limit) override;
	void UpdatePlanUsage();

	void OnSend(wxCommandEvent& event);
	void OnStop(wxCommandEvent& event);
	void OnClear(wxCommandEvent& event);
	void OnApiKey(wxCommandEvent& event);
	void OnBackendChanged(wxCommandEvent& event);
	void OnModelChanged(wxCommandEvent& event);
	void OnEffortChanged(wxCommandEvent& event);
	void OnShowThinkingChanged(wxCommandEvent& event);
	void OnInputKeyDown(wxKeyEvent& event);

	std::unique_ptr<AssistantBackend> backend;
	ClaudeAgent* api_agent = nullptr; // non-null while the API backend is active

	wxChoice* backend_choice = nullptr;
	wxChoice* model_choice = nullptr;
	wxChoice* effort_choice = nullptr;
	wxCheckBox* show_thinking = nullptr;
	wxButton* api_key_button = nullptr;
	wxTextCtrl* transcript = nullptr;
	wxTextCtrl* input = nullptr;
	wxButton* send_button = nullptr;
	wxButton* stop_button = nullptr;
	wxButton* clear_button = nullptr;
	wxStaticText* status = nullptr;
	wxStaticText* plan_usage = nullptr;
	std::map<std::string, AssistantRateLimit> rate_limits; // by window type

	bool assistant_header_written = false;
	bool thinking_header_written = false;
	bool busy = false;
};

#endif
