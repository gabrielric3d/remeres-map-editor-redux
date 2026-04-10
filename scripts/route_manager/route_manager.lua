-- Route Manager Script
-- Manage player routes using camera paths system.
-- Define start (A), waypoints, and end (B) points to create reusable routes.

if not app then
    print("Error: RME Lua API not found.")
    return
end

-- ============================================================================
-- State
-- ============================================================================

local dlg = nil
local selectedRouteName = nil
local selectedKeyframeIndex = nil  -- 1-based

-- ============================================================================
-- Helpers
-- ============================================================================

local function getRoutes()
    if not app.hasMap() then return {} end
    return app.cameraPaths.getPaths()
end

local function getRouteByName(name)
    if not app.hasMap() or not name then return nil end
    return app.cameraPaths.getPath(name)
end

local function formatPos(x, y, z)
    if not x or not y or not z then return "---" end
    return string.format("%d, %d, %d", x, y, z)
end

local function getKeyframeLabel(index, total)
    if index == 1 then
        return "Start (A)"
    elseif index == total then
        return "End (B)"
    else
        return "Waypoint " .. (index - 1)
    end
end

local function getCameraPos()
    local pos = app.getCameraPosition()
    if not pos then return nil end
    return { x = pos.x, y = pos.y, z = pos.z }
end

-- ============================================================================
-- Route List Items
-- ============================================================================

