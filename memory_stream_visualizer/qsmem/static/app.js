// author: Claude Code
//
// Client for the qsmem visualizer server (see ../server.py). Unlike the
// old inline-HTML tool this replaces, no trace data is embedded in the
// page: everything is fetched on demand from /api/meta, /api/window and
// /api/epoch/<i>, so pan/zoom cost is proportional to what's on screen,
// not to the size of the trace.

const MAX_COLUMNS = 1500;

const COL_RESIDENT = '#4a90e2';
const COL_LOADED   = '#27ae60';
const COL_EVICTED  = '#e74c3c';
const COL_ABSENT   = '#eef0f4';
const COL_HOT_BAND = 'rgba(231,76,60,.18)';

let meta = null;
let win  = null;   // last /api/window response

let viewStart = 0;
let viewWidth = 300;
let maxDisplayQubits = 64;
let hotThresholdK = 2.0;
let selectedEpoch = -1;

const METRIC_DEFS = [
    { key: 'compute_since_prev', label: 'Compute/epoch',      color: '#2980b9', enabled: true },
    { key: 'intensity',          label: 'Compute intensity',  color: '#8e44ad', enabled: true },
    { key: 'mem_accesses',       label: 'Mem accesses/epoch', color: '#e67e22', enabled: true },
    { key: 'unused_bw',          label: 'Unused BW',          color: '#7f8c8d', enabled: false },
    { key: 'front_width',        label: 'Front-layer width',  color: '#16a085', enabled: false },
];

function fmtK(n) {
    if (n >= 1e6) return (n/1e6).toFixed(1) + 'M';
    if (n >= 1e3) return (n/1e3).toFixed(1) + 'K';
    return '' + n;
}
function fmtNum(n) {
    if (!isFinite(n)) return '∞';
    if (n >= 1e6) return (n/1e6).toFixed(2) + 'M';
    if (n >= 1e3) return (n/1e3).toFixed(1) + 'K';
    if (n >= 1)   return n.toFixed(1);
    return n.toFixed(3);
}

async function getJSON(url) {
    const resp = await fetch(url);
    if (!resp.ok) throw new Error(`${url}: HTTP ${resp.status}`);
    return resp.json();
}

// ================================================================
//  BOOTSTRAP
// ================================================================

async function main() {
    meta = await getJSON('/api/meta');
    viewWidth = Math.min(meta.total_epochs, 300) || 1;
    maxDisplayQubits = Math.min(64, meta.ever_resident_qubits.length) || 1;

    document.getElementById('inp-max-qubits').max = meta.ever_resident_qubits.length;
    document.getElementById('inp-max-qubits').value = maxDisplayQubits;

    buildSummaryCards();
    buildMetricLegend();
    renderDistributions();
    wireControls();

    await refresh();
}

function buildSummaryCards() {
    const ci = meta.total_accesses > 0 ? (unrolledFromWindowGuess()) : null;
    const container = document.getElementById('summary-cards');
    const items = [
        { label: 'Scheduler', value: meta.scheduler },
        { label: 'Program Qubits', value: meta.num_qubits.toLocaleString() },
        { label: 'Active-Set Capacity', value: meta.active_set_capacity },
        { label: 'Scheduling Epochs', value: meta.total_epochs.toLocaleString() },
        { label: 'Memory Accesses', value: meta.total_accesses.toLocaleString() },
        { label: 'Ever-Resident Qubits', value: meta.ever_resident_qubits.length },
    ];
    if (meta.truncated) {
        items.push({ label: 'File Status', value: 'RECOVERED (truncated)', hot: true });
    }
    for (const it of items) {
        const card = document.createElement('div');
        card.className = 'card';
        card.innerHTML = `<div class="label">${it.label}</div>`
                       + `<div class="value${it.hot ? ' hot' : ''}">${it.value}</div>`;
        container.appendChild(card);
    }
}
function unrolledFromWindowGuess() { return null; } // placeholder, no longer needed once window loads

