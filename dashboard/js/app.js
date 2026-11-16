/**
 * NeuroGuard-RT: Cockpit Telemetry Controller & Fault Injection Dispatcher
 */

import { drawSparkline, drawWorkspace, drawOscilloscope } from './renderer.js';

const oscHistory = new Array(110).fill(0.0);
const sparkAiHist = new Array(40).fill(12.3);
const sparkSupHist = new Array(40).fill(1.45);

let latest = {
  state: 0,
  fault_mode: 0,
  fault_reason: 0,
  raw_x: 11.2,
  raw_y: 0,
  ai_x: 11.2,
  ai_y: 0,
  x: 11.2,
  y: 0,
  radius_mm: 11.2,
  offset_mm: 6.8,
  tip_force_n: 0.12,
  ai_latency_us: 12.3,
  reaction_us: 1.44,
  p99_reaction_us: 1.84,
  osc_sample_us: 0.08,
  p2p_jitter_us: 1.37,
  rms_jitter_us: 0.23,
  heartbeat_age_ms: 0.8,
  missed_deadlines: 0,
  sup_pid: 294931,
  ai_pid: 307220,
  resmgr_pid: 282642,
  uptime_s: 0,
  qnx_live: false
};

async function sendCtrl(cmd) {
  try {
    await fetch('/api/fault', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ cmd })
    });
  } catch (_) {
    // Non-fatal transient fetch error
  }
}

function fmtUptime(sec) {
  const h = String(Math.floor(sec / 3600)).padStart(2, '0');
  const m = String(Math.floor((sec % 3600) / 60)).padStart(2, '0');
  const s = String(sec % 60).padStart(2, '0');
  return `${h}:${m}:${s}`;
}

function updateDOM(d) {
  const clamped = d.state === 2;

  document.getElementById('src-label').textContent = d.qnx_live
    ? 'QNX SDP v8.0 (LIVE)'
    : 'QNX SDP v8.0 (TWIN)';
  document.getElementById('uptime-txt').textContent = fmtUptime(d.uptime_s || 0);

  document.getElementById('top-banner').classList.toggle('clamped', clamped);
  document.getElementById('ws-panel').classList.toggle('workspace-clamped', clamped);
  document.getElementById('clamp-badge').classList.toggle('active', clamped);

  document.getElementById('btn-starve').classList.toggle('active', d.fault_mode === 1);
  document.getElementById('btn-unsafe').classList.toggle('active', d.fault_mode === 2);

  const metaMode = document.getElementById('meta-mode');
  const clampTitle = document.getElementById('clamp-badge-title');

  if (clamped) {
    document.getElementById('banner-icon').textContent = '⚠️';
    document.getElementById('banner-headline').textContent = 'SAFE CLAMPED – FAULT DETECTED';
    document.getElementById('banner-sub').textContent =
      d.fault_reason === 1
        ? 'AI HEARTBEAT WATCHDOG TIMEOUT (>2.50 ms) — TIP LOCKED TO SAFE BOUNDARY'
        : 'UNSAFE AI TRAJECTORY INTERCEPTED — TIP POSITION LOCKED TO R = 18.00 mm';
    metaMode.textContent = 'SAFE CLAMPED';
    metaMode.style.color = '#f87171';

    document.getElementById('clamp-badge-icon').textContent = '🛑';
    clampTitle.textContent = 'SAFE CLAMPED';
    clampTitle.style.color = '#f87171';
    document.getElementById('clamp-badge-sub').textContent =
      'INSTRUMENT POSITION LOCKED TO SAFE BOUNDARY';
  } else {
    document.getElementById('banner-icon').textContent = '🛡️';
    document.getElementById('banner-headline').textContent =
      'NORMAL — AI TREMOR FILTER INSIDE SAFE ZONE';
    document.getElementById('banner-sub').textContent =
      '1kHz WATCHDOG NOMINAL • SAFE SURGICAL BOUNDARY R ≤ 18.00 mm';
    metaMode.textContent = 'NORMAL';
    metaMode.style.color = '#4ade80';

    document.getElementById('clamp-badge-icon').textContent = '🛡️';
    clampTitle.textContent = 'SAFE ENVELOPE NOMINAL';
    clampTitle.style.color = '#4ade80';
    document.getElementById('clamp-badge-sub').textContent =
      `AI DECISION: (${d.ai_x.toFixed(1)}, ${d.ai_y.toFixed(1)}) mm`;
  }

  const aiEl = document.getElementById('m-ai-lat');
  aiEl.textContent = `${d.ai_latency_us.toFixed(2)} µs`;
  aiEl.className = d.ai_latency_us > 850 ? 'm-val red' : 'm-val';

  document.getElementById('m-react').innerHTML =
    `${d.reaction_us.toFixed(2)} µs <small class="m-subval">/ p99: ${d.p99_reaction_us.toFixed(2)} µs</small>`;

  const missEl = document.getElementById('m-missed');
  missEl.textContent = d.missed_deadlines;
  missEl.className = d.missed_deadlines > 0 ? 'm-val red' : 'm-val green';

  document.getElementById('osc-p2p').textContent = `${d.p2p_jitter_us.toFixed(2)} µs`;
  document.getElementById('osc-rms').textContent = `${d.rms_jitter_us.toFixed(2)} µs`;

  document.getElementById('pid-sup').textContent = d.sup_pid || 294931;
  document.getElementById('pid-resmgr').textContent = d.resmgr_pid || 282642;
  document.getElementById('pid-ai').textContent = d.ai_pid || 307220;

  document.getElementById('row-sup').classList.toggle('clamped', clamped);
  const stSup = document.getElementById('st-sup');
  stSup.textContent = clamped ? 'CLAMPING (ACTIVE)' : 'RECEIVE';
  stSup.style.color = clamped ? '#f87171' : '#4ade80';

  const stAi = document.getElementById('st-ai');
  if (d.fault_mode === 1) {
    stAi.textContent = 'STARVED (PRIO 35r)';
    stAi.style.color = '#f87171';
  } else if (d.fault_mode === 2) {
    stAi.textContent = 'CORRUPTED OUTPUT';
    stAi.style.color = '#fbbf24';
  } else {
    stAi.textContent = 'READY (1kHz)';
    stAi.style.color = '#cbd5e1';
  }

  document.getElementById('b-radius').textContent = d.radius_mm.toFixed(2);
  document.getElementById('b-radius-sub').textContent = clamped
    ? 'RADIUS (LOCKED BOUNDARY)'
    : 'RADIUS FROM CENTER';
  document.getElementById('b-offset').textContent = d.offset_mm.toFixed(2);
  document.getElementById('b-offset-sub').textContent = clamped
    ? 'CLAMPED AT PERIMETER'
    : 'OFFSET FROM BOUNDARY';
  document.getElementById('b-force').textContent = d.tip_force_n.toFixed(2);
  document.getElementById('b-force-sub').textContent = clamped
    ? 'ZERO FORCE LOCK'
    : 'SAFE TISSUE CONTACT';

  const hbWrap = document.getElementById('b-hb-wrap');
  document.getElementById('b-hb').textContent = d.heartbeat_age_ms.toFixed(2);
  hbWrap.style.color = d.heartbeat_age_ms > 2.5 ? '#f87171' : '#22d3ee';
}

