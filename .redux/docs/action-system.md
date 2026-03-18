# RME Redux - Action System (Undo/Redo)

## Overview

The action system provides undo/redo by storing tile snapshots before modifications. All map changes go through `Change → Action → BatchAction → ActionQueue`.

## Class Hierarchy

```
ActionQueue          — Manages undo/redo stack (deque of BatchActions)
  └── BatchAction    — Groups multiple Actions for atomic undo
       └── Action    — Contains multiple Changes
            └── Change — Single tile/waypoint/house modification
```

## Change (`source/editor/action.h`)

Stores a single modification using `std::variant`.

```cpp
enum ChangeType { CHANGE_NONE, CHANGE_TILE, CHANGE_MOVE_HOUSE_EXIT,
                  CHANGE_MOVE_WAYPOINT, CHANGE_CAMERA_PATHS };

class Change {
    ChangeType type;
    std::variant<std::monostate, std::unique_ptr<Tile>,
                 HouseExitChangeData, WaypointChangeData, CameraPathsChangeData> data;
public:
    explicit Change(std::unique_ptr<Tile> tile);          // Tile change
    static Change* Create(House* house, const Position&); // House exit
    static Change* Create(Waypoint* wp, const Position&); // Waypoint
    static Change* Create(const CameraPathsSnapshot&);    // Camera paths

    ChangeType getType() const;
    const Tile* getTile() const;
    uint32_t memsize() const;
};

using ChangeList = std::vector<std::unique_ptr<Change>>;
```

## Action (`source/editor/action.h`)

Contains multiple Changes. On commit, swaps tiles with the map (old tile stored for undo).

```cpp
enum ActionIdentifier {
    ACTION_MOVE, ACTION_REMOTE, ACTION_SELECT, ACTION_DELETE_TILES,
    ACTION_CUT_TILES, ACTION_PASTE_TILES, ACTION_RANDOMIZE,
    ACTION_BORDERIZE, ACTION_DRAW, ACTION_SWITCHDOOR,
    ACTION_ROTATE_ITEM, ACTION_REPLACE_ITEMS, ACTION_CHANGE_PROPERTIES,
    ACTION_GENERATE_DUNGEON
};

class Action {
    ChangeList changes;
    Editor& editor;
    ActionIdentifier type;
    bool commited;
public:
    void addChange(std::unique_ptr<Change> t);
    void commit(DirtyList* dirty_list);   // Apply changes to map
    void undo(DirtyList* dirty_list);     // Reverse (iterate in reverse)
    void redo(DirtyList* dirty_list) { commit(dirty_list); }
    size_t size() const;                  // NOTE: empty() does NOT exist, use size()==0
};
```

## BatchAction (`source/editor/action.h`)

Groups multiple Actions into one atomic undo/redo unit.

```cpp
class BatchAction {
    ActionVector batch;          // vector<unique_ptr<Action>>
    Editor& editor;
    int timestamp;               // For time-based merging
    uint32_t memory_size;        // Cached
    ActionIdentifier type;
public:
    void addAction(std::unique_ptr<Action> action);
    void addAndCommitAction(std::unique_ptr<Action> action);  // Add + commit immediately
    void commit();   // Commit all uncommitted actions
    void undo();     // Undo all in reverse
    void redo();     // Redo all
    void merge(BatchAction* other);  // Merge another batch into this
};
```

## ActionQueue (`source/editor/action_queue.h`)

Manages the undo/redo stack.

```cpp
class ActionQueue {
    std::deque<std::unique_ptr<BatchAction>> actions;
    size_t current;       // Index into actions (0..current = undo history)
    size_t memory_size;
    Editor& editor;
public:
    // Factory methods
    std::unique_ptr<Action> createAction(ActionIdentifier ident);
    std::unique_ptr<Action> createAction(BatchAction* parent);  // Inherits type from parent
    std::unique_ptr<BatchAction> createBatch(ActionIdentifier ident);

    // Add to stack
    void addBatch(std::unique_ptr<BatchAction> action, int stacking_delay = 0);
    void addAction(std::unique_ptr<Action> action, int stacking_delay = 0);

    // Undo/redo
    void undo();   // current--, batch->undo()
    void redo();   // batch->redo(), current++
    bool canUndo() { return current > 0; }
    bool canRedo() { return current < actions.size(); }
    void clear();
};
```

### addBatch() Behavior