function buildMetricLegend() {
    const container = document.getElementById('metric-legend');
    for (const m of METRIC_DEFS) {
        const lbl = document.createElement('label');
        const cb = document.createElement('input');
        cb.type = 'checkbox';
        cb.checked = m.enabled;
        cb.onchange = () => { m.enabled = cb.checked; renderMetrics(); };
        const sw = document.createElement('span');
        sw.className = 'metric-swatch';
        sw.style.background = m.color;
        lbl.append(cb, sw, ' ', m.label);
        container.appendChild(lbl);
    }
}

function wireControls() {
    document.getElementById('heatmap-scrollbar').addEventListener('input', e => scrollTo(+e.target.value));
    document.getElementById('metrics-scrollbar').addEventListener('input', e => scrollTo(+e.target.value));

    document.getElementById('btn-zoom-in').addEventListener('click', () => {
        const center = viewStart + viewWidth / 2;
        viewWidth = Math.max(10, Math.floor(viewWidth * 0.6));
        scrollTo(Math.floor(center - viewWidth / 2));
    });
    document.getElementById('btn-zoom-out').addEventListener('click', () => {
        const center = viewStart + viewWidth / 2;
        viewWidth = Math.min(meta.total_epochs, Math.ceil(viewWidth / 0.6));
        scrollTo(Math.floor(center - viewWidth / 2));
    });
    document.getElementById('btn-zoom-fit').addEventListener('click', () => {
        viewWidth = meta.total_epochs;
        scrollTo(0);
    });

    const gotoInst = () => {
        const el = document.getElementById('inp-goto-inst');
        const target = +el.value;
        if (!Number.isFinite(target) || el.value === '') return;
        goToInstruction(target);
    };
    document.getElementById('btn-goto-inst').addEventListener('click', gotoInst);
    document.getElementById('inp-goto-inst').addEventListener('keydown', e => {
        if (e.key === 'Enter') gotoInst();
    });

    document.getElementById('inp-max-qubits').addEventListener('change', e => {
        maxDisplayQubits = Math.max(1, Math.min(+e.target.value, meta.ever_resident_qubits.length));
        e.target.value = maxDisplayQubits;
        refresh();
    });

    document.getElementById('sld-threshold').addEventListener('input', e => {
        hotThresholdK = +e.target.value;
        document.getElementById('lbl-threshold').textContent = hotThresholdK.toFixed(2);
        renderHeatmap();
        renderMetrics();
    });

    const hm = document.getElementById('heatmap');
    hm.addEventListener('wheel', e => {
        e.preventDefault();
        const rect = hm.getBoundingClientRect();
        const pivotCol = Math.floor((e.clientX - rect.left) / (hm.width / viewWidth));
        const pivotEpoch = viewStart + pivotCol;
        const factor = e.deltaY > 0 ? 1.25 : 0.8;
        viewWidth = Math.max(10, Math.min(meta.total_epochs, Math.round(viewWidth * factor)));
        scrollTo(Math.round(pivotEpoch - pivotCol));
    }, { passive: false });
    hm.addEventListener('click', e => onHeatmapClick(e));
    hm.addEventListener('mousemove', e => onHeatmapHover(e));
    hm.addEventListener('mouseleave', hideTooltip);

    const mc = document.getElementById('metrics-canvas');
    mc.addEventListener('click', e => onMetricsClick(e));
    mc.addEventListener('mousemove', e => onMetricsHover(e));
    mc.addEventListener('mouseleave', hideTooltip);

    window.addEventListener('resize', () => { renderHeatmap(); renderMetrics(); renderDistributions(); });
}

function scrollTo(newStart) {
    viewStart = Math.max(0, Math.min(newStart, Math.max(0, meta.total_epochs - viewWidth)));
    syncScrollbars();
    refresh();
}

