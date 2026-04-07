// State
let tasks = [];
let isPaused = false;
let sortKey = 'id';
let sortAsc = true;
let updateInterval = null;

// State for stacktrace
let expandedTasks = new Set();
let stackTraces = {};

// State for tree
let expandedTreeNodes = new Set();

// When the tree is clicked
function toggleTree(event, id) {
    event.stopPropagation(); 
    if (expandedTreeNodes.has(id)) {
        expandedTreeNodes.delete(id);
    }
    else {
        expandedTreeNodes.add(id);
    }
    render();
}

// Generic handle click
function handleRowClick(task) {
    const hasChildren = task.children && task.children.length > 0;
    
    if (hasChildren) {
        if (expandedTreeNodes.has(task.id)) {
            expandedTreeNodes.delete(task.id);
        }
        else {
            expandedTreeNodes.add(task.id);
        }
        render();
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
    
    stackTraces[id] = '<div style="color:#888;">Loading stacktrace...</div>';
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
                        <div class="st-header">
                            <span class="st-frame-index">#${index}</span>
                            <span class="st-func">${func}</span>
                        </div>
                        <div class="st-location">
                            <span class="st-keyword">at</span>
                            <span class="st-file">${file}</span>:<span class="st-line">${line}</span>
                            ${message ? `<span class="st-message">[${message}]</span>` : ''}
                        </div>
                    </div>
                `;
            });
            stackTraces[id] = htmlStr;
        }
        else {
            stackTraces[id] = '<div style="color:#888;">No stacktrace available.</div>';
        }
    }
    catch (error) {
        console.error(`Failed to fetch stacktrace for task ${id}:`, error);
        stackTraces[id] = '<div style="color: #f48771;">Error: Failed to load stacktrace.</div>';
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
    const childToParent = new Map();
    let runningCount = 0;

    tasks.forEach(t => {
        taskMap.set(t.id, t);
        if (t.state === 'Running') runningCount++;
        if (t.children && Array.isArray(t.children)) {
            t.children.forEach(childId => childToParent.set(childId, t.id));
        }
    });

    // Get the root
    let rootTasks = tasks.filter(t => !childToParent.has(t.id));

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
            
            const hasChildren = t.children && t.children.length > 0;
            if (hasChildren && expandedTreeNodes.has(t.id)) {
                let childrenNodes = t.children
                    .map(cid => taskMap.get(cid))
                    .filter(Boolean);
                buildRenderList(childrenNodes, depth + 1);
            }
        }
    };

    buildRenderList(rootTasks, 0);

    // Render the table
    renderList.forEach(t => {
        const hasChildren = t.children && t.children.length > 0;
        
        const isExpanded = hasChildren ? expandedTreeNodes.has(t.id) : expandedTasks.has(t.id);
        const busyRatio = Math.min((t.busy_time / t.total_time) * 100, 100) || 0;
        
        const tr = document.createElement('tr');
        tr.className = 'clickable-row' + (isExpanded ? ' expanded' : '');
        tr.onclick = () => handleRowClick(t); 
        
        const indentPx = t._depth * 20;

        tr.innerHTML = `
            <td style="padding-left: ${12 + indentPx}px;">
                <span class="expand-icon">▶</span> ${t.id}
            </td>
            <td>${t.name}</td>
            <td class="cell-location" title="${t.location || ''}">${t.location || '-'}</td>
            <td class="state-${t.state}">${t.state}</td>
            <td>${formatTime(t.total_time)}</td>
            <td>
                ${formatTime(t.busy_time)}
                <div class="bar-container"><div class="bar-fill" style="width: ${busyRatio}%"></div></div>
            </td>
            <td>${t.resumes}</td>
        `;
        tbody.appendChild(tr);

        // If the deepest and stacktrace was shown, show the stacktrace
        if (!hasChildren && isExpanded) {
            const stackTr = document.createElement('tr');
            stackTr.className = 'stack-row';
            stackTr.innerHTML = `
                <td colspan="7" style="padding: 10px 20px; padding-left: ${30 + indentPx}px;">
                    <pre class="stack-container">${stackTraces[t.id]}</pre>
                </td>
            `;
            tbody.appendChild(stackTr);
        }
    });

    // Update state
    document.getElementById('stats').innerText = `Tasks: ${tasks.length} | Running: ${runningCount}`;
    
    // Sort indicators
    document.querySelectorAll('th span').forEach(el => el.innerText = '');
    document.getElementById(`sort-${sortKey}`).innerText = sortAsc ? ' ▲' : ' ▼';
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
    btn.style.background = isPaused ? '#e51400' : '#007acc';
    if (!isPaused) fetchTasks();
}

// Start it
fetchTasks();
updateInterval = setInterval(fetchTasks, 1000);