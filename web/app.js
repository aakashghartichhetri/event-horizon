import {
  STORIES, money, centsInteger, integer, shortCommand, eventMeaning, snapshotAt,
  eventPoints, classification, fileSafeName, buildTradeCsv, resilienceOutcome,
} from './insights.mjs';

const $ = (id) => document.getElementById(id);
const SVG = 'http://www.w3.org/2000/svg';
const VIEW_NAMES = { overview: 'Overview', network: 'Network map', ledger: 'Trade ledger', comparison: 'Compare outcomes', resilience: 'Resilience lab', workbench: 'Workbench' };
const SAVE_KEY = 'event-horizon.saved-experiments.v1';
const ICON_PATHS = { play: '#ic-play', pause: '#ic-pause' };
const model = {
  catalog: [], active: '', current: null, step: 0, running: false,
  timer: null, view: 'overview', comparison: null, loadGeneration: 0,
  selectedParticipant: null, networkFilter: 'all', selectedHistoryAccount: 'BEACON',
  resilience: null, resilienceChoice: { recovery: 2, funding: 0 }, resilienceContext: null,
};

function element(tag, className = '', content) {
  const item = document.createElement(tag);
  if (className) item.className = className;
  if (content !== undefined && content !== null) item.textContent = String(content);
  return item;
}
function svg(tag, attributes = {}) {
  const item = document.createElementNS(SVG, tag);
  for (const [key, value] of Object.entries(attributes)) item.setAttribute(key, String(value));
  return item;
}
function text(id, content) { $(id).textContent = String(content); }
function replace(id, ...children) { $(id).replaceChildren(...children); }
function icon(name) {
  const wrap = svg('svg', { class: 'icon', viewBox: '0 0 24 24', 'aria-hidden': 'true' });
  wrap.append(svg('use', { href: ICON_PATHS[name] || '#ic-arrow' }));
  return wrap;
}
function displayError(message) {
  text('error-message', message);
  $('error-banner').hidden = false;
}
function clearError() { $('error-banner').hidden = true; }
let toastTimeout;
function toast(message) {
  text('toast', message);
  $('toast').hidden = false;
  window.clearTimeout(toastTimeout);
  toastTimeout = window.setTimeout(() => { $('toast').hidden = true; }, 3400);
}