function syncScrollbars() {
    const maxStart = Math.max(0, meta.total_epochs - viewWidth);
    for (const id of ['heatmap-scrollbar', 'metrics-scrollbar']) {
        const el = document.getElementById(id);
        el.max = maxStart;
        el.value = viewStart;
    }
    document.getElementById('lbl-qubit-info').textContent =
        `viewing ${viewWidth.toLocaleString()} of ${meta.total_epochs.toLocaleString()} epochs`;
}

// ================================================================
//  WINDOW FETCH + RENDER
// ================================================================

let refreshToken = 0;
async function refresh() {
    syncScrollbars();
    const token = ++refreshToken;
    const columns = Math.min(viewWidth, MAX_COLUMNS);
    const url = `/api/window?start=${viewStart}&count=${viewWidth}&columns=${columns}&top=${maxDisplayQubits}`;
    const result = await getJSON(url);
    if (token !== refreshToken) return; // a newer request superseded this one
    win = result;
    document.getElementById('lbl-qubit-coverage').textContent =
        `(showing ${win.qubits.length} of ${meta.ever_resident_qubits.length} ever-resident qubits, sorted by access count)`;
    renderHeatmap();
    renderMetrics();
}

function bucketHeat(i) {
    return win.mem_accesses[i] / Math.max(1, win.compute_since_prev[i]);
}
function computeHotRegions() {
    if (!win) return [];
    const threshold = meta.heat_mean + hotThresholdK * meta.heat_std;
    const regions = [];
    let inRegion = false, rStart = 0;
    for (let i = 0; i < win.num_buckets; i++) {
        if (bucketHeat(i) > threshold) {
            if (!inRegion) { inRegion = true; rStart = i; }
        } else if (inRegion) {
            regions.push([rStart, i - 1]);
            inRegion = false;
        }
    }
    if (inRegion) regions.push([rStart, win.num_buckets - 1]);
    return regions;
}

// ================================================================
//  HEATMAP
// ================================================================

const hmCanvas  = document.getElementById('heatmap');
const hmCtx     = hmCanvas.getContext('2d');
const yaxCanvas = document.getElementById('heatmap-y-axis');
const yaxCtx    = yaxCanvas.getContext('2d');
const xaxCanvas = document.getElementById('heatmap-x-axis');
const xaxCtx    = xaxCanvas.getContext('2d');

function renderHeatmap() {
    if (!win) return;
    const wrapper = document.getElementById('heatmap-scroll-wrapper');
    const W = wrapper.clientWidth || 800;
    const nRows = win.qubits.length || 1;
    const cellH = Math.max(1, Math.min(24, Math.floor(400 / nRows)));
    const H = Math.max(60, nRows * cellH);

    hmCanvas.width = W; hmCanvas.height = H;
    xaxCanvas.width = W;
    yaxCanvas.height = H;

    const nCols = win.num_buckets || 1;
    const cellW = W / nCols;
    const hotRegions = computeHotRegions();

    hmCtx.clearRect(0, 0, W, H);
    hmCtx.fillStyle = '#f8f9fb';
    hmCtx.fillRect(0, 0, W, H);

    hmCtx.fillStyle = COL_HOT_BAND;
    for (const [lo, hi] of hotRegions) {
        const x0 = lo * cellW, x1 = (hi + 1) * cellW;
        hmCtx.fillRect(x0, 0, x1 - x0, H);
    }

    for (let col = 0; col < nCols; col++) {
        const x = col * cellW;
        for (let row = 0; row < nRows; row++) {
            const s = win.state[row][col];
            if (s === 2)      hmCtx.fillStyle = COL_LOADED;
            else if (s === 3) hmCtx.fillStyle = COL_EVICTED;
            else if (s === 1) hmCtx.fillStyle = COL_RESIDENT;
            else              hmCtx.fillStyle = COL_ABSENT;
            hmCtx.fillRect(x + 0.5, row * cellH + 0.5, Math.max(cellW - 1, 0.5), Math.max(cellH - 1, 0.5));
        }
    }

    if (cellH > 5) {
        hmCtx.strokeStyle = 'rgba(200,210,225,.5)';
        hmCtx.lineWidth = 0.5;
        for (let row = 1; row < nRows; row++) {
            hmCtx.beginPath();
            hmCtx.moveTo(0, row * cellH);
            hmCtx.lineTo(W, row * cellH);
            hmCtx.stroke();
        }
    }

    if (selectedEpoch >= win.start && selectedEpoch < win.end) {
        const col = bucketForEpoch(selectedEpoch);
        if (col >= 0) {
            const x = col * cellW;
            hmCtx.strokeStyle = 'rgba(243,156,18,.9)';
            hmCtx.lineWidth = Math.max(1.5, cellW * 0.15);
            hmCtx.strokeRect(x + 1, 1, Math.max(cellW - 2, 1), H - 2);
        }
    }

    renderYAxis(nRows, cellH);
    renderXAxis(cellW, nCols);
    updateHotRegionList(hotRegions);
}

