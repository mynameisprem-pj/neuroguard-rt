/**
 * NeuroGuard-RT: Real-Time Canvas Rendering Engine
 * Renders the R=18.00mm Safe Surgical Workspace, Jitter Oscilloscope, and Sparklines.
 */

const SAFE_R_MM = 18.00;
const VIEW_EXTENT_MM = 27.5;
const trailSafe = [];

export function drawSparkline(canvasId, arr, color, minV, maxV) {
  const c = document.getElementById(canvasId);
  if (!c) return;
  const ctx = c.getContext('2d');
  ctx.clearRect(0, 0, c.width, c.height);
  ctx.strokeStyle = color;
  ctx.lineWidth = 1.6;
  ctx.beginPath();
  for (let i = 0; i < arr.length; i++) {
    const xp = (i / (arr.length - 1)) * c.width;
    const norm = Math.max(0, Math.min(1, (arr[i] - minV) / (maxV - minV)));
    const yp = c.height - 3 - norm * (c.height - 6);
    if (i === 0) ctx.moveTo(xp, yp);
    else ctx.lineTo(xp, yp);
  }
  ctx.stroke();
}

export function drawWorkspace(latest) {
  const c = document.getElementById('ws-canvas');
  if (!c) return;
  const rect = c.parentElement.getBoundingClientRect();
  if (c.width !== Math.floor(rect.width) || c.height !== Math.floor(rect.height)) {
    c.width = Math.floor(rect.width);
    c.height = Math.floor(rect.height);
  }

  const ctx = c.getContext('2d');
  const w = c.width;
  const h = c.height;
  const cx = w / 2;
  const cy = h / 2;
  const pxPerMm = (Math.min(w, h) * 0.44) / VIEW_EXTENT_MM;
  const safePx = SAFE_R_MM * pxPerMm;

  ctx.clearRect(0, 0, w, h);

  // Subtle surgical polar grid
  ctx.strokeStyle = 'rgba(30, 58, 95, 0.35)';
  ctx.lineWidth = 1;
  [6, 12, 24].forEach((rmm) => {
    ctx.beginPath();
    ctx.arc(cx, cy, rmm * pxPerMm, 0, Math.PI * 2);
    ctx.stroke();
  });
  ctx.beginPath();
  ctx.moveTo(cx - safePx * 1.35, cy);
  ctx.lineTo(cx + safePx * 1.35, cy);
  ctx.moveTo(cx, cy - safePx * 1.35);
  ctx.lineTo(cx, cy + safePx * 1.35);
  ctx.stroke();

  // Organic surgical cavity glow inside R=18.00mm
  const grad = ctx.createRadialGradient(cx - safePx * 0.18, cy + safePx * 0.12, 8, cx, cy, safePx);
  grad.addColorStop(0, 'rgba(56, 189, 248, 0.13)');
  grad.addColorStop(0.65, 'rgba(15, 35, 62, 0.18)');
  grad.addColorStop(1, 'rgba(4, 10, 20, 0.02)');
  ctx.fillStyle = grad;
  ctx.beginPath();
  ctx.arc(cx, cy, safePx, 0, Math.PI * 2);
  ctx.fill();

  // Glowing Cyan/Red R = 18.00 mm Safe Boundary Ring
  const clamped = latest.state === 2;
  ctx.save();
  ctx.shadowBlur = 20;
  ctx.shadowColor = clamped ? '#ef4444' : '#22d3ee';
  ctx.strokeStyle = clamped ? '#f87171' : '#22d3ee';
  ctx.lineWidth = 2.6;
  ctx.beginPath();
  ctx.arc(cx, cy, safePx, 0, Math.PI * 2);
  ctx.stroke();
  ctx.restore();

  ctx.fillStyle = '#38bdf8';
  ctx.font = '600 13px "JetBrains Mono", monospace';
  ctx.fillText('R = 18.00 mm', cx + safePx * 0.18, cy - safePx * 0.68);

  const rawPxX = cx + latest.raw_x * pxPerMm;
  const rawPxY = cy - latest.raw_y * pxPerMm;
  const aiPxX = cx + latest.ai_x * pxPerMm;
  const aiPxY = cy - latest.ai_y * pxPerMm;
  const safePxX = cx + latest.x * pxPerMm;
  const safePxY = cy - latest.y * pxPerMm;

  // Safe end-effector motion trail
  trailSafe.push({ x: safePxX, y: safePxY });
  if (trailSafe.length > 45) trailSafe.shift();
  if (trailSafe.length > 2) {
    ctx.strokeStyle = clamped ? 'rgba(248, 113, 113, 0.35)' : 'rgba(34, 211, 238, 0.38)';
    ctx.lineWidth = 2;
    ctx.beginPath();
    trailSafe.forEach((p, idx) => (idx === 0 ? ctx.moveTo(p.x, p.y) : ctx.lineTo(p.x, p.y)));
    ctx.stroke();
  }

  // Raw 10Hz Surgeon Tremor Ghost Dot
  ctx.fillStyle = 'rgba(251, 191, 36, 0.55)';
  ctx.beginPath();
  ctx.arc(rawPxX, rawPxY, 3.5, 0, Math.PI * 2);
  ctx.fill();

  if (clamped) {
    // Rejected rogue AI vector outside safe zone
    ctx.save();
    ctx.setLineDash([5, 4]);
    ctx.strokeStyle = 'rgba(239, 68, 68, 0.8)';
    ctx.lineWidth = 1.8;
    ctx.beginPath();
    ctx.moveTo(safePxX, safePxY);
    ctx.lineTo(aiPxX, aiPxY);
    ctx.stroke();
    ctx.restore();

    // Red scalpel probe wedge + glowing clamp reticle locked on perimeter
    const ang = Math.atan2(safePxY - cy, safePxX - cx);
    ctx.save();
    ctx.translate(safePxX, safePxY);
    ctx.rotate(ang);
    ctx.fillStyle = 'rgba(239, 68, 68, 0.78)';
    ctx.beginPath();
    ctx.moveTo(0, 0);
    ctx.lineTo(44, -6);
    ctx.lineTo(48, 0);
    ctx.lineTo(44, 6);
    ctx.closePath();
    ctx.fill();
    ctx.restore();

    ctx.save();
    ctx.shadowBlur = 22;
    ctx.shadowColor = '#ef4444';
    ctx.strokeStyle = '#f87171';
    ctx.lineWidth = 2.4;
    ctx.beginPath();
    ctx.arc(safePxX, safePxY, 12, 0, Math.PI * 2);
    ctx.stroke();
    ctx.restore();

    ctx.strokeStyle = '#ffffff';
    ctx.lineWidth = 1.3;
    ctx.beginPath();
    ctx.moveTo(safePxX - 8, safePxY);
    ctx.lineTo(safePxX + 8, safePxY);
    ctx.moveTo(safePxX, safePxY - 8);
    ctx.lineTo(safePxX, safePxY + 8);
    ctx.stroke();
  } else {
    // Nominal glowing emerald surgical end-effector
    ctx.save();
    ctx.shadowBlur = 16;
    ctx.shadowColor = '#4ade80';
    ctx.fillStyle = '#4ade80';
    ctx.beginPath();
    ctx.arc(safePxX, safePxY, 7, 0, Math.PI * 2);
    ctx.fill();
    ctx.strokeStyle = '#ffffff';
    ctx.lineWidth = 1.8;
    ctx.stroke();
    ctx.restore();
  }
}