async function api(path, options) {
  const response = await fetch(path, { cache: 'no-store', ...options });
  let body;
  try { body = await response.json(); }
  catch { throw new Error(`The server returned an unreadable response (${response.status}).`); }
  if (!response.ok) throw new Error(body.error || `Request failed (HTTP ${response.status}).`);
  return body;
}
function stopPlayback() {
  model.running = false;
  window.clearTimeout(model.timer);
  model.timer = null;
  $('play-pause').replaceChildren(icon('play'));
  $('play-pause').setAttribute('aria-label', 'Play replay');
}
function startPlayback() {
  if (!model.current) return;
  if (model.running) { stopPlayback(); return; }
  if (model.step >= model.current.timeline.length) moveToStep(0);
  model.running = true;
  $('play-pause').replaceChildren(icon('pause'));
  $('play-pause').setAttribute('aria-label', 'Pause replay');
  const advance = () => {
    if (!model.running || !model.current) return;
    if (model.step >= model.current.timeline.length) { stopPlayback(); return; }
    moveToStep(model.step + 1);
    model.timer = window.setTimeout(advance, Number($('playback-rate').value));
  };
  advance();
}
function setView(view) {
  if (!(view in VIEW_NAMES)) return;
  model.view = view;
  for (const id of Object.keys(VIEW_NAMES)) $('view-' + id).hidden = id !== view;
  document.querySelectorAll('[data-view]').forEach(button => {
    const selected = button.dataset.view === view;
    button.classList.toggle('active', selected);
    if (selected) button.setAttribute('aria-current', 'page');
    else button.removeAttribute('aria-current');
  });
  text('view-breadcrumb', VIEW_NAMES[view]);
  closeNavigation();
  window.scrollTo({ top: 0, behavior: 'instant' });
  if (view === 'comparison') void ensureComparison();
  if (view === 'resilience') void ensureResilience();
  if (view === 'network' && model.current) renderNetwork();
  if (view === 'ledger' && model.current) renderLedger();
}
function openNavigation() {
  $('sidebar').classList.add('is-open');
  $('nav-backdrop').hidden = false;
  $('menu-toggle').setAttribute('aria-expanded', 'true');
}
function closeNavigation() {
  $('sidebar').classList.remove('is-open');
  $('nav-backdrop').hidden = true;
  $('menu-toggle').setAttribute('aria-expanded', 'false');
}
function scenarioTitle(id) { return model.catalog.find(item => item.id === id)?.title || (id === 'custom' ? 'Custom experiment' : id); }
function currentStory() { return STORIES[model.active] || STORIES.custom; }
function renderScenarioNavigation() {
  const nav = $('scenario-nav');
  nav.replaceChildren();
  for (const [index, scenario] of model.catalog.entries()) {
    const button = element('button', 'scenario-option');
    button.type = 'button';
    button.classList.toggle('active', model.active === scenario.id);
    const story = STORIES[scenario.id];
    button.append(element('span', 'scenario-index', String(index + 1).padStart(2, '0')),
      element('span', 'scenario-label', scenario.title),
      element('span', `scenario-severity ${story?.severity || ''}`));
    button.addEventListener('click', () => { void loadScenario(scenario.id); });
    nav.append(button);
  }
  text('scenario-count', String(model.catalog.length).padStart(2, '0'));
}
async function loadScenario(id) {
  stopPlayback();
  const generation = ++model.loadGeneration;
  clearError();
  try {
    const simulation = await api(`/api/scenarios/${encodeURIComponent(id)}`);
    if (generation !== model.loadGeneration) return;
    displaySimulation(simulation, id, { updateEditor: true });
    setView('overview');
    const url = new URL(location.href);
    url.searchParams.delete('resilience');
    url.searchParams.set('scenario', id);
    window.history.replaceState({}, '', url.pathname + url.search);
  } catch (error) {
    if (generation !== model.loadGeneration) return;
    displayError(`Could not load this experiment: ${error.message}`);
  }
}
function displaySimulation(simulation, id, { updateEditor = false } = {}) {
  model.current = simulation;
  model.active = id;
  model.step = simulation.timeline.length;
  model.selectedParticipant = simulation.accounts[0]?.name || null;
  model.selectedHistoryAccount = simulation.accounts.some(a => a.name === model.selectedHistoryAccount) ? model.selectedHistoryAccount : (simulation.accounts[0]?.name || '');
  renderScenarioNavigation();
  renderStory();
  renderEventHistory();
  renderResultCards();
  renderNetwork();
  renderLedger();
  renderRelated();
  if (updateEditor) { $('csv-input').value = simulation.source; updateEditorLines(); }
  $('step-slider').max = String(simulation.timeline.length);
  moveToStep(model.step);
  if (model.comparison) renderComparison();
}
function renderStory() {
  const story = currentStory();
  text('story-eyebrow', story.eyebrow);
  text('story-title', story.title);
  text('story-description', story.description);
  text('story-takeaway', story.takeaway);
  if (model.active === 'resilience' && model.resilienceContext && model.resilience) {
    const { recovery, funding } = model.resilienceContext;
    text('story-description', `${model.resilience.recovery_labels[recovery]}. ${model.resilience.funding_labels[funding]}. The event trace and balances come from this exact run.`);
  }
}
function moveToStep(step) {
  if (!model.current) return;
  const current = snapshotAt(model.current, step);
  model.step = current.step;
  $('step-slider').value = String(current.step);
  text('playback-step', `Event ${current.step} of ${current.total}`);
  text('playback-label', current.current ? shortCommand(current.current.command).replaceAll('_', ' ') : 'Before the first event');
  text('metric-matched', integer(current.tradesExecuted));
  text('metric-settled', integer(current.tradesSettled));
  text('metric-rejected', integer(current.rejected));
  text('metric-escrow', money(current.clearingCash));
  text('exchange-label', current.exchangeOnline ? 'Exchange online' : 'Exchange offline');
  text('payments-label', current.paymentsOnline ? 'Payments online' : 'Payments offline');
  $('exchange-chip').classList.toggle('down', !current.exchangeOnline);
  $('payments-chip').classList.toggle('down', !current.paymentsOnline);
  $('inspector-position').textContent = current.step === 0 ? 'START' : `${current.step} / ${current.total}`;
  renderInspector(current);
  renderPressureChart(current);
  document.querySelectorAll('.event-history-button').forEach(button => {
    const selected = Number(button.dataset.step) === current.step;
    button.classList.toggle('active', selected);
    button.setAttribute('aria-current', selected ? 'step' : 'false');
  });
  const active = document.querySelector('.event-history-button.active');
  if (active && model.running) active.scrollIntoView({ block: 'nearest', behavior: 'instant' });
}
function renderInspector(current) {
  const root = $('event-inspector');
  root.replaceChildren();
  if (!current.current) {
    root.append(element('span', 'inspector-kicker', 'Before the replay'),
      element('h4', '', 'Every chain starts somewhere.'),
      element('p', '', 'Press play or choose an event. We’ll follow what happens to trades, payments, and available funds.'));
    return;
  }
  const meaning = eventMeaning(current.current);
  root.append(element('span', 'inspector-kicker', `EVENT ${current.step} · ${current.current.status}`),
    element('h4', '', meaning.label[0].toUpperCase() + meaning.label.slice(1)),
    element('code', '', current.current.command),
    element('p', 'inspector-detail', meaning.detail));
  const facts = element('div', 'inspector-facts');
  for (const [label, value] of [['Trades pending', current.pendingTrades], ['Cash in clearing', money(current.clearingCash)]]) {
    const item = element('div');
    item.append(element('span', '', label), element('strong', '', value));
    facts.append(item);
  }
  root.append(facts);
  const history = element('div', 'position-history');
  const title = element('label', 'history-label', 'Available cash at this event');
  const select = element('select', 'history-select');
  const selectId = 'history-participant';
  select.id = selectId;
  title.htmlFor = selectId;
  for (const account of model.current.accounts) {
    if (account.name === 'FACILITY') continue;
    const option = element('option', '', account.name); option.value = account.name;
    select.append(option);
  }
  if (model.selectedHistoryAccount) select.value = model.selectedHistoryAccount;
  select.addEventListener('change', () => {
    model.selectedHistoryAccount = select.value;
    renderInspector(snapshotAt(model.current, model.step));
  });
  const match = current.current.accounts?.find(a => a.name === select.value);
  const value = element('strong', 'history-value', match ? money(match.available_cash_cents) : 'Not added yet');
  history.append(title, select, value);
  const samples = model.current.timeline.slice(0, current.step)
    .map(ev => ev.accounts?.find(a => a.name === select.value)?.available_cash_cents)
    .filter(n => n !== undefined && n !== null);
  if (samples.length > 1) {
    const chart = svg('svg', { class: 'history-sparkline', viewBox: '0 0 250 62', role: 'img',
      'aria-label': `${select.value} available cash across ${samples.length} events` });
    const exact = samples.map(centsInteger);
    const low = exact.reduce((a, b) => a < b ? a : b);
    const high = exact.reduce((a, b) => a > b ? a : b);
    const range = high > low ? high - low : 1n;
    const points = exact.map((v, i) => `${8 + (234 * i / (exact.length - 1))},${52 - 42 * Number((v - low) * 10000n / range) / 10000}`).join(' ');
    chart.append(svg('line', { x1: 7, y1: 53, x2: 243, y2: 53, class: 'history-axis' }));
    chart.append(svg('polyline', { points, class: 'history-line' }));
    history.append(chart);
  }
  root.append(history);
}
function renderEventHistory() {
  const root = $('event-history');
  root.replaceChildren();
  for (const [index, event] of model.current.timeline.entries()) {
    const button = element('button', 'event-history-button');
    button.type = 'button';
    button.dataset.step = String(index + 1);
    const flag = event.status === 'REJECTED' ? 'reject' : event.status === 'BLOCKED' ? 'block' : 'ok';
    button.append(element('span', 'history-index', String(index + 1).padStart(2, '0')),
      element('span', 'history-command', shortCommand(event.command).replaceAll('_', ' ')),
      element('span', `status-marker ${flag}`, flag === 'ok' ? '✓' : '!'));
    button.title = `${event.command} — ${event.message}`;
    button.addEventListener('click', () => { stopPlayback(); moveToStep(index + 1); });
    root.append(button);
  }
  text('event-history-count', `${model.current.timeline.length} events`);
}
function renderPressureChart(current) {
  const root = $('pressure-chart');
  root.replaceChildren();
  const all = eventPoints(model.current, model.current.timeline.length);
  const shown = eventPoints(model.current, current.step);
  const width = 690, height = 234, left = 57, right = 18, top = 21, bottom = 34;
  const plotWidth = width - left - right, plotHeight = height - top - bottom;
  const maxValue = all.reduce((maximum, point) => {
    const amount = centsInteger(point.clearingCash);
    return amount > maximum ? amount : maximum;
  }, 100n);
  const total = Math.max(1, model.current.timeline.length);
  const x = step => left + (step / total) * plotWidth;
  const y = amount => top + plotHeight * (1 - Number(centsInteger(amount) * 10000n / maxValue) / 10000);
  const path = points => points.map((point, index) => `${index ? 'L' : 'M'} ${x(point.index).toFixed(2)} ${y(point.clearingCash).toFixed(2)}`).join(' ');
  const picture = svg('svg', { viewBox: `0 0 ${width} ${height}`, role: 'img', 'aria-label': 'Cash waiting in clearing by replay event' });
  const defs = svg('defs');
  const gradient = svg('linearGradient', { id: 'pressure-gradient', x1: '0%', y1: '0%', x2: '0%', y2: '100%' });
  gradient.append(svg('stop', { offset: '0%', 'stop-color': '#61d7bd', 'stop-opacity': '.24' }));
  gradient.append(svg('stop', { offset: '100%', 'stop-color': '#61d7bd', 'stop-opacity': '0' }));
  defs.append(gradient); picture.append(defs);
  for (let row = 0; row <= 4; row++) {
    const amount = maxValue * BigInt(row) / 4n;
    const lineY = y(amount);
    picture.append(svg('line', { x1: left, y1: lineY, x2: width - right, y2: lineY,
      stroke: '#2b3d50', 'stroke-dasharray': '4 6' }));
    const label = svg('text', { x: left - 10, y: lineY + 4, 'text-anchor': 'end',
      'font-size': 11, fill: '#829bb0' }); label.textContent = money(amount);
    picture.append(label);
  }
  const future = svg('path', { d: path(all), fill: 'none', stroke: '#4a5d70', 'stroke-width': 2,
    'stroke-dasharray': '4 7' }); picture.append(future);
  const activePath = path(shown);
  if (shown.length > 1) {
    picture.append(svg('path', { d: `${activePath} L ${x(shown.at(-1).index)} ${y(0)} L ${left} ${y(0)} Z`, fill: 'url(#pressure-gradient)' }));
    picture.append(svg('path', { d: activePath, fill: 'none', stroke: '#6be1c7', 'stroke-width': 2.75, 'stroke-linecap': 'round', 'stroke-linejoin': 'round' }));
  }
  const last = shown.at(-1);
  picture.append(svg('line', { x1: x(last.index), x2: x(last.index), y1: top, y2: top + plotHeight,
    stroke: '#6be1c7', opacity: '.55', 'stroke-dasharray': '3 5' }));
  picture.append(svg('circle', { cx: x(last.index), cy: y(last.clearingCash), r: 5,
    fill: '#86f4d5', stroke: '#142e34', 'stroke-width': 3 }));
  for (const [value, align] of [[0, 'start'], [total / 2, 'middle'], [total, 'end']]) {
    const label = svg('text', { x: x(value), y: height - 8, 'text-anchor': align,
      'font-size': 11, fill: '#8399af' }); label.textContent = `Event ${Math.round(value)}`;
    picture.append(label);
  }
  root.append(picture);
  text('chart-peak', `Peak: ${money(model.current.metrics.peak_clearing_cash_cents)}`);
}
function renderResultCards() {
  const simulation = model.current;
  const outcome = classification(simulation);
  const label = $('outcome-status');
  label.className = `outcome-status ${outcome.level}`;
  label.textContent = outcome.label;
  text('outcome-note', outcome.note);
  text('outcome-pending', integer(simulation.metrics.pending_trades));
  text('outcome-peak', money(simulation.metrics.peak_clearing_cash_cents));
  text('outcome-invariants', simulation.invariants.passed ? 'Passed' : 'Failed');
}
function relatedIds(id) {
  const preferred = id === 'payments_outage'
    ? ['baseline', 'recovery', 'liquidity_bridge']
    : ['payments_outage', 'recovery', 'liquidity_bridge', 'baseline'];
  return preferred.filter(name => name !== id).slice(0, 3);
}
function renderRelated() {
  const root = $('related-grid'); root.replaceChildren();
  for (const id of relatedIds(model.active)) {
    const story = STORIES[id];
    const card = element('button', 'related-card'); card.type = 'button';
    card.append(element('span', 'related-type', story.category.toUpperCase()),
      element('strong', '', scenarioTitle(id)), element('p', '', story.takeaway), icon('arrow'));
    card.addEventListener('click', () => { void loadScenario(id); });
    root.append(card);
  }
}
function graph(container, filter, large = false) {
  const data = model.current;
  if (!data) return;
  const root = $(container); root.replaceChildren();
  const nodes = data.accounts.slice(0, 15);
  const width = 760, height = large ? 415 : 280;
  const centerX = width / 2, centerY = height / 2;
  const rx = large ? 245 : 225, ry = large ? 141 : 91;
  const positions = new Map();
  nodes.forEach((account, index) => {
    const angle = -Math.PI / 2 + 2 * Math.PI * index / Math.max(1, nodes.length);
    positions.set(account.name, { x: centerX + Math.cos(angle) * rx, y: centerY + Math.sin(angle) * ry });
  });
  const canvas = svg('svg', { viewBox: `0 0 ${width} ${height}`, role: large ? 'group' : 'img',
    'aria-label': 'Network of simulated participants connected by executed trades' });
  const defs = svg('defs');
  for (const [name, color] of [['settled', '#61d7bd'], ['pending', '#f6ae6c']]) {
    const marker = svg('marker', { id: `${container}-${name}`, viewBox: '0 0 10 10', refX: 9, refY: 5,
      markerWidth: 6, markerHeight: 6, orient: 'auto-start-reverse' });
    marker.append(svg('path', { d: 'M 0 0 L 10 5 L 0 10 Z', fill: color }));
    defs.append(marker);
  }
  canvas.append(defs);
  for (const trade of data.trades) {
    if (filter === 'pending' && trade.settled) continue;
    if (filter === 'settled' && !trade.settled) continue;
    const from = positions.get(trade.seller), to = positions.get(trade.buyer);
    if (!from || !to) continue;
    const dx = to.x - from.x, dy = to.y - from.y;
    const dist = Math.hypot(dx, dy) || 1, ux = dx / dist, uy = dy / dist;
    const r = 38;
    const color = trade.settled ? '#61d7bd' : '#f6ae6c';
    const x1 = from.x + ux * r, y1 = from.y + uy * r;
    const x2 = to.x - ux * (r + 5), y2 = to.y - uy * (r + 5);
    const cx = (x1 + x2) / 2 - uy * 12, cy = (y1 + y2) / 2 + ux * 12;
    const edge = svg('path', { d: `M ${x1} ${y1} Q ${cx} ${cy} ${x2} ${y2}`, fill: 'none',
      stroke: color, 'stroke-width': 2.7, opacity: '.8', class: 'network-edge',
      'marker-end': `url(#${container}-${trade.settled ? 'settled' : 'pending'})` });
    const title = svg('title'); title.textContent = `${trade.seller} sells ${trade.quantity} share(s) to ${trade.buyer}; ${trade.settled ? 'settled' : 'pending'}`;
    edge.append(title); canvas.append(edge);
  }
  const participants = new Set(data.trades.flatMap(trade => [trade.seller, trade.buyer]));
  const pending = new Set(data.trades.filter(trade => !trade.settled).flatMap(trade => [trade.seller, trade.buyer]));
  for (const account of nodes) {
    const pos = positions.get(account.name);
    const isSelected = large && account.name === model.selectedParticipant;
    const hasPending = pending.has(account.name);
    const nodeColor = hasPending ? '#f6ae6c' : participants.has(account.name) ? '#61d7bd' : '#60768d';
    const group = svg('g', { tabindex: large ? 0 : -1, role: 'button',
      'data-participant': account.name, 'aria-pressed': String(isSelected),
      'aria-label': `Inspect ${account.name}`, class: 'network-node' });
    if (isSelected) group.append(svg('circle', { cx: pos.x, cy: pos.y, r: 44, fill: '#61d7bd', opacity: '.12' }));
    group.append(svg('circle', { cx: pos.x, cy: pos.y, r: 35, fill: '#183040', stroke: nodeColor, 'stroke-width': isSelected ? 3.6 : 2.2 }));
    const label = svg('text', { x: pos.x, y: pos.y + 4, 'text-anchor': 'middle', fill: '#effcfb',
      'font-size': account.name.length >= 8 ? 11 : 12, 'font-weight': 700 });
    label.textContent = account.name;
    group.append(label);
    const select = () => {
      model.selectedParticipant = account.name;
      renderParticipantDetail();
      graph('network-full', model.networkFilter, true);
      // Re-rendering the SVG replaces the focused node. Restore keyboard focus.
      const selected = Array.from($('network-full').querySelectorAll('[data-participant]'))
        .find(item => item.getAttribute('data-participant') === account.name);
      selected?.focus({ preventScroll: true });
    };
    group.addEventListener('click', select);
    group.addEventListener('keydown', event => {
      if (event.key === 'Enter' || event.key === ' ') { event.preventDefault(); select(); }
    });
    canvas.append(group);
  }
  root.append(canvas);
  if (data.accounts.length > nodes.length) {
    root.append(element('p', 'reference-foot', `Showing ${nodes.length} of ${data.accounts.length} participants. All accounts are in the directory below.`));
  }
}
function renderNetwork() {
  if (!model.current) return;
  graph('network-overview', 'all');
  graph('network-full', model.networkFilter, true);
  renderParticipantDetail();
  text('account-count', `${integer(model.current.accounts.length)} participants`);
  const rows = $('participant-table'); rows.replaceChildren();
  for (const account of model.current.accounts) {
    const tr = element('tr');
    const td = element('td');
    const button = element('button', 'table-link', account.name);
    button.addEventListener('click', () => { model.selectedParticipant = account.name; renderParticipantDetail(); graph('network-full', model.networkFilter, true); });
    td.append(button); tr.append(td);
    const trades = model.current.trades.filter(trade => trade.buyer === account.name || trade.seller === account.name).length;
    for (const value of [money(account.available_cash_cents), money(account.reserved_cash_cents),
      `${account.available_shares} / ${account.shares}`, String(trades)]) tr.append(element('td', '', value));
    rows.append(tr);
  }
}
function renderParticipantDetail() {
  const account = model.current.accounts.find(item => item.name === model.selectedParticipant) || model.current.accounts[0];
  const root = $('participant-detail'); root.replaceChildren();
  if (!account) { text('participant-title', 'No accounts'); return; }
  text('participant-title', account.name);
  const tradeCount = model.current.trades.filter(trade => trade.seller === account.name || trade.buyer === account.name).length;
  root.append(element('div', 'participant-badge', account.name.slice(0, 2)),
    element('h4', 'participant-name', account.name),
    element('p', '', `${tradeCount} executed trade${tradeCount === 1 ? '' : 's'} involve this participant in the current scenario.`));
  const rows = element('div', 'participant-rows');
  for (const [label, value] of [['Cash available', money(account.available_cash_cents)],
    ['Cash reserved', money(account.reserved_cash_cents)], ['Total cash', money(account.cash_cents)],
    ['Shares available', integer(account.available_shares)], ['Shares reserved', integer(account.reserved_shares)]]) {
    const line = element('div'); line.append(element('span', '', label), element('strong', '', value)); rows.append(line);
  }
  root.append(rows);
}
function renderLedger() {
  const data = model.current;
  text('ledger-notional', money(data.metrics.traded_notional_cents));
  text('ledger-settled', integer(data.metrics.trades_settled));
  text('ledger-cash', money(data.metrics.clearing_cash_cents));
  text('ledger-loans', money(data.loans.reduce((sum, loan) => sum + centsInteger(loan.outstanding_cents), 0n)));
  const filter = $('trade-filter').value;
  const body = $('trades-table'); body.replaceChildren();
  const trades = data.trades.filter(trade => filter === 'all' || (filter === 'settled' ? trade.settled : !trade.settled));
  if (!trades.length) {
    const row = element('tr'); const cell = element('td', '', 'No trades match this filter.');
    cell.colSpan = 6; row.append(cell); body.append(row); return;
  }
  for (const trade of trades) {
    const row = element('tr');
    for (const value of [`#${trade.id}`, `${trade.seller} → ${trade.buyer}`, integer(trade.quantity),
      money(trade.price_cents), money(trade.cash_cents)]) row.append(element('td', '', value));
    const cell = element('td'); cell.append(element('span', trade.settled ? 'status-pill' : 'status-pill pending', trade.settled ? 'Delivered' : 'Pending'));
    row.append(cell); body.append(row);
  }
}
async function ensureComparison() {
  if (!model.catalog.length) return;
  if (model.comparison) { renderComparison(); return; }
  try {
    model.comparison = (await api('/api/compare')).comparison;
    renderComparison();
  } catch (error) { displayError(`Comparison is unavailable: ${error.message}`); }
}
function renderComparison() {
  if (!model.comparison) return;
  const aId = $('compare-a').value || 'payments_outage';
  const bId = $('compare-b').value || 'recovery';
  const a = model.comparison.find(item => item.id === aId);
  const b = model.comparison.find(item => item.id === bId);
  if (!a || !b) return;
  text('compare-name-a', a.title);
  text('compare-name-b', b.title);
  const metrics = [
    ['Trades matched', 'executed', false], ['Trades delivered', 'settled', false],
    ['Orders rejected', 'rejected', false], ['Trades waiting', 'pending', false],
    ['Cash in clearing', 'escrow_cents', true],
  ];
  const body = $('compare-table'); body.replaceChildren();
  const chart = $('comparison-chart'); chart.replaceChildren();
  for (const [title, key, currency] of metrics) {
    const format = value => currency ? money(value) : integer(value);
    const diff = currency ? centsInteger(b[key]) - centsInteger(a[key]) : b[key] - a[key];
    const row = element('tr');
    for (const value of [title, format(a[key]), format(b[key]), `${diff > 0 ? '+' : diff < 0 ? '−' : ''}${format(currency ? (diff < 0n ? -diff : diff) : Math.abs(diff))}`]) {
      row.append(element('td', '', value));
    }
    body.append(row);
    if (['settled', 'rejected', 'escrow_cents'].includes(key)) {
      const group = element('div', 'compare-group'); group.append(element('span', 'compare-group-label', title));
      const bars = element('div', 'compare-bars');
      const max = currency ? [1n, centsInteger(a[key]), centsInteger(b[key])].reduce((x,y) => x > y ? x : y) : Math.max(1, a[key], b[key]);
      for (const [value, second] of [[a[key], false], [b[key], true]]) {
        const line = element('div', second ? 'compare-bar second' : 'compare-bar');
        const track = element('div', 'compare-track');
        const bar = element('i'); bar.style.width = `${currency ? Number(centsInteger(value) * 10000n / max) / 100 : (value / max) * 100}%`;
        track.append(bar); line.append(track, element('span', '', format(value))); bars.append(line);
      }
      group.append(bars); chart.append(group);
    }
  }
  const sameBase = a.cash_initial_cents === b.cash_initial_cents;
  text('comparison-context', sameBase
    ? `${a.title} and ${b.title} begin with the same total modeled cash. The table shows how their final outcomes differ; the scenarios may still have different command sequences.`
    : `${a.title} and ${b.title} use different initial cash totals. Treat this as a comparison of example behaviors, not evidence that one intervention caused the difference.`);
  renderExperimentGallery();
}
function renderExperimentGallery() {
  const root = $('experiment-gallery'); root.replaceChildren();
  for (const scenario of model.catalog) {
    const story = STORIES[scenario.id];
    const card = element('button', 'experiment-card'); card.type = 'button';
    card.append(element('small', '', story.category.toUpperCase()),
      element('strong', '', scenario.title), element('p', '', story.takeaway));
    card.addEventListener('click', () => { void loadScenario(scenario.id); });
    root.append(card);
  }
}