function bucketForEpoch(epoch) {
    if (!win || epoch < win.start || epoch >= win.end) return -1;
    return Math.min(win.num_buckets - 1, Math.floor((epoch - win.start) / win.bucket_size));
}

function renderYAxis(nRows, cellH) {
    const H = yaxCanvas.height;
    yaxCtx.clearRect(0, 0, 52, H);
    yaxCtx.fillStyle = '#f0f2f5';
    yaxCtx.fillRect(0, 0, 52, H);
    yaxCtx.fillStyle = '#444';
    yaxCtx.font = `${Math.min(cellH - 1, 11)}px monospace`;
    yaxCtx.textAlign = 'right';
    const step = Math.max(1, Math.ceil(12 / cellH));
    for (let row = 0; row < nRows; row += step) {
        yaxCtx.fillText('q' + win.qubits[row], 50, row * cellH + cellH / 2 + 4);
    }
}

function renderXAxis(cellW, nCols) {
    const W = xaxCanvas.width;
    xaxCtx.clearRect(0, 0, W, 32);
    xaxCtx.fillStyle = '#f0f2f5';
    xaxCtx.fillRect(0, 0, W, 32);
    xaxCtx.fillStyle = '#555';
    xaxCtx.font = '10px sans-serif';
    xaxCtx.textAlign = 'center';
    const step = Math.max(1, Math.ceil(60 / cellW));
    for (let col = 0; col < nCols; col += step) {
        const x = col * cellW + cellW / 2;
        const ei = win.epoch_first[col];
        xaxCtx.fillStyle = '#555';
        xaxCtx.fillText('E' + ei, x, 12);
        xaxCtx.fillStyle = '#888';
        xaxCtx.fillText(fmtK(win.inst_done[col]), x, 24);
    }
}

function updateHotRegionList(hotRegions) {
    const el = document.getElementById('hot-region-list');
    if (hotRegions.length === 0) {
        el.textContent = 'No hot regions detected at current threshold (in visible window).';
        return;
    }
    const parts = hotRegions.slice(0, 12).map(([lo, hi]) => {
        const e0 = win.epoch_first[lo], e1 = win.epoch_first[hi];
        return `Epochs ${e0}–${e1}`;
    });
    if (hotRegions.length > 12) parts.push(`… and ${hotRegions.length - 12} more`);
    el.textContent = '⚠ Hot regions (visible window): ' + parts.join(' | ');
}

// ================================================================
//  METRICS
// ================================================================

const metricsCanvas = document.getElementById('metrics-canvas');
const mCtx = metricsCanvas.getContext('2d');

function metricValue(key, col) {
    switch (key) {
        case 'compute_since_prev': return win.compute_since_prev[col];
        case 'intensity': return win.mem_accesses[col] > 0 ? win.compute_since_prev[col] / win.mem_accesses[col] : win.compute_since_prev[col];
        case 'mem_accesses': return win.mem_accesses[col];
        case 'unused_bw': return win.unused_bw[col];
        case 'front_width': return win.front_width[col];
    }
    return 0;
}