export function drawOscilloscope(oscHistory) {
  const c = document.getElementById('osc-canvas');
  if (!c) return;
  const rect = c.getBoundingClientRect();
  if (c.width !== Math.floor(rect.width) || c.height !== Math.floor(rect.height)) {
    c.width = Math.floor(rect.width);
    c.height = Math.floor(rect.height);
  }

  const ctx = c.getContext('2d');
  const w = c.width;
  const h = c.height;
  ctx.clearRect(0, 0, w, h);

  const padL = 38;
  const padR = 10;
  const padT = 10;
  const padB = 18;
  const pw = w - padL - padR;
  const ph = h - padT - padB;
  const midY = padT + ph / 2;

  ctx.strokeStyle = 'rgba(30, 58, 95, 0.45)';
  ctx.fillStyle = '#7c93b0';
  ctx.font = '10px "JetBrains Mono", monospace';
  ctx.lineWidth = 1;

  [-2, -1, 0, 1, 2].forEach((v) => {
    const y = midY - (v / 2.0) * (ph / 2);
    ctx.beginPath();
    ctx.moveTo(padL, y);
    ctx.lineTo(padL + pw, y);
    ctx.stroke();
    const lbl = v > 0 ? `+${v.toFixed(1)}` : v.toFixed(1);
    ctx.fillText(lbl, 4, y + 3);
  });

  ctx.fillText('-5.0 ms', padL, h - 4);
  ctx.fillText('0', padL + pw * 0.5 - 4, h - 4);
  ctx.fillText('+5.0 ms', padL + pw - 44, h - 4);

  ctx.save();
  ctx.shadowBlur = 10;
  ctx.shadowColor = '#22d3ee';
  ctx.strokeStyle = '#22d3ee';
  ctx.lineWidth = 2;
  ctx.beginPath();
  for (let i = 0; i < oscHistory.length; i++) {
    const x = padL + (i / (oscHistory.length - 1)) * pw;
    const clampedVal = Math.max(-2.0, Math.min(2.0, oscHistory[i]));
    const y = midY - (clampedVal / 2.0) * (ph / 2);
    if (i === 0) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
  }
  ctx.stroke();
  ctx.restore();
}