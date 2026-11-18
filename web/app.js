// The dashboard has no build step. Every number comes from the C++ API.
const byId = (id) => document.getElementById(id);
const formatMoney = (cents) => new Intl.NumberFormat('en-US', {
  style: 'currency', currency: 'USD', maximumFractionDigits: 2,
}).format(cents / 100);
const safeNumber = (n) => Number(n).toLocaleString('en-US');
const SVG = 'http://www.w3.org/2000/svg';

const state = { scenarios: [], active: 'baseline', latest: null, comparison: null, compareOpen: false };

function node(tag, className = '', content = '') {
  const element = document.createElement(tag);
  if (className) element.className = className;
  if (content !== '') element.textContent = String(content);
  return element;
}
function svgNode(tag, attributes = {}) {
  const element = document.createElementNS(SVG, tag);
  for (const [key, value] of Object.entries(attributes)) element.setAttribute(key, String(value));
  return element;
}
function setText(id, value) { byId(id).textContent = String(value); }
function clear(element) { element.replaceChildren(); }

function status(message, error = false) {
  setText('api-status', message);
  byId('api-pill').classList.toggle('offline', error);
}
function showError(message) {
  const alert = byId('alert');
  alert.textContent = message;
  alert.hidden = false;
}
function hideError() { byId('alert').hidden = true; }
function busy(on) {
  for (const id of ['run-button', 'run-custom', 'compare-button']) byId(id).disabled = on;
}
async function request(url, options = {}) {
  const response = await fetch(url, { cache: 'no-store', ...options });
  const data = await response.json();
  if (!response.ok) throw new Error(data.error || `HTTP ${response.status}`);
  return data;
}

function renderNavigation() {
  const nav = byId('scenario-list');
  clear(nav);
  const icons = ['◈', '⚠', '↻', '✧', '◉', '⊘', '☒', '≋', '⌁'];
  state.scenarios.forEach((scenario, index) => {
    const button = node('button', 'nav-option');
    button.type = 'button';
    button.classList.toggle('active', scenario.id === state.active);
    button.setAttribute('aria-current', scenario.id === state.active ? 'page' : 'false');
    button.append(node('span', 'nav-icon', icons[index] || '◈'));
    button.append(node('span', 'nav-text', scenario.title));
    button.addEventListener('click', () => { void loadScenario(scenario.id); });
    nav.append(button);
  });
}

async function loadScenario(id) {
  hideError(); busy(true);
  try {
    const data = await request(`/api/scenarios/${encodeURIComponent(id)}`);
    state.active = id;
    state.latest = data;
    const current = state.scenarios.find((item) => item.id === id);
    renderNavigation();
    renderSimulation(data, current?.title || id, current?.description || '');
    byId('csv-input').value = data.source;
    status('Engine online');
  } catch (error) { showError(`Could not load scenario: ${error.message}`); status('Connection issue', true); }
  finally { busy(false); }
}

function renderSimulation(data, title, description) {
  setText('crumb-title', title);
  setText('scenario-title', title);
  setText('scenario-description', description);
  setText('m-executed', safeNumber(data.metrics.trades_executed));
  setText('m-settled', safeNumber(data.metrics.trades_settled));
  setText('m-rejected', safeNumber(data.metrics.rejected_orders));
  setText('m-escrow', formatMoney(data.metrics.clearing_cash_cents));
  setText('event-count', `${data.timeline.length} events`);
  setText('pending-count', `${data.metrics.pending_trades} pending`);
  setText('status-exchange', `Exchange ${data.exchange_online ? 'online' : 'offline'}`);
  setText('status-payments', `Payments ${data.payments_online ? 'online' : 'offline'}`);
  byId('status-exchange').classList.toggle('danger', !data.exchange_online);
  byId('status-payments').classList.toggle('danger', !data.payments_online);
  renderEscrowChart(data.timeline);
  renderNetwork(data.accounts, data.trades);
  renderAccounts(data.accounts);
  renderTrades(data.trades);
  renderTimeline(data.timeline);
}