function renderMetrics() {
    if (!win) return;
    const wrapper = document.getElementById('metrics-canvas-wrapper');
    metricsCanvas.width = wrapper.clientWidth || 800;
    const W = metricsCanvas.width, H = metricsCanvas.height;
    mCtx.clearRect(0, 0, W, H);
    mCtx.fillStyle = '#fcfcfd';
    mCtx.fillRect(0, 0, W, H);

    const nCols = win.num_buckets || 1;
    const cellW = W / nCols;
    const hotRegions = computeHotRegions();
    mCtx.fillStyle = COL_HOT_BAND;
    for (const [lo, hi] of hotRegions) mCtx.fillRect(lo * cellW, 0, (hi - lo + 1) * cellW, H);

    const enabled = METRIC_DEFS.filter(m => m.enabled);
    if (enabled.length === 0) return;
    const PAD_TOP = 8, PAD_BTM = 4;
    const rowH = (H - PAD_TOP - PAD_BTM) / enabled.length;

    for (let mi = 0; mi < enabled.length; mi++) {
        const m = enabled[mi];
        const yOff = PAD_TOP + mi * rowH;
        let minV = Infinity, maxV = -Infinity;
        for (let col = 0; col < nCols; col++) {
            const v = metricValue(m.key, col);
            if (v < minV) minV = v;
            if (v > maxV) maxV = v;
        }
        const range = maxV - minV || 1;

        if (mi > 0) {
            mCtx.strokeStyle = '#ddd'; mCtx.lineWidth = 0.5;
            mCtx.beginPath(); mCtx.moveTo(0, yOff); mCtx.lineTo(W, yOff); mCtx.stroke();
        }
        mCtx.fillStyle = m.color; mCtx.font = '11px sans-serif'; mCtx.textAlign = 'left';
        mCtx.fillText(m.label, 4, yOff + 11);
        mCtx.fillStyle = '#888'; mCtx.textAlign = 'right';
        mCtx.fillText(fmtNum(maxV), W - 2, yOff + 11);

        mCtx.beginPath();
        for (let col = 0; col < nCols; col++) {
            const v = metricValue(m.key, col);
            const x = col * cellW + cellW / 2;
            const y = yOff + rowH - ((v - minV) / range) * (rowH - 16) - 4;
            if (col === 0) mCtx.moveTo(x, y); else mCtx.lineTo(x, y);
        }
        mCtx.strokeStyle = m.color; mCtx.lineWidth = 1.5; mCtx.stroke();

        if (selectedEpoch >= win.start && selectedEpoch < win.end) {
            const col = bucketForEpoch(selectedEpoch);
            const v = metricValue(m.key, col);
            const x = col * cellW + cellW / 2;
            const y = yOff + rowH - ((v - minV) / range) * (rowH - 16) - 4;
            mCtx.beginPath(); mCtx.arc(x, y, 4, 0, 2 * Math.PI);
            mCtx.fillStyle = 'rgba(243,156,18,.9)'; mCtx.fill();
        }
    }
}

// ================================================================
//  INTERACTION
// ================================================================

function bucketFromMouseX(canvas, clientX) {
    const rect = canvas.getBoundingClientRect();
    const relX = clientX - rect.left;
    return Math.floor(relX / (canvas.width / win.num_buckets));
}

function onHeatmapClick(e) {
    if (!win) return;
    const col = bucketFromMouseX(hmCanvas, e.clientX);
    if (col < 0 || col >= win.num_buckets) return;
    selectEpoch(win.epoch_first[col]);
}
function onMetricsClick(e) {
    if (!win) return;
    const col = bucketFromMouseX(metricsCanvas, e.clientX);
    if (col < 0 || col >= win.num_buckets) return;
    selectEpoch(win.epoch_first[col]);
}