1. Returns if batch is empty
2. Calls `batch->commit()` to finalize
3. Discards redo stack (pops everything after `current`)
4. Memory management: trims front if exceeds `Config::UNDO_MEM_SIZE` or `Config::UNDO_SIZE`
5. **Time-based merging**: if `GROUP_ACTIONS` enabled AND same type AND within `stacking_delay` → merges into last batch instead of pushing
6. Otherwise pushes batch and increments `current`

## Complete Usage Pattern

```cpp
// Step 1: Create batch
auto batch = editor->actionQueue->createBatch(ACTION_DRAW);

// Step 2: Create action
auto action = editor->actionQueue->createAction(batch.get());

// Step 3: Add changes (tile modifications)
auto newTile = TileOperations::deepCopy(tile, editor->map);
// ... modify newTile ...
action->addChange(std::make_unique<Change>(std::move(newTile)));

// Step 4: Commit action into batch
batch->addAndCommitAction(std::move(action));

// Step 5: (Optional) Add more actions to same batch
auto action2 = editor->actionQueue->createAction(batch.get());
// ... add borderize changes ...
batch->addAndCommitAction(std::move(action2));

// Step 6: Add batch to queue (triggers undo stack update)
editor->addBatch(std::move(batch));  // stacking_delay optional
```

## Real Examples from Codebase

### Cut Operation (`source/editor/operations/copy_operations.cpp`)
```cpp
auto batch = editor.actionQueue->createBatch(ACTION_CUT_TILES);
auto action = editor.actionQueue->createAction(batch.get());
for (Tile* tile : editor.selection) {
    auto newtile = TileOperations::deepCopy(tile, editor.map);
    // remove selected items from newtile
    action->addChange(std::make_unique<Change>(std::move(newtile)));
}
batch->addAndCommitAction(std::move(action));
editor.addBatch(std::move(batch));
```

### Camera Paths Snapshot (`source/editor/editor.cpp`)
```cpp
void Editor::ApplyCameraPathsSnapshot(const CameraPathsSnapshot& snapshot,
                                       ActionIdentifier actionType) {
    auto batch = actionQueue->createBatch(actionType);
    auto action = actionQueue->createAction(batch.get());
    action->addChange(std::unique_ptr<Change>(Change::Create(snapshot)));
    batch->addAndCommitAction(std::move(action));
    addBatch(std::move(batch), 2);  // stacking_delay = 2
}
```

## Editor Integration

```cpp
class Editor {
    std::unique_ptr<ActionQueue> actionQueue;
    Selection selection;
    Map map;

    void addBatch(std::unique_ptr<BatchAction> action, int stacking_delay = 0);
    void addAction(std::unique_ptr<Action> action, int stacking_delay = 0);
};
```

## Selection Interaction

During action commit/undo, selection state is preserved via `Selection::INTERNAL` sessions:
- `Action::commit()` calls `selection.start(Selection::INTERNAL)` / `selection.finish(Selection::INTERNAL)`
- INTERNAL flag means selection changes don't create undo entries

## Networked Extensions (`source/live/live_action.h`)

- `NetworkedAction : Action` — adds `owner` field for tracking client
- `NetworkedBatchAction : BatchAction` — broadcasts changes via `DirtyList`
- `NetworkedActionQueue : ActionQueue` — overrides createAction/createBatch for network
- `ACTION_REMOTE` actions bypass local undo stack

## Key Invariants

1. **Ownership**: Changes own tiles via `unique_ptr<Tile>`. Commit/undo SWAPS with map.
2. **No empty()**: Use `action->size() == 0` (not `action->empty()`)
3. **Always move**: `std::move()` required for all unique_ptr transfers
4. **Redo discard**: New actions erase redo history
5. **Memory cached**: `BatchAction::memory_size` is cached, recalculated on merge

## File Locations

| Component | Path |
|-----------|------|
| Action/BatchAction/Change | `source/editor/action.h`, `action.cpp` |
| ActionQueue | `source/editor/action_queue.h`, `action_queue.cpp` |
| Editor | `source/editor/editor.h`, `editor.cpp` |
| Selection | `source/editor/selection.h` |
| CopyBuffer | `source/editor/copybuffer.h` |
| CopyOperations | `source/editor/operations/copy_operations.cpp` |
| DirtyList | `source/editor/dirty_list.h` |
| NetworkedAction | `source/live/live_action.h`, `live_action.cpp` |