function renderEscrowChart(events) {
  const parent = byId('escrow-chart');
  clear(parent);
  const svg = svgNode('svg', { viewBox: '0 0 650 210', role: 'img', 'aria-label': 'Clearing cash balance by event' });
  const x0 = 58, y0 = 16, w = 568, h = 154;
  const max = Math.max(1, ...events.map((e) => e.clearing_cash_cents));
  [0, 0.25, 0.5, 0.75, 1].forEach((fraction) => {
    const y = y0 + h * (1 - fraction);
    svg.append(svgNode('line', { x1: x0, x2: x0 + w, y1: y, y2: y, stroke: '#2a3a4d', 'stroke-dasharray': '3 4' }));
    const label = svgNode('text', { x: 49, y: y + 3, 'text-anchor': 'end', fill: '#7b91a6', 'font-size': 10 });
    label.textContent = max * fraction >= 100000 ? `$${Math.round(max * fraction / 100000)}k` : `$${Math.round(max * fraction / 100)}`;
    svg.append(label);
  });
  const points = [{ x: x0, y: y0 + h }];
  events.forEach((event, i) => {
    points.push({ x: x0 + ((i + 1) / Math.max(1, events.length)) * w, y: y0 + h * (1 - event.clearing_cash_cents / max) });
  });
  const d = points.map((point, i) => `${i === 0 ? 'M' : 'L'} ${point.x} ${point.y}`).join(' ');
  const area = `${d} L ${x0 + w} ${y0 + h} L ${x0} ${y0 + h} Z`;
  const defs = svgNode('defs');
  const gradient = svgNode('linearGradient', { id: 'escrowFill', x1: '0%', y1: '0%', x2: '0%', y2: '100%' });
  gradient.append(svgNode('stop', { offset: '0%', 'stop-color': '#54e4c5', 'stop-opacity': '.32' }));
  gradient.append(svgNode('stop', { offset: '100%', 'stop-color': '#54e4c5', 'stop-opacity': '0' }));
  defs.append(gradient); svg.append(defs);
  svg.append(svgNode('path', { d: area, fill: 'url(#escrowFill)' }));
  svg.append(svgNode('path', { d, fill: 'none', stroke: '#54e4c5', 'stroke-width': 2.5, 'stroke-linejoin': 'round' }));
  const last = points.at(-1);
  svg.append(svgNode('circle', { cx: last.x, cy: last.y, r: 4, fill: '#54e4c5', stroke: '#142735', 'stroke-width': 2 }));
  for (const [x, label] of [[x0, 'START'], [x0 + w / 2, 'EVENTS'], [x0 + w, 'END']]) {
    const text = svgNode('text', { x, y: 197, fill: '#718aa2', 'font-size': 10, 'text-anchor': x === x0 ? 'start' : x === x0 + w ? 'end' : 'middle' });
    text.textContent = label; svg.append(text);
  }
  parent.append(svg);
}