function onHeatmapHover(e) {
    if (!win) return;
    const col = bucketFromMouseX(hmCanvas, e.clientX);
    if (col < 0 || col >= win.num_buckets) { hideTooltip(); return; }
    const rect = hmCanvas.getBoundingClientRect();
    const cellH = hmCanvas.height / win.qubits.length;
    const row = Math.floor((e.clientY - rect.top) / cellH);
    let qInfo = '';
    if (row >= 0 && row < win.qubits.length) {
        const s = win.state[row][col];
        const label = s === 2 ? 'loaded ▲' : s === 3 ? 'evicted ▼' : s === 1 ? 'resident' : 'absent';
        qInfo = `<br>q${win.qubits[row]}: <b>${label}</b>`;
    }
    showTooltip(e,
        `<b>Epoch ${win.epoch_first[col]}</b> (bucket of ${win.bucket_size})${qInfo}<br>` +
        `Compute: ${win.compute_since_prev[col].toLocaleString()}<br>` +
        `Mem accesses: ${win.mem_accesses[col]}, unused BW (max): ${win.unused_bw[col]}`);
}
function onMetricsHover(e) {
    if (!win) return;
    const col = bucketFromMouseX(metricsCanvas, e.clientX);
    if (col < 0 || col >= win.num_buckets) { hideTooltip(); return; }
    showTooltip(e,
        `<b>Epoch ${win.epoch_first[col]}</b><br>` +
        `Compute: ${win.compute_since_prev[col].toLocaleString()}<br>` +
        `Mem accesses: ${win.mem_accesses[col]}`);
}

const tooltip = document.getElementById('tooltip');
function showTooltip(e, html) {
    tooltip.innerHTML = html;
    tooltip.style.display = 'block';
    tooltip.style.left = (e.clientX + 12) + 'px';
    tooltip.style.top = (e.clientY + 12) + 'px';
}
function hideTooltip() { tooltip.style.display = 'none'; }

async function selectEpoch(epoch) {
    selectedEpoch = epoch;
    renderHeatmap();
    renderMetrics();
    const detail = await getJSON(`/api/epoch/${epoch}`);
    showEpochDetail(detail);
}

async function goToInstruction(instDone) {
    const found = await getJSON(`/api/find_epoch?inst_done=${Math.max(0, Math.floor(instDone))}`);
    const epoch = found.epoch;
    scrollTo(Math.floor(epoch - viewWidth / 2));
    await selectEpoch(epoch);
}

function showEpochDetail(d) {
    document.getElementById('epoch-detail-label').textContent = `— Epoch ${d.epoch}`;
    const panel = document.getElementById('epoch-detail');
    function chips(qs, cls) { return qs.map(q => `<span class="qubit-chip ${cls}">q${q}</span>`).join(''); }
    const intensity = d.cumulative_compute_intensity == null ? '∞' : d.cumulative_compute_intensity.toFixed(2);
    panel.innerHTML = `
      <div class="detail-block"><div class="dl">Epoch</div><div class="dv">${d.epoch} / ${d.total_epochs - 1}</div></div>
      <div class="detail-block"><div class="dl">Cumulative inst done</div><div class="dv">${d.inst_done.toLocaleString()}</div></div>
      <div class="detail-block"><div class="dl">Compute since last swap</div><div class="dv">${d.compute_since_prev.toLocaleString()} inst</div></div>
      <div class="detail-block"><div class="dl">Memory accesses (this epoch)</div><div class="dv">${d.memory_accesses} (${d.unused_bandwidth} unused BW)</div></div>
      <div class="detail-block"><div class="dl">Front-layer / DAG</div><div class="dv">${d.front_layer_width} / ${d.dag_inst_count} inst</div></div>
      <div class="detail-block"><div class="dl">Cumulative memory accesses</div><div class="dv">${d.cumulative_memory_accesses.toLocaleString()}</div></div>
      <div class="detail-block"><div class="dl">Cumulative compute intensity</div><div class="dv">${intensity}</div></div>
      <div class="detail-block" style="flex:1 1 100%"><div class="dl">Loaded (${d.loaded.length})</div><div class="dv">${chips(d.loaded, 'chip-loaded') || '<em style="color:var(--muted)">none</em>'}</div></div>
      <div class="detail-block" style="flex:1 1 100%"><div class="dl">Evicted (${d.evicted.length})</div><div class="dv">${chips(d.evicted, 'chip-evicted') || '<em style="color:var(--muted)">none</em>'}</div></div>
      <div class="detail-block" style="flex:1 1 100%"><div class="dl">Active set after swap (${d.active_set.length})</div><div class="dv">${chips(d.active_set, 'chip-active')}</div></div>`;
}