async function pollLoop() {
  try {
    const res = await fetch('/api/telemetry', { cache: 'no-store' });
    if (res.ok) {
      const d = await res.json();
      latest = d;

      oscHistory.push(d.osc_sample_us || 0);
      if (oscHistory.length > 110) oscHistory.shift();

      sparkAiHist.push(Math.min(100, d.ai_latency_us));
      if (sparkAiHist.length > 40) sparkAiHist.shift();

      sparkSupHist.push(d.reaction_us);
      if (sparkSupHist.length > 40) sparkSupHist.shift();

      updateDOM(d);
      drawSparkline('spark-ai', sparkAiHist, d.ai_latency_us > 850 ? '#f87171' : '#22d3ee', 5, 100);
      drawSparkline('spark-sup', sparkSupHist, '#4ade80', 0.5, 3.0);
    }
  } catch (_) {
    // Keep rendering smoothly across brief socket reconnects
  }
}

function renderLoop() {
  drawWorkspace(latest);
  drawOscilloscope(oscHistory);
  requestAnimationFrame(renderLoop);
}

function bindControls() {
  document.getElementById('btn-starve').addEventListener('click', () => sendCtrl('STARVE=1'));
  document.getElementById('btn-unsafe').addEventListener('click', () => sendCtrl('UNSAFE=1'));
  document.getElementById('btn-reset').addEventListener('click', () => sendCtrl('RESET'));
  document.getElementById('btn-mini-reset').addEventListener('click', () => sendCtrl('RESET'));

  window.addEventListener('keydown', (e) => {
    if (e.key === 's' || e.key === 'S') sendCtrl('STARVE=1');
    if (e.key === 'u' || e.key === 'U') sendCtrl('UNSAFE=1');
    if (e.key === 'r' || e.key === 'R') sendCtrl('RESET');
  });
}

document.addEventListener('DOMContentLoaded', () => {
  bindControls();
  setInterval(pollLoop, 45);
  requestAnimationFrame(renderLoop);
});