function renderNetwork(accounts, trades) {
  const parent = byId('network');
  clear(parent);
  const visible = accounts.slice(0, 8);
  const width = 600, height = 205, centerX = 300, centerY = 103;
  const svg = svgNode('svg', { viewBox: `0 0 ${width} ${height}`, role: 'img',
    'aria-label': 'Directed arrows connect sellers to buyers for executed trades' });
  const defs = svgNode('defs');
  for (const [id, color] of [['paid', '#54e4c5'], ['unpaid', '#f6bd71']]) {
    const marker = svgNode('marker', { id, viewBox: '0 0 10 10', refX: 9, refY: 5,
      markerWidth: 6, markerHeight: 6, orient: 'auto-start-reverse' });
    marker.append(svgNode('path', { d: 'M 0 0 L 10 5 L 0 10 Z', fill: color }));
    defs.append(marker);
  }
  svg.append(defs);
  const positions = new Map();
  visible.forEach((account, index) => {
    const angle = -Math.PI / 2 + (Math.PI * 2 * index) / visible.length;
    positions.set(account.name, {
      x: centerX + (visible.length <= 2 ? 165 : 205) * Math.cos(angle),
      y: centerY + (visible.length <= 2 ? 0 : 64) * Math.sin(angle),
    });
  });
  trades.forEach((trade) => {
    const from = positions.get(trade.seller), to = positions.get(trade.buyer);
    if (!from || !to) return;
    const dx = to.x - from.x, dy = to.y - from.y, distance = Math.hypot(dx, dy) || 1;
    const ux = dx / distance, uy = dy / distance, radius = 30;
    const x1 = from.x + ux * radius, y1 = from.y + uy * radius;
    const x2 = to.x - ux * radius, y2 = to.y - uy * radius;
    const curvature = trades.length > 1 ? 13 : 0;
    const midX = (x1 + x2) / 2 - uy * curvature;
    const midY = (y1 + y2) / 2 + ux * curvature;
    const color = trade.settled ? '#54e4c5' : '#f6bd71';
    svg.append(svgNode('path', { d: `M ${x1} ${y1} Q ${midX} ${midY} ${x2} ${y2}`,
      fill: 'none', stroke: color, 'stroke-width': 2.2, opacity: .85,
      'marker-end': trade.settled ? 'url(#paid)' : 'url(#unpaid)' }));
  });
  const tradedParticipants = new Set(trades.flatMap((t) => [t.seller, t.buyer]));
  const pendingParticipants = new Set(trades.filter((t) => !t.settled).flatMap((t) => [t.seller, t.buyer]));
  visible.forEach((account) => {
    const position = positions.get(account.name);
    const color = pendingParticipants.has(account.name) ? '#f6bd71'
      : tradedParticipants.has(account.name) ? '#54e4c5' : '#50657e';
    svg.append(svgNode('circle', { cx: position.x, cy: position.y, r: 30,
      fill: '#172a3c', stroke: color, 'stroke-width': 1.8 }));
    const label = svgNode('text', { x: position.x, y: position.y + 3,
      'text-anchor': 'middle', fill: '#e6f1f8', 'font-weight': 700,
      'font-size': account.name.length > 7 ? 9 : 11 });
    label.textContent = account.name;
    svg.append(label);
  });
  parent.append(svg);
  if (accounts.length > visible.length) parent.append(node('p', 'muted small',
    `Showing ${visible.length} of ${accounts.length} accounts; all balances appear below.`));
}

function renderAccounts(accounts) {
  const body = byId('accounts-body'); clear(body);
  accounts.forEach((account) => {
    const row = node('tr');
    row.append(node('td', 'account-name', account.name));
    row.append(node('td', '', formatMoney(account.available_cash_cents)));
    row.append(node('td', '', formatMoney(account.reserved_cash_cents)));
    row.append(node('td', '', `${account.available_shares} / ${account.shares}`));
    body.append(row);
  });
}

function renderTrades(trades) {
  const body = byId('trades-body'); clear(body);
  if (!trades.length) {
    const row = node('tr'); const cell = node('td', 'muted', 'No trades executed');
    cell.colSpan = 3; row.append(cell); body.append(row); return;
  }
  trades.forEach((trade) => {
    const row = node('tr');
    row.append(node('td', '', `${trade.seller} → ${trade.buyer}`));
    row.append(node('td', '', `${formatMoney(trade.cash_cents)} · ${trade.quantity} sh`));
    const cell = node('td'); cell.append(node('span', `status-chip ${trade.settled ? '' : 'pending'}`, trade.settled ? 'Settled' : 'Pending'));
    row.append(cell); body.append(row);
  });
}

function renderTimeline(events) {
  const body = byId('timeline-body'); clear(body);
  events.forEach((event) => {
    const row = node('tr');
    row.append(node('td', 'muted', String(event.index).padStart(2, '0')));
    const command = node('td', '', event.command); command.title = `Line ${event.line}: ${event.message}`;
    row.append(command);
    row.append(node('td', event.status.toLowerCase(), event.status));
    row.append(node('td', '', event.pending_trades));
    row.append(node('td', '', formatMoney(event.clearing_cash_cents)));
    body.append(row);
  });
}

