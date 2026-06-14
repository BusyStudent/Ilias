// State
let tasks = [];
let isPaused = false;
let sortKey = 'id';
let sortAsc = true;
let updateInterval = null;
let settings = {
    refreshInterval: 1000,
    hideOrphanChildren: false,
};

const SETTINGS_STORAGE_KEY = 'ilias-tracing-webui-settings';

// State for stacktrace
let expandedTasks = new Set();
let stackTraces = {};

// State for tree
let expandedTreeNodes = new Set();

function loadSettings() {
    try {
        const saved = JSON.parse(localStorage.getItem(SETTINGS_STORAGE_KEY) || '{}');
        settings = {
            ...settings,
            ...saved,
            refreshInterval: Number(saved.refreshInterval || settings.refreshInterval),
            hideOrphanChildren: Boolean(saved.hideOrphanChildren),
        };
    }
    catch (error) {
        console.warn('Failed to load settings:', error);
    }
}

function saveSettings() {
    try {
        localStorage.setItem(SETTINGS_STORAGE_KEY, JSON.stringify(settings));
    }
    catch (error) {
        console.warn('Failed to save settings:', error);
    }
}

function syncSettingsControls() {
    const intervalSelect = document.getElementById('refreshIntervalSelect');
    const orphanToggle = document.getElementById('hideOrphanChildrenToggle');
    if (intervalSelect) intervalSelect.value = String(settings.refreshInterval);
    if (orphanToggle) orphanToggle.checked = settings.hideOrphanChildren;
}

function restartUpdateTimer() {
    if (updateInterval !== null) {
        clearInterval(updateInterval);
    }
    updateInterval = setInterval(fetchTasks, settings.refreshInterval);
}

function setRefreshInterval(value) {
    const interval = Number(value);
    if (!Number.isFinite(interval) || interval < 100) {
        return;
    }
    settings.refreshInterval = interval;
    saveSettings();
    restartUpdateTimer();
}

function setHideOrphanChildren(checked) {
    settings.hideOrphanChildren = Boolean(checked);
    saveSettings();
    render();
}

function toggleSettings() {
    const panel = document.getElementById('settingsPanel');
    const btn = document.getElementById('settingsBtn');
    const willOpen = panel.hidden;
    panel.hidden = !willOpen;
    btn.classList.toggle('is-active', willOpen);
    btn.setAttribute('aria-expanded', String(willOpen));
}

function toggleTreeNode(id) {
    if (expandedTreeNodes.has(id)) {
        expandedTreeNodes.delete(id);
    }
    else {
        expandedTreeNodes.add(id);
    }
    render();
}

// When the tree is clicked
function toggleTree(event, id) {
    event.stopPropagation();
    toggleTreeNode(id);
}

// Generic handle click
function handleRowClick(task) {
    const hasChildren = task.children && task.children.length > 0;
    
    if (hasChildren) {
        toggleTreeNode(task.id);
    }
    else {
        // If is the deepest node, toggle stacktrace
        toggleStackTrace(task.id);
    }
}

async function fetchTasks() {
    if (isPaused) return;

    try {
        // [
        //     {
        //         "id": 0,
        //         "name": "Task 1",
        //         "state": "Running or Idle or Yielded or Completed",
        //         "total_time": 1234,
        //         "busy_time": 1000,
        //         "resumes": 5,
        //         "location": "main.cpp:114514"
        //     },
        // ]
        const response = await fetch('/api/tasks');
        const data = await response.json();

        tasks = data;
        render();
    }
    catch (error) {
        console.error("Failed to fetch tasks:", error);
    }
}