// ================================================================
//  DISTRIBUTIONS  (computed once from /api/meta, not per-window)
// ================================================================

function barChart(canvas, labels, values, color) {
    const ctx = canvas.getContext('2d');
    const W = canvas.parentElement.clientWidth - 4;
    canvas.width = Math.max(W, 120);
    const H = canvas.height;
    ctx.clearRect(0, 0, canvas.width, H);
    ctx.fillStyle = '#f8f9fb';
    ctx.fillRect(0, 0, canvas.width, H);
    if (values.length === 0) return;

    const maxV = Math.max(...values) || 1;
    const n = values.length;
    const PAD_L = 40, PAD_R = 4, PAD_T = 8, PAD_B = 28;
    const plotW = canvas.width - PAD_L - PAD_R, plotH = H - PAD_T - PAD_B;
    const barW = Math.max(1, plotW / n - 1);

    ctx.fillStyle = color + 'cc';
    for (let i = 0; i < n; i++) {
        const bh = (values[i] / maxV) * plotH;
        ctx.fillRect(PAD_L + i * (plotW / n), PAD_T + plotH - bh, barW, bh);
    }
    ctx.strokeStyle = '#aaa'; ctx.lineWidth = 0.8;
    ctx.beginPath();
    ctx.moveTo(PAD_L, PAD_T); ctx.lineTo(PAD_L, PAD_T + plotH); ctx.lineTo(PAD_L + plotW, PAD_T + plotH);
    ctx.stroke();
    ctx.fillStyle = '#666'; ctx.font = '10px sans-serif'; ctx.textAlign = 'right';
    ctx.fillText(fmtNum(maxV), PAD_L - 2, PAD_T + 8);
    ctx.fillText('0', PAD_L - 2, PAD_T + plotH);
    ctx.textAlign = 'center';
    const labelStep = Math.max(1, Math.ceil(n / 8));
    for (let i = 0; i < n; i += labelStep) ctx.fillText(labels[i], PAD_L + i * (plotW / n) + barW / 2, PAD_T + plotH + 12);
    if (n > 1) ctx.fillText(labels[n - 1], PAD_L + (n - 1) * (plotW / n) + barW / 2, PAD_T + plotH + 12);
}

function renderDistributions() {
    const access = meta.access_counts;
    const topQ = access.map((v, q) => [q, v]).filter(([, v]) => v > 0)
                        .sort((a, b) => b[1] - a[1]).slice(0, 30);
    barChart(document.getElementById('chart-qubit-access'), topQ.map(([q]) => 'q' + q), topQ.map(([, v]) => v), '#4a90e2');

    const spans = meta.span_lengths;
    if (spans.length > 0) {
        const maxLen = Math.max(...spans);
        const BINS = Math.min(40, maxLen + 1);
        const binW = Math.max(1, Math.ceil((maxLen + 1) / BINS));
        const hist = new Array(BINS).fill(0);
        for (const l of spans) hist[Math.min(Math.floor(l / binW), BINS - 1)]++;
        barChart(document.getElementById('chart-residency-span'),
                 Array.from({ length: BINS }, (_, i) => i * binW), hist, '#8e44ad');
    }

    const bwHist = meta.unused_bandwidth_hist;
    barChart(document.getElementById('chart-unused-bw'),
              Array.from({ length: bwHist.length }, (_, i) => i), bwHist, '#7f8c8d');
}

main().catch(e => {
    document.getElementById('app').innerHTML =
        `<div class="section"><h2>Failed to load trace</h2><p>${e.message}</p></div>`;
    console.error(e);
});