function renderComparison(data) {
  const chart = byId('comparison-chart'); clear(chart);
  const max = Math.max(1, ...data.comparison.map((s) => s.executed));
  data.comparison.forEach((s) => {
    const row = node('div', 'compare-row');
    row.append(node('span', 'compare-label', s.title));
    const background = node('div', 'compare-bar-bg');
    const bar = node('div', `compare-bar ${s.pending ? 'outage' : ''}`);
    bar.style.width = `${100 * s.settled / max}%`;
    background.append(bar); row.append(background);
    row.append(node('span', 'compare-value', `${s.settled} / ${s.executed}`));
    chart.append(row);
  });
  const container = byId('comparison-table'); clear(container);
  const table = node('table');
  const thead = node('thead'); const head = node('tr');
  for (const title of ['Scenario', 'Executed', 'Settled', 'Rejected', 'Pending', 'Escrow']) head.append(node('th', '', title));
  thead.append(head); table.append(thead);
  const body = node('tbody');
  data.comparison.forEach((s) => {
    const row = node('tr');
    [s.title, s.executed, s.settled, s.rejected, s.pending, formatMoney(s.escrow_cents)]
      .forEach((value, index) => row.append(node('td', index === 0 ? 'account-name' : '', value)));
    body.append(row);
  });
  table.append(body); container.append(table);
}

async function showComparison() {
  hideError(); busy(true);
  try {
    if (!state.comparison) state.comparison = await request('/api/compare');
    renderComparison(state.comparison);
    state.compareOpen = true;
    byId('comparison-section').hidden = false;
    byId('comparison-section').scrollIntoView({ behavior: 'smooth', block: 'start' });
  } catch (error) { showError(`Could not compare scenarios: ${error.message}`); }
  finally { busy(false); }
}

async function runCustom() {
  hideError(); busy(true);
  try {
    const source = byId('csv-input').value;
    if (!source || new TextEncoder().encode(source).length > 65536) throw new Error('Scenario must be 1–65536 UTF-8 bytes');
    const data = await request('/api/replay', {
      method: 'POST', headers: { 'Content-Type': 'text/plain; charset=utf-8' }, body: source,
    });
    state.latest = data;
    renderSimulation(data, 'Custom experiment', 'Your edited script ran in a fresh, isolated C++ exchange.');
    setText('editor-hint', 'Custom experiment replayed successfully ✓');
    window.scrollTo({ top: 300, behavior: 'smooth' });
  } catch (error) { showError(`Replay rejected: ${error.message}`); byId('alert').scrollIntoView({ behavior: 'smooth' }); }
  finally { busy(false); }
}

function exportResults() {
  if (!state.latest) return;
  const blob = new Blob([JSON.stringify(state.latest, null, 2)], { type: 'application/json' });
  const url = URL.createObjectURL(blob);
  const anchor = node('a');
  anchor.href = url;
  anchor.download = `event-horizon-${state.latest.scenario}.json`;
  document.body.append(anchor); anchor.click(); anchor.remove();
  URL.revokeObjectURL(url);
}

byId('run-button').addEventListener('click', () => { void loadScenario(state.active); });
byId('compare-button').addEventListener('click', () => { void showComparison(); });
byId('close-compare').addEventListener('click', () => { byId('comparison-section').hidden = true; state.compareOpen = false; });
byId('run-custom').addEventListener('click', () => { void runCustom(); });
byId('reset-editor').addEventListener('click', () => { void loadScenario(state.active); setText('editor-hint', 'Maximum 64 KB, 500 events'); });
byId('export-json').addEventListener('click', exportResults);

async function initialize() {
  try {
    const catalog = await request('/api/scenarios');
    state.scenarios = catalog.scenarios;
    await loadScenario(catalog.default);
  } catch (error) {
    status('Connection issue', true);
    showError(`The C++ API is unavailable: ${error.message}. Make sure event-horizon-server is running.`);
  }
}
void initialize();