local function buildRouteListItems()
    local routes = getRoutes()
    local items = {}
    for i, route in ipairs(routes) do
        local kfCount = route.keyframeCount or 0
        local startPos = "---"
        local endPos = "---"
        if route.keyframes and #route.keyframes > 0 then
            local first = route.keyframes[1]
            startPos = formatPos(first.x, first.y, first.z)
            if #route.keyframes > 1 then
                local last = route.keyframes[#route.keyframes]
                endPos = formatPos(last.x, last.y, last.z)
            end
        end
        local text = string.format("%s  |  %d pts  |  %s -> %s",
            route.name, kfCount, startPos, endPos)
        table.insert(items, {
            text = text,
            tooltip = string.format("Route: %s\nPoints: %d\nStart: %s\nEnd: %s",
                route.name, kfCount, startPos, endPos)
        })
    end
    return items
end

-- ============================================================================
-- Keyframe List Items
-- ============================================================================

local function buildKeyframeListItems()
    if not selectedRouteName then return {} end
    local route = getRouteByName(selectedRouteName)
    if not route or not route.keyframes then return {} end

    local items = {}
    local total = #route.keyframes
    for i, kf in ipairs(route.keyframes) do
        local label = getKeyframeLabel(i, total)
        local text = string.format("#%d  %s  |  %s",
            i, label, formatPos(kf.x, kf.y, kf.z))
        table.insert(items, {
            text = text,
            tooltip = string.format("Position: %s\nDuration: %.1f\nZoom: %.1f",
                formatPos(kf.x, kf.y, kf.z), kf.duration or 1.0, kf.zoom or 1.0)
        })
    end
    return items
end

-- ============================================================================
-- Refresh UI
-- ============================================================================

local function refreshUI()
    if not dlg then return end

    -- Refresh route list
    local routeItems = buildRouteListItems()
    dlg:modify({ route_list = { items = routeItems } })

    -- Find selection index for routes
    local routes = getRoutes()
    local routeSelIdx = 0
    if selectedRouteName then
        for i, r in ipairs(routes) do
            if r.name == selectedRouteName then
                routeSelIdx = i
                break
            end
        end
        if routeSelIdx == 0 then
            selectedRouteName = nil
            selectedKeyframeIndex = nil
        end
    end

    -- Update selected route label
    local routeLabel = selectedRouteName or "(none)"
    dlg:modify({ lbl_selected_route = { text = "Selected: " .. routeLabel } })

    -- Refresh keyframe list
    local kfItems = buildKeyframeListItems()
    dlg:modify({ keyframe_list = { items = kfItems } })

    -- Update show paths checkbox
    if app.hasMap() then
        local showing = app.cameraPaths.isShowingPaths()
        dlg:modify({ chk_show_paths = { selected = showing } })

        -- Update loop checkbox
        if selectedRouteName then
            local route = getRouteByName(selectedRouteName)
            if route then
                dlg:modify({ chk_loop = { selected = route.loop } })
            end
        end
    end
end

-- ============================================================================
-- Route Actions
-- ============================================================================

local function onNewRoute()
    if not app.hasMap() then
        app.alert("No map open.")
        return
    end
    local name = app.cameraPaths.addPath()
    if name then
        selectedRouteName = name
        selectedKeyframeIndex = nil
        refreshUI()
    end
end

local function onRenameRoute()
    if not selectedRouteName then
        app.alert("No route selected.")
        return
    end

    -- Simple rename using alert with input (we'll use a sub-dialog)
    local renameDlg = Dialog {
        title = "Rename Route",
        width = 300,
        height = 120,
        resizable = false
    }
    renameDlg:input {
        id = "new_name",
        label = "New name:",
        text = selectedRouteName,
        expand = true
    }
    renameDlg:newrow()
    renameDlg:box({ orient = "horizontal", padding = 4, expand = false, align = "right" })
        renameDlg:button {
            text = "Rename",
            onclick = function(d)
                local newName = d.data.new_name
                if newName and newName ~= "" and newName ~= selectedRouteName then
                    local ok = app.cameraPaths.renamePath(selectedRouteName, newName)
                    if ok then
                        selectedRouteName = newName
                    else
                        app.alert("Failed to rename. Name may already exist.")
                    end
                end
                d:close()
                refreshUI()
            end
        }
        renameDlg:button {
            text = "Cancel",
            onclick = function(d) d:close() end
        }
    renameDlg:endbox()
    renameDlg:show{ wait = false }
end

local function onDeleteRoute()
    if not selectedRouteName then
        app.alert("No route selected.")
        return
    end

    local result = app.alert({
        title = "Delete Route",
        text = "Delete route '" .. selectedRouteName .. "'? This action can be undone.",
        buttons = { "Yes", "No" }
    })

    if result == 1 then
        app.cameraPaths.removePath(selectedRouteName)
        selectedRouteName = nil
        selectedKeyframeIndex = nil
        refreshUI()
    end
end

local function onDuplicateRoute()
    if not selectedRouteName then
        app.alert("No route selected.")
        return
    end

    local source = getRouteByName(selectedRouteName)
    if not source then return end

    -- Create new path with unique name
    local newName = app.cameraPaths.addPath(selectedRouteName .. " (copy)")
    if not newName then return end

    -- Copy keyframes from source to new path
    if source.keyframes then
        for _, kf in ipairs(source.keyframes) do
            app.cameraPaths.addKeyframe(newName, {
                x = kf.x, y = kf.y, z = kf.z,
                duration = kf.duration,
                speed = kf.speed,
                zoom = kf.zoom,
                easing = kf.easing
            })
        end
    end

    -- Copy loop setting
    if source.loop then
        app.cameraPaths.setPathLoop(newName, true)
    end

    selectedRouteName = newName
    selectedKeyframeIndex = nil
    refreshUI()
end

-- ============================================================================
-- Keyframe Actions
-- ============================================================================

local function onSetStart()
    if not selectedRouteName then
        app.alert("No route selected.")
        return
    end
    local pos = getCameraPos()
    if not pos then
        app.alert("Cannot get camera position.")
        return
    end

    local route = getRouteByName(selectedRouteName)
    if not route then return end

    if route.keyframes and #route.keyframes > 0 then
        -- Update existing first keyframe
        app.cameraPaths.updateKeyframe(selectedRouteName, 1, {
            x = pos.x, y = pos.y, z = pos.z
        })
    else
        -- Add new first keyframe
        app.cameraPaths.addKeyframe(selectedRouteName, {
            x = pos.x, y = pos.y, z = pos.z,
            duration = 1.0, zoom = 1.0
        })
    end
    refreshUI()
end

local function onAddWaypoint()
    if not selectedRouteName then
        app.alert("No route selected.")
        return
    end
    local pos = getCameraPos()
    if not pos then
        app.alert("Cannot get camera position.")
        return
    end

    local route = getRouteByName(selectedRouteName)
    if not route then return end

    local kfProps = {
        x = pos.x, y = pos.y, z = pos.z,
        duration = 1.0, zoom = 1.0
    }

    -- If we have a selection, insert after it (but before the last if it's the end)
    local total = route.keyframes and #route.keyframes or 0
    if selectedKeyframeIndex and selectedKeyframeIndex > 0 and total > 0 then
        -- Insert after the selected keyframe
        local insertIdx = selectedKeyframeIndex + 1
        if insertIdx > total then insertIdx = total end
        kfProps.index = insertIdx
    elseif total > 1 then
        -- Insert before the last (end) keyframe
        kfProps.index = total
    end

    app.cameraPaths.addKeyframe(selectedRouteName, kfProps)
    refreshUI()
end

local function onSetEnd()
    if not selectedRouteName then
        app.alert("No route selected.")
        return
    end
    local pos = getCameraPos()
    if not pos then
        app.alert("Cannot get camera position.")
        return
    end

    local route = getRouteByName(selectedRouteName)
    if not route then return end

    local total = route.keyframes and #route.keyframes or 0
    if total > 0 then
        -- Update existing last keyframe
        app.cameraPaths.updateKeyframe(selectedRouteName, total, {
            x = pos.x, y = pos.y, z = pos.z
        })
    else
        -- Add as first (and only) keyframe
        app.cameraPaths.addKeyframe(selectedRouteName, {
            x = pos.x, y = pos.y, z = pos.z,
            duration = 1.0, zoom = 1.0
        })
    end
    refreshUI()
end

local function onRemovePoint()
    if not selectedRouteName or not selectedKeyframeIndex then
        app.alert("No point selected.")
        return
    end

    app.cameraPaths.removeKeyframe(selectedRouteName, selectedKeyframeIndex)
    selectedKeyframeIndex = nil
    refreshUI()
end

local function onMoveUp()
    if not selectedRouteName or not selectedKeyframeIndex then return end
    if selectedKeyframeIndex <= 1 then return end

    app.cameraPaths.moveKeyframe(selectedRouteName, selectedKeyframeIndex, selectedKeyframeIndex - 1)
    selectedKeyframeIndex = selectedKeyframeIndex - 1
    refreshUI()
end

local function onMoveDown()
    if not selectedRouteName or not selectedKeyframeIndex then return end

    local route = getRouteByName(selectedRouteName)
    if not route or not route.keyframes then return end
    if selectedKeyframeIndex >= #route.keyframes then return end

    app.cameraPaths.moveKeyframe(selectedRouteName, selectedKeyframeIndex, selectedKeyframeIndex + 1)
    selectedKeyframeIndex = selectedKeyframeIndex + 1
    refreshUI()
end

local function onGoTo()
    if not selectedRouteName or not selectedKeyframeIndex then
        app.alert("No point selected.")
        return
    end
    app.cameraPaths.goToKeyframe(selectedRouteName, selectedKeyframeIndex)
end

-- ============================================================================
-- Create UI
-- ============================================================================

local function createUI()
    dlg = Dialog {
        title = "Route Manager",
        width = 380,
        height = 600,
        resizable = true,
        dockable = true,
        onclose = function()
            dlg = nil
        end
    }

    -- ========================================
    -- Section 1: Route List
    -- ========================================
    dlg:box({ orient = "vertical", label = "Routes", padding = 6, margin = 4, expand = true })
        dlg:list {
            id = "route_list",
            height = 120,
            expand = true,
            items = buildRouteListItems(),
            onchange = function(d)
                local idx = d.data.route_list
                if idx and idx > 0 then
                    local routes = getRoutes()
                    if routes[idx] then
                        selectedRouteName = routes[idx].name
                        selectedKeyframeIndex = nil
                        app.cameraPaths.setActivePath(selectedRouteName)
                        refreshUI()
                    end
                end
            end,
            ondoubleclick = function(d)
                -- Double-click on route: toggle show paths
                if app.hasMap() then
                    local showing = app.cameraPaths.isShowingPaths()
                    app.cameraPaths.setShowPaths(not showing)
                    refreshUI()
                end
            end
        }
    dlg:endbox()

    -- ========================================
    -- Section 2: Route Actions
    -- ========================================
    dlg:box({ orient = "horizontal", padding = 4, margin = 2, expand = false })
        dlg:button {
            text = "New",
            width = 60,
            onclick = function() onNewRoute() end
        }
        dlg:button {
            text = "Rename",
            width = 60,
            onclick = function() onRenameRoute() end
        }
        dlg:button {
            text = "Delete",
            width = 60,
            onclick = function() onDeleteRoute() end
        }
        dlg:button {
            text = "Duplicate",
            width = 70,
            onclick = function() onDuplicateRoute() end
        }
    dlg:endbox()

    -- ========================================
    -- Section 3: Keyframe List
    -- ========================================
    dlg:label { id = "lbl_selected_route", text = "Selected: (none)" }

    dlg:box({ orient = "vertical", label = "Route Points", padding = 6, margin = 4, expand = true })
        dlg:list {
            id = "keyframe_list",
            height = 140,
            expand = true,
            items = buildKeyframeListItems(),
            onchange = function(d)
                local idx = d.data.keyframe_list
                if idx and idx > 0 then
                    selectedKeyframeIndex = idx
                end
            end,
            ondoubleclick = function(d)
                -- Double-click: go to keyframe
                if selectedRouteName and selectedKeyframeIndex then
                    app.cameraPaths.goToKeyframe(selectedRouteName, selectedKeyframeIndex)
                end
            end
        }
    dlg:endbox()

    -- ========================================
    -- Section 4: Keyframe Actions
    -- ========================================
    dlg:box({ orient = "horizontal", padding = 4, margin = 2, expand = false })
        dlg:button {
            text = "Set Start (A)",
            width = 80,
            onclick = function() onSetStart() end
        }
        dlg:button {
            text = "Add Waypoint",
            width = 90,
            onclick = function() onAddWaypoint() end
        }
        dlg:button {
            text = "Set End (B)",
            width = 80,
            onclick = function() onSetEnd() end
        }
    dlg:endbox()

    dlg:box({ orient = "horizontal", padding = 4, margin = 2, expand = false })
        dlg:button {
            text = "Remove",
            width = 60,
            onclick = function() onRemovePoint() end
        }
        dlg:button {
            text = "Move Up",
            width = 60,
            onclick = function() onMoveUp() end
        }
        dlg:button {
            text = "Move Down",
            width = 70,
            onclick = function() onMoveDown() end
        }
        dlg:button {
            text = "Go To",
            width = 50,
            onclick = function() onGoTo() end
        }
    dlg:endbox()

    -- ========================================
    -- Section 5: Options
    -- ========================================
    dlg:box({ orient = "vertical", label = "Options", padding = 6, margin = 4, expand = false })
        dlg:check {
            id = "chk_show_paths",
            text = "Show Routes on Map",
            selected = app.hasMap() and app.cameraPaths.isShowingPaths() or false,
            onclick = function(d)
                if app.hasMap() then
                    app.cameraPaths.setShowPaths(d.data.chk_show_paths)
                end
            end
        }
        dlg:newrow()
        dlg:check {
            id = "chk_loop",
            text = "Loop Route",
            selected = false,
            onclick = function(d)
                if selectedRouteName then
                    app.cameraPaths.setPathLoop(selectedRouteName, d.data.chk_loop)
                end
            end
        }
    dlg:endbox()

    dlg:show{ wait = false }
end

-- ============================================================================
-- Main
-- ============================================================================

createUI()