function escapeHtml(unsafe) {
    if (unsafe === undefined || unsafe === null) return '';
    return unsafe.toString()
            .replace(/&/g, "&amp;")
            .replace(/</g, "&lt;")
            .replace(/>/g, "&gt;")
            .replace(/"/g, "&quot;")
            .replace(/'/g, "&#039;");
}

function cleanCppSignature(signature) {
    if (!signature) return '';
    let str = signature.trim();

    // Remove return type
    str = str.replace(/(?:\s+(?:const|volatile|&|&&|noexcept))+$/g, '').trim();

    // Split parameters
    let paramStart = -1;
    let pDepth = 0;
    if (str.endsWith(')')) {
        for (let i = str.length - 1; i >= 0; i--) {
            if (str[i] === ')') pDepth++;
            else if (str[i] === '(') {
                pDepth--;
                if (pDepth === 0) {
                    paramStart = i;
                    break;
                }
            }
        }
    }
    
    // Left [return type] [call] function<template>
    if (paramStart !== -1) {
        str = str.substring(0, paramStart).trim();
    }

    // Remove calling convention if any
    const ccRegex = /\b__(cdecl|stdcall|thiscall|fastcall|vectorcall|clrcall|pascal)\b/g;
    let match;
    let lastCcIndex = -1;
    let lastCcLength = 0;
    while ((match = ccRegex.exec(str)) !== null) {
        lastCcIndex = match.index;
        lastCcLength = match[0].length;
    }

    if (lastCcIndex !== -1) {
        return str.substring(lastCcIndex + lastCcLength).trim();
    }

    // Fallback
    let angleDepth = 0;
    let parenDepth = 0;
    for (let i = str.length - 1; i >= 0; i--) {
        let char = str[i];
        if (char === '>') angleDepth++;
        else if (char === '<') angleDepth--;
        else if (char === ')') parenDepth++;
        else if (char === '(') parenDepth--;
        else if (char === ' ' && angleDepth === 0 && parenDepth === 0) {
            let leftPart = str.substring(0, i).trimEnd();
            if (leftPart.endsWith('operator')) {
                continue; 
            }
            str = str.substring(i + 1).trim();
            break;
        }
    }

    return str;
}

async function toggleStackTrace(id) {
    if (expandedTasks.has(id)) {
        expandedTasks.delete(id);
        render();
        return;
    }
    expandedTasks.add(id);
    
    stackTraces[id] = '<div class="st-status">Loading stacktrace...</div>';
    render();

    try {
        const response = await fetch(`/api/stacktrace/${id}`);
        if (!response.ok) throw new Error(`HTTP error! status: ${response.status}`);
        const data = await response.json();
        
        if (Array.isArray(data) && data.length > 0) {
            let htmlStr = '';
            data.forEach((frame, index) => {
                const cleanFunc = cleanCppSignature(frame.function);
                const func = escapeHtml(cleanFunc);
                const file = escapeHtml(frame.file || 'unknown_file');
                const line = escapeHtml(frame.line || '0');
                const message = frame.message ? escapeHtml(frame.message) : '';

                htmlStr += `
                    <div class="st-frame">
                        <span class="st-frame-index">#${index}</span>
                        <div class="st-frame-main">
                            <div class="st-header">
                                <span class="st-func">${func}</span>
                            </div>
                            <div class="st-location">
                                <span class="st-keyword">at</span>
                                <span class="st-file">${file}</span>:<span class="st-line">${line}</span>
                                ${message ? `<span class="st-message">[${message}]</span>` : ''}
                            </div>
                        </div>
                    </div>
                `;
            });
            stackTraces[id] = htmlStr;
        }
        else {
            stackTraces[id] = '<div class="st-status st-empty">No stacktrace available.</div>';
        }
    }
    catch (error) {
        console.error(`Failed to fetch stacktrace for task ${id}:`, error);
        stackTraces[id] = '<div class="st-status st-error">Error: Failed to load stacktrace.</div>';
    }
    
    if (expandedTasks.has(id)) {
        render();
    }
}

// Render
function formatTime(ms) {
    if (ms < 1) return ms.toFixed(2) + 'ms';
    if (ms < 1000) return ms.toFixed(0) + 'ms';
    return (ms / 1000).toFixed(2) + 's';
}

function render() {
    const tbody = document.getElementById('task-body');
    tbody.innerHTML = '';

    // Build the tree
    const taskMap = new Map();
    const parentByChild = new Map();
    const childrenByParent = new Map();
    let runningCount = 0;

    tasks.forEach(t => {
        taskMap.set(t.id, t);
        childrenByParent.set(t.id, []);
        if (t.state === 'Running') runningCount++;
    });

    const addChild = (parentId, childId) => {
        if (parentId === undefined || parentId === null || parentId === 0 || parentId === childId) {
            return;
        }
        if (!taskMap.has(parentId) || !taskMap.has(childId)) {
            return;
        }
        const children = childrenByParent.get(parentId);
        if (!children.includes(childId)) {
            children.push(childId);
        }
        parentByChild.set(childId, parentId);
    };

    tasks.forEach(t => {
        if (Array.isArray(t.children)) {
            t.children.forEach(childId => addChild(t.id, childId));
        }
        addChild(t.parent_id ?? t.parentId, t.id);
    });

    // Get the root
    let rootTasks = tasks.filter(t => !parentByChild.has(t.id));
    if (settings.hideOrphanChildren) {
        rootTasks = rootTasks.filter(t => {
            const id = Number(t.id);
            const parentId = t.parent_id ?? t.parentId;
            const missingParent = parentId === undefined ||
                parentId === null ||
                Number(parentId) === 0 ||
                !taskMap.has(parentId);
            return !(id < 0 && missingParent);
        });
    }

    // Sort it
    const sortNodes = (nodes) => {
        return nodes.sort((a, b) => {
            let valA = a[sortKey];
            let valB = b[sortKey];
            if (typeof valA === 'string') {
                return sortAsc ? valA.localeCompare(valB) : valB.localeCompare(valA);
            }
            return sortAsc ? (valA || 0) - (valB || 0) : (valB || 0) - (valA || 0);
        });
    };

    // Flat the tree
    const renderList = [];
    const buildRenderList = (nodes, depth) => {
        sortNodes(nodes);
        for (let t of nodes) {
            t._depth = depth;
            renderList.push(t);
            
            const childrenIds = childrenByParent.get(t.id) || [];
            const hasChildren = childrenIds.length > 0;
            if (hasChildren && expandedTreeNodes.has(t.id)) {
                let childrenNodes = childrenIds
                    .map(cid => taskMap.get(cid))
                    .filter(Boolean);
                buildRenderList(childrenNodes, depth + 1);
            }
        }
    };

    buildRenderList(rootTasks, 0);

    // Render the table
    renderList.forEach(t => {
        const childrenIds = childrenByParent.get(t.id) || [];
        const hasChildren = childrenIds.length > 0;
        const treeOpen = hasChildren && expandedTreeNodes.has(t.id);
        const traceOpen = !hasChildren && expandedTasks.has(t.id);
        const isExpanded = treeOpen || traceOpen;
        const busyRatio = Math.min((t.busy_time / t.total_time) * 100, 100) || 0;
        const childCount = childrenIds.length;
        const safeName = escapeHtml(t.name || '(anonymous)');
        const safeLocation = escapeHtml(t.location || '');
        const state = String(t.state || 'Unknown');
        const safeState = escapeHtml(state);
        const stateClass = escapeHtml(state.replace(/[^\w-]/g, ''));
        
        const tr = document.createElement('tr');
        tr.className = [
            'clickable-row',
            hasChildren ? 'tree-parent' : 'tree-leaf',
            isExpanded ? 'expanded' : '',
            traceOpen ? 'trace-open' : ''
        ].filter(Boolean).join(' ');
        tr.onclick = () => handleRowClick(t); 
        
        const indentPx = t._depth * 22;
        const nestedClass = t._depth > 0 ? ' is-nested' : '';
        const toggleLabel = hasChildren
            ? `${treeOpen ? 'Collapse' : 'Expand'} task ${t.id} children`
            : `${traceOpen ? 'Hide' : 'Show'} task ${t.id} stacktrace`;

        tr.innerHTML = `
            <td class="cell-id">
                <div class="tree-cell${nestedClass}" style="--tree-indent: ${indentPx}px; --tree-guide: ${indentPx - 11}px;">
                    <button class="tree-toggle ${hasChildren ? 'has-children' : 'is-leaf'}" type="button" aria-label="${escapeHtml(toggleLabel)}" aria-expanded="${isExpanded}">
                        <span class="tree-chevron"></span>
                    </button>
                    <span class="task-id">#${escapeHtml(t.id)}</span>
                    ${hasChildren ? `<span class="child-count" title="Children">${childCount}</span>` : '<span class="trace-dot" title="Stacktrace"></span>'}
                </div>
            </td>
            <td class="cell-name">${safeName}</td>
            <td class="cell-location" title="${safeLocation}">${safeLocation || '-'}</td>
            <td class="state-${stateClass}">${safeState}</td>
            <td>${formatTime(t.total_time)}</td>
            <td>
                ${formatTime(t.busy_time)}
                <div class="bar-container"><div class="bar-fill" style="width: ${busyRatio}%"></div></div>
            </td>
            <td>${t.resumes}</td>
        `;
        const toggleBtn = tr.querySelector('.tree-toggle');
        toggleBtn.onclick = (event) => {
            event.stopPropagation();
            handleRowClick(t);
        };
        tbody.appendChild(tr);

        // If the deepest and stacktrace was shown, show the stacktrace
        if (!hasChildren && isExpanded) {
            const stackTr = document.createElement('tr');
            stackTr.className = 'stack-row';
            stackTr.innerHTML = `
                <td colspan="7" class="stack-cell" style="--stack-indent: ${30 + indentPx}px;">
                    <section class="stack-panel">
                        <div class="stack-title">
                            <span>Stacktrace</span>
                            <span class="stack-meta">task #${escapeHtml(t.id)}</span>
                        </div>
                        <div class="stack-container">${stackTraces[t.id] || '<div class="st-status">Loading stacktrace...</div>'}</div>
                    </section>
                </td>
            `;
            tbody.appendChild(stackTr);
        }
    });

    // Update state
    document.getElementById('stats').innerText = `Tasks: ${tasks.length} | Running: ${runningCount}`;
    
    // Sort indicators
    document.querySelectorAll('th span').forEach(el => {
        el.innerText = '';
        el.removeAttribute('data-dir');
    });
    document.getElementById(`sort-${sortKey}`).dataset.dir = sortAsc ? 'asc' : 'desc';
}

function setSort(key) {
    if (sortKey === key) {
        sortAsc = !sortAsc;
    }
    else {
        sortKey = key;
        sortAsc = true;
    }
    render();
}

function togglePause() {
    isPaused = !isPaused;
    const btn = document.getElementById('pauseBtn');
    btn.innerText = isPaused ? 'Resume' : 'Pause';
    btn.classList.toggle('is-paused', isPaused);
    if (!isPaused) fetchTasks();
}

// Start it
loadSettings();
syncSettingsControls();
fetchTasks();
restartUpdateTimer();