async function ensureResilience() {
  if (model.resilience) { renderResilience(); return; }
  try {
    const matrix = await api('/api/resilience');
    model.resilience = matrix;
    renderResilience();
  } catch (error) {
    $('resilience-stats').replaceChildren(element('p', 'resilience-error', `Could not load the resilience matrix: ${error.message}`));
  }
}
function renderResilience() {
  const data = model.resilience;
  if (!data) return;
  const cases = data.cases;
  const summary = $('resilience-stats'); summary.replaceChildren();
  for (const [value, label] of [
    [cases.length, 'Controlled runs'],
    [cases.filter(row => row.settled === 3).length, 'All trades delivered'],
    [cases.filter(row => row.rejected > 0).length, 'With rejected orders'],
    [cases.filter(row => row.pending > 0).length, 'With settlement backlog'],
  ]) {
    const card = element('div', 'resilience-stat');
    card.append(element('strong', '', integer(value)), element('span', '', label));
    summary.append(card);
  }
  const grid = $('resilience-grid'); grid.replaceChildren();
  const corner = element('div', 'matrix-corner', 'When payments return ↓ / facility support →');
  grid.append(corner);
  for (const label of data.funding_labels) grid.append(element('div', 'matrix-column', label));
  for (let recovery = 0; recovery < 4; ++recovery) {
    const rowLabel = element('div', 'matrix-row-label');
    rowLabel.append(element('span', '', `WINDOW 0${recovery + 1}`), element('strong', '', data.recovery_labels[recovery]));
    grid.append(rowLabel);
    for (let funding = 0; funding < 3; ++funding) {
      const outcome = cases.find(row => row.recovery === recovery && row.funding === funding);
      const button = element('button', `matrix-case matrix-${resilienceOutcome(outcome)}`);
      button.type = 'button';
      button.dataset.recovery = String(recovery); button.dataset.funding = String(funding);
      const isSelected = model.resilienceChoice.recovery === recovery && model.resilienceChoice.funding === funding;
      button.classList.toggle('is-selected', isSelected);
      button.setAttribute('aria-pressed', String(isSelected));
      button.setAttribute('aria-label', `${data.recovery_labels[recovery]}, ${data.funding_labels[funding]}: ${outcome.settled} of 3 trades settled, ${outcome.rejected} orders rejected`);
      button.append(element('strong', '', `${outcome.settled} / 3`), element('span', '', `${outcome.rejected} rejected · ${outcome.pending} pending`));
      button.addEventListener('click', () => {
        model.resilienceChoice = { recovery, funding };
        renderResilience();
      });
      grid.append(button);
    }
  }
  const { recovery, funding } = model.resilienceChoice;
  const current = cases.find(row => row.recovery === recovery && row.funding === funding);
  const unfunded = cases.find(row => row.recovery === recovery && row.funding === 0);
  const detail = $('resilience-detail'); detail.replaceChildren();
  detail.append(element('span', 'tiny-overline', 'SELECTED INTERVENTION'),
    element('h3', '', data.recovery_labels[recovery]),
    element('p', 'resilience-detail-sub', data.funding_labels[funding]));
  const outcomes = element('div', 'resilience-detail-list');
  for (const [label, value] of [
    ['Trades executed', `${current.matched} / 3`],
    ['Trades settled', `${current.settled} / 3`],
    ['Rejected orders', integer(current.rejected)],
    ['Cash awaiting settlement', money(current.escrow_cents)],
    ['Facility debt still owed', money(current.outstanding_debt_cents)],
  ]) {
    const row = element('div'); row.append(element('span', '', label), element('strong', '', value)); outcomes.append(row);
  }
  detail.append(outcomes);
  const difference = current.matched - unfunded.matched;
  const note = element('p', 'resilience-delta', funding === 0
    ? 'Baseline for this recovery window: no temporary financing is provided.'
    : `${difference > 0 ? '+' : ''}${difference} matched trades compared with no facility support at the same recovery time. This is a model outcome, not a forecast.`);
  detail.append(note);
  const action = element('button', 'button button-primary resilience-action', 'Replay this case');
  action.type = 'button';
  action.addEventListener('click', () => { void loadResilienceCase(recovery, funding); });
  detail.append(action);
}
async function loadResilienceCase(recovery, funding) {
  stopPlayback(); clearError();
  const generation = ++model.loadGeneration;
  try {
    const simulation = await api(`/api/resilience/case?recovery=${recovery}&funding=${funding}`);
    if (generation !== model.loadGeneration) return;
    model.resilienceContext = { recovery, funding };
    displaySimulation(simulation, 'resilience', { updateEditor: true });
    setView('overview');
    const url = new URL(location.href);
    url.searchParams.delete('scenario');
    url.searchParams.set('resilience', `${recovery}-${funding}`);
    window.history.replaceState({}, '', url.pathname + url.search);
    toast('Opened the actual event trace for this intervention.');
  } catch (error) {
    if (generation !== model.loadGeneration) return;
    displayError(`Unable to replay the selected intervention: ${error.message}`);
  }
}
function updateEditorLines() {
  const lines = $('csv-input').value.split(/\r?\n/).filter(line => line.trim() && !line.trim().startsWith('#')).length;
  text('editor-lines', `${integer(lines)} commands`);
}
async function runCustom() {
  const source = $('csv-input').value;
  const bytes = new TextEncoder().encode(source).length;
  if (bytes < 1 || bytes > 65536) { displayError('Scenario must contain between 1 and 65,536 bytes.'); return; }
  stopPlayback(); clearError();
  const button = $('run-custom'); button.disabled = true;
  ++model.loadGeneration;
  try {
    const data = await api('/api/replay', { method: 'POST', headers: { 'Content-Type': 'text/plain; charset=utf-8' }, body: source });
    displaySimulation(data, 'custom');
    const root = $('editor-results'); root.hidden = false; root.replaceChildren();
    root.append(element('strong', '', 'Experiment completed.'),
      element('p', '', `${data.metrics.trades_executed} matched trades, ${data.metrics.trades_settled} settled, ${data.metrics.rejected_orders} rejected orders. All accounting checks passed.`));
    setView('overview');
    toast('Your experiment ran in a fresh, isolated C++ simulation.');
  } catch (error) {
    displayError(`Experiment rejected: ${error.message}`);
    $('error-banner').scrollIntoView({ block: 'start', behavior: 'smooth' });
  } finally { button.disabled = false; }
}
function download(filename, contents, mimeType) {
  const blob = new Blob([contents], { type: mimeType });
  const url = URL.createObjectURL(blob);
  const anchor = element('a'); anchor.href = url; anchor.download = filename;
  document.body.append(anchor); anchor.click(); anchor.remove();
  window.setTimeout(() => URL.revokeObjectURL(url), 1000);
}
function loadSaved() {
  try {
    const data = JSON.parse(window.localStorage.getItem(SAVE_KEY) || '[]');
    if (!Array.isArray(data)) return [];
    return data.filter(entry => entry && typeof entry.name === 'string' && typeof entry.source === 'string' && entry.source.length <= 65536).slice(0, 8);
  } catch { return []; }
}
function persistSaved(experiments) {
  try { window.localStorage.setItem(SAVE_KEY, JSON.stringify(experiments)); return true; }
  catch { return false; }
}
function renderSaved() {
  const root = $('saved-experiments'); root.replaceChildren();
  const experiments = loadSaved();
  if (!experiments.length) { root.append(element('p', 'saved-empty', 'No saved experiments yet.')); return; }
  for (const [index, item] of experiments.entries()) {
    const row = element('div', 'saved-entry');
    const load = element('button', 'saved-load', item.name); load.type = 'button';
    load.addEventListener('click', () => { $('csv-input').value = item.source; updateEditorLines(); toast('Loaded your saved script into the editor.'); });
    const remove = element('button', 'saved-remove', '×'); remove.type = 'button';
    remove.setAttribute('aria-label', `Delete ${item.name}`);
    remove.addEventListener('click', () => {
      const current = loadSaved(); current.splice(index, 1);
      if (!persistSaved(current)) displayError('Your browser blocked saved experiment storage.');
      renderSaved();
    });
    row.append(load, remove); root.append(row);
  }
}
function saveExperiment() {
  const name = $('save-name').value.trim();
  if (!name) { displayError('Enter a name before saving this experiment.'); $('save-name').focus(); return; }
  const sourceBytes = new TextEncoder().encode($('csv-input').value).length;
  if (sourceBytes < 1 || sourceBytes > 65536) { displayError('Scenario must have 1–65,536 bytes before saving.'); return; }
  const stored = loadSaved();
  stored.unshift({ name, source: $('csv-input').value });
  if (!persistSaved(stored.slice(0, 8))) { displayError('Saving is blocked by your browser. Export CSV instead.'); return; }
  $('save-name').value = '';
  renderSaved(); toast('Saved in this browser. Export the CSV for a portable copy.');
}
function setUpEvents() {
  document.querySelectorAll('[data-view]').forEach(button => button.addEventListener('click', () => setView(button.dataset.view)));
  document.querySelectorAll('[data-switch-view]').forEach(button => button.addEventListener('click', () => setView(button.dataset.switchView)));
  $('menu-toggle').addEventListener('click', () => $('sidebar').classList.contains('is-open') ? closeNavigation() : openNavigation());
  $('nav-backdrop').addEventListener('click', closeNavigation);
  $('dismiss-error').addEventListener('click', clearError);
  $('guide-button').addEventListener('click', () => $('about-dialog').showModal());
  $('close-about').addEventListener('click', () => $('about-dialog').close());
  $('about-ok').addEventListener('click', () => $('about-dialog').close());
  $('step-slider').addEventListener('input', event => { stopPlayback(); moveToStep(Number(event.target.value)); });
  $('step-back').addEventListener('click', () => { stopPlayback(); moveToStep(model.step - 1); });
  $('step-forward').addEventListener('click', () => { stopPlayback(); moveToStep(model.step + 1); });
  $('play-pause').addEventListener('click', startPlayback);
  $('hero-play').addEventListener('click', () => { setView('overview'); startPlayback(); });
  $('playback-rate').addEventListener('change', () => { if (model.running) { stopPlayback(); startPlayback(); } });
  $('network-filter').querySelectorAll('button').forEach(button => button.addEventListener('click', () => {
    model.networkFilter = button.dataset.filter;
    $('network-filter').querySelectorAll('button').forEach(item => {
      const active = item === button;
      item.classList.toggle('active', active);
      item.setAttribute('aria-pressed', String(active));
    });
    graph('network-full', model.networkFilter, true);
  }));
  $('trade-filter').addEventListener('change', () => { if (model.current) renderLedger(); });
  $('compare-a').addEventListener('change', renderComparison);
  $('compare-b').addEventListener('change', renderComparison);
  $('csv-input').addEventListener('input', updateEditorLines);
  $('run-custom').addEventListener('click', () => { void runCustom(); });
  $('reset-editor').addEventListener('click', () => {
    if (!model.current) return;
    $('csv-input').value = model.current.source; updateEditorLines();
    $('editor-results').hidden = true; toast('Editor reset to the current simulation source.');
  });
  $('download-csv').addEventListener('click', () => download(`${fileSafeName(model.active)}.csv`, $('csv-input').value, 'text/csv'));
  $('download-button').addEventListener('click', () => {
    if (!model.current) return;
    download(`event-horizon-${fileSafeName(model.active)}.json`, JSON.stringify(model.current, null, 2), 'application/json');
  });
  $('export-ledger').addEventListener('click', () => {
    if (model.current) download(`trades-${fileSafeName(model.active)}.csv`, buildTradeCsv(model.current.trades), 'text/csv');
  });
  $('save-experiment').addEventListener('click', saveExperiment);
  $('resilience-export').addEventListener('click', () => {
    if (model.resilience) download('event-horizon-recovery-matrix.json', JSON.stringify(model.resilience, null, 2), 'application/json');
  });
  document.addEventListener('keydown', event => {
    if (event.key === 'Escape') closeNavigation();
    if (model.view !== 'overview' || !model.current) return;
    if (['INPUT', 'TEXTAREA', 'SELECT', 'BUTTON'].includes(document.activeElement?.tagName)) return;
    if (event.key === 'ArrowRight') { event.preventDefault(); stopPlayback(); moveToStep(model.step + 1); }
    if (event.key === 'ArrowLeft') { event.preventDefault(); stopPlayback(); moveToStep(model.step - 1); }
  });
}
async function initialize() {
  setUpEvents(); renderSaved();
  try {
    const catalog = await api('/api/scenarios');
    model.catalog = catalog.scenarios;
    const first = 'payments_outage';
    for (const select of [$('compare-a'), $('compare-b')]) {
      select.replaceChildren();
      for (const item of model.catalog) {
        const option = element('option', '', item.title); option.value = item.id; select.append(option);
      }
    }
    $('compare-a').value = first;
    $('compare-b').value = 'recovery';
    renderScenarioNavigation();
    const parameters = new URL(location.href).searchParams;
    const deepLink = /^(\d)-([012])$/.exec(parameters.get('resilience') || '');
    if (deepLink && Number(deepLink[1]) < 4) {
      await ensureResilience();
      if (model.resilience) await loadResilienceCase(Number(deepLink[1]), Number(deepLink[2]));
      else await loadScenario(first);
    } else {
      const requested = parameters.get('scenario');
      const valid = model.catalog.some(item => item.id === requested);
      await loadScenario(valid ? requested : first);
    }
  } catch (error) {
    displayError(`Couldn't connect to the C++ engine: ${error.message}. Start the server and refresh this page.`);
  }
}
void initialize();
