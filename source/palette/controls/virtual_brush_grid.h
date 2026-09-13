#ifndef RME_PALETTE_CONTROLS_VIRTUAL_BRUSH_GRID_H_
#define RME_PALETTE_CONTROLS_VIRTUAL_BRUSH_GRID_H_

#include "app/main.h"
#include "util/nanovg_canvas.h"
#include "palette/panels/brush_panel.h"
#include "ui/dcbutton.h" // For RenderSize

/**
 * @class VirtualBrushGrid
 * @brief High-performance brush grid using NanoVG for GPU-accelerated rendering.
 *
 * This control displays a grid of brush icons with virtual scrolling,
 * supporting thousands of brushes at 60fps. Uses texture caching for
 * efficient sprite rendering.
 */
class VirtualBrushGrid : public NanoVGCanvas, public BrushBoxInterface {
public:
	/**
	 * @brief Constructs a VirtualBrushGrid.
	 * @param parent Parent window
	 * @param _tileset The tileset category containing brushes
	 * @param rsz Icon render size (16x16 or 32x32)
	 */
	VirtualBrushGrid(wxWindow* parent, const TilesetCategory* _tileset, RenderSize rsz);
	~VirtualBrushGrid() override;

	wxWindow* GetSelfWindow() override {
		return this;
	}

	// BrushBoxInterface
	void SelectFirstBrush() override;
	Brush* GetSelectedBrush() const override;
	bool SelectBrush(const Brush* brush) override;
	bool SelectBrushByOffset(int offset) override;
	void OnSwitchIn() override;
	void OnSwitchOut() override;

	enum class DisplayMode {
		Grid,
		List
	};

	static constexpr int LIST_ROW_HEIGHT = 36;
	static constexpr int GRID_PADDING = 4;
	static constexpr int GRID_ITEM_SIZE_BASE = 32;
	static constexpr int ICON_OFFSET = 2;
	static constexpr int PRELOAD_BATCH_SIZE = 16; // Textures to preload per timer tick
	static constexpr int PRELOAD_TIMER_INTERVAL = 8; // ms between preload batches
	static constexpr int LABEL_HEIGHT = 26; // Extra room under the icon for a two line name

	void SetDisplayMode(DisplayMode mode);

	// Filter support
	void SetFilter(const std::string& filter);
	void ClearFilter();

	// Display options
	void SetSort(TilesetSortKey key, TilesetSortDirection dir) override;
	void SetShowLabels(bool show) override;
	// Drag and drop inside the grid reorders instead of dragging brushes out
	void SetReorderMode(bool enabled) override;
	// Drops the user defined order of this tileset category
	void ResetCustomOrder() override;

protected:
	/**
	 * @brief Performs NanoVG rendering of the brush grid.
	 * @param vg NanoVG context
	 * @param width Canvas width
	 * @param height Canvas height
	 */
	void OnNanoVGPaint(NVGcontext* vg, int width, int height) override;

	wxSize DoGetBestClientSize() const override;

	// Event Handlers
	void OnMouseDown(wxMouseEvent& event);
	void OnMouseUp(wxMouseEvent& event);
	void OnCaptureLost(wxMouseCaptureLostEvent& event);
	void OnRightClick(wxMouseEvent& event);
	void OnCopyServerID(wxCommandEvent& event);
	void OnCopyClientID(wxCommandEvent& event);
	void OnApplyReplaceOriginal(wxCommandEvent& event);
	void OnApplyReplaceReplacement(wxCommandEvent& event);
	void OnMoveBrushMenu(wxCommandEvent& event);
	void OnMotion(wxMouseEvent& event);
	void OnSize(wxSizeEvent& event);

	// Internal helpers
	void UpdateLayout();
	int HitTest(int x, int y) const;
	wxRect GetItemRect(int index) const;
	void DrawBrushItem(NVGcontext* vg, int index, const wxRect& rect);
	void DrawInsertMarker(NVGcontext* vg);

	// Cell metrics (labels grow the grid cells, never the list rows)
	int GetCellHeight() const;
	int GetRowStride() const;

	DisplayMode display_mode = DisplayMode::Grid;
	RenderSize icon_size;
	int selected_index;
	int hover_index;
	int columns;
	int item_size;
	int padding;

	// Display list: base order (user defined or as loaded) filtered and sorted.
	size_t GetEffectiveBrushCount() const;
	Brush* GetEffectiveBrush(size_t index) const;
	std::string current_filter;
	std::vector<size_t> base_indices; // Indices into tileset->brushlist, user order
	std::vector<size_t> display_indices; // base_indices after filter and sort
	bool filter_active = false;
	bool use_display_list = false; // False means "plain tileset order"
	void RebuildDisplayList();
	void LoadStoredOrder();

	// Sorting
	TilesetSortKey sort_key = TilesetSortKey::None;
	TilesetSortDirection sort_dir = TilesetSortDirection::Ascending;

	// Labels
	bool show_labels = false;

	// Manual reordering
	bool reorder_mode = false;
	int reorder_source = -1; // Display index being dragged
	int reorder_target = -1; // Insert position, -1 when idle
	bool CanReorder() const;
	int InsertHitTest(int x, int y) const;
	void MoveBrush(int from, int insertBefore);
	void PersistOrder();

	// Optimization: UTF8 name cache
	mutable std::unordered_map<const Brush*, std::string> m_utf8NameCache;
	const std::string& GetUtf8Name(const Brush* brush) const;

	// Animation state
	wxTimer m_animTimer;
	float hover_anim = 0.0f;
	void OnTimer(wxTimerEvent& event);

	// Progressive texture preloading
	wxTimer m_preloadTimer;
	size_t m_preloadIndex = 0; // Next brush index to preload
	bool m_preloadComplete = false;
	void OnPreloadTimer(wxTimerEvent& event);
	void StartPreloading();

	// Drag state
	wxPoint m_dragStartPos;
	bool m_isDragging = false;
};

#endif
