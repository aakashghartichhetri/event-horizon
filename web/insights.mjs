// Presentation-only analysis. The C++ engine is the source of truth for every balance and trade.
export const STORIES = Object.freeze({
  baseline: {
    eyebrow: 'THE CONTROL CASE',
    title: 'When the system works, money keeps moving.',
    description: 'NOVA buys shares from BEACON. Once the sale settles, BEACON can buy from CIRRUS, who can then buy from DELTA. Every step depends on the last payment arriving.',
    takeaway: 'Settlement turns one participant’s sale into the next participant’s buying power.',
    lens: 'Normal operations', category: 'Baseline', severity: 'healthy',
  },
  payments_outage: {
    eyebrow: 'THE CASCADE',
    title: 'One payment failure. Three firms feel it.',
    description: 'NOVA’s trade with BEACON matches, but the payment system goes offline. BEACON cannot use the money it expects, and CIRRUS faces the same problem further down the chain.',
    takeaway: 'A matched trade is not spendable cash. Until settlement completes, a firm can be solvent on paper and still short of liquidity.',
    lens: 'Payment disruption', category: 'Failure', severity: 'critical',
  },
  recovery: {
    eyebrow: 'THE RECOVERY PLAN',
    title: 'Fix the payment rail before the next trade.',
    description: 'The first trade is interrupted, but settlement resumes before BEACON needs its sale proceeds. Those funds reach the next participant in time.',
    takeaway: 'Recovery timing can matter as much as the size of the initial disruption.',
    lens: 'Service restoration', category: 'Mitigation', severity: 'recovery',
  },
  liquidity_bridge: {
    eyebrow: 'THE LIQUIDITY BACKSTOP',
    title: 'Keep firms trading while payments recover.',
    description: 'A pre-funded facility lends to participants temporarily waiting on settlement. The facility transfers existing funds; its loans are repaid after payments resume.',
    takeaway: 'A liquidity backstop can interrupt the cascade, but it must be funded and reconciled.',
    lens: 'Funding support', category: 'Mitigation', severity: 'recovery',
  },
  liquidity_shock: {
    eyebrow: 'THE LIQUIDITY SQUEEZE',
    title: 'Cash on hand is not always cash you can spend.',
    description: 'A competing reservation ties up available cash before participants can place their next orders. The exchange rejects requests that cannot be funded.',
    takeaway: 'Reservations prevent double spending, but they can also reduce available liquidity.',
    lens: 'Cash constraints', category: 'Failure', severity: 'warning',
  },
  circuit_breaker: {
    eyebrow: 'THE SAFETY RAIL',
    title: 'Stop an extreme price before it spreads.',
    description: 'A fixed reference-price band blocks executions outside the configured limit. The result is a trading restriction, not a guarantee of a stable market.',
    takeaway: 'A price band contains some executions; it does not repair the underlying source of stress.',
    lens: 'Price controls', category: 'Controls', severity: 'warning',
  },
  exchange_outage: {
    eyebrow: 'THE TRADING HALT',
    title: 'What happens when the exchange stops taking orders?',
    description: 'Previously matched business can still settle, but new orders are rejected while matching is unavailable. Trading only resumes after the exchange recovers.',
    takeaway: 'Exchange availability and settlement availability are separate operating concerns.',
    lens: 'Exchange outage', category: 'Failure', severity: 'warning',
  },
  settlement_throttle: {
    eyebrow: 'THE CAPACITY LIMIT',
    title: 'The system is running. The queue is still growing.',
    description: 'The payment rail is online, but only a limited number of trades can settle per cycle. The remaining trade stays queued in clearing.',
    takeaway: 'A slow dependency can create pending obligations even without a complete outage.',
    lens: 'Settlement throughput', category: 'Capacity', severity: 'warning',
  },
  agents: {
    eyebrow: 'THE PARTICIPANT NETWORK',
    title: 'Automated strategies react to available funds.',
    description: 'Three deterministic, one-shot strategies wait until they can fund their orders. Each submission follows the same input sequence on every replay.',
    takeaway: 'Autonomous behavior is easier to reason about when its rules and event ordering are explicit.',
    lens: 'Trading agents', category: 'Strategies', severity: 'healthy',
  },
  resilience: {
    eyebrow: 'RECOVERY WINDOW ANALYSIS',
    title: 'The timing of intervention changes what firms can do next.',
    description: 'This case changes when payment settlement restarts and whether a pre-funded facility supports the two exposed firms. The ledger records the actual outcome.',
    takeaway: 'A rescue works only while the next decision can still be made.',
    lens: 'Intervention analysis', category: 'Controlled case', severity: 'recovery',
  },
  custom: {
    eyebrow: 'THE EXPERIMENT WORKBENCH',
    title: 'Change one event. See what breaks next.',
    description: 'This run comes from your own CSV commands. The same execution, accounting, and validation rules apply as they do to the built-in scenarios.',
    takeaway: 'An experiment is reproducible when the input, event order, and accounting rules stay fixed.',
    lens: 'Custom simulation', category: 'Workbench', severity: 'healthy',
  },
});

export const money = (cents) => new Intl.NumberFormat('en-US', {
  style: 'currency', currency: 'USD', minimumFractionDigits: 0,
  maximumFractionDigits: 2,
}).format(Number(cents || 0) / 100);
export const integer = (n) => new Intl.NumberFormat('en-US').format(Number(n || 0));
export const shortCommand = (command = '') => String(command).split(',')[0].trim();

export function eventMeaning(event) {
  const command = shortCommand(event.command);
  if (event.status === 'REJECTED') return { tone: 'critical', label: 'Order rejected', detail: event.message };
  if (event.status === 'BLOCKED') return { tone: 'warning', label: 'Settlement blocked', detail: event.message };
  const explanations = {
    ACCOUNT: 'A participant is added to the simulated network.',
    PLACE: 'An order enters the exchange. It may match or rest until a counterparty appears.',
    CANCEL: 'The unfilled remainder of an order is withdrawn, releasing its reserved assets.',
    SETTLE: 'Pending trades are delivered if payment infrastructure is available.',
    PAYMENTS_DOWN: 'The payment rail goes offline. Matched trades can no longer settle.',
    PAYMENTS_UP: 'The payment rail is restored. Waiting trades may now be settled.',
    EXCHANGE_DOWN: 'New orders can no longer be accepted by the exchange.',
    EXCHANGE_UP: 'Order acceptance resumes.',
    LEND: 'The funded facility transfers existing cash to a participant.',
    REPAY: 'A participant returns borrowed cash to the facility.',
    MAX_NOTIONAL: 'A maximum order value is applied to incoming orders.',
    PRICE_BAND: 'A reference-price limit is applied to matching.',
    AGENT: 'A deterministic trading strategy is registered.',
    TICK: 'Registered strategies check their funding and may submit orders.',
  };
  return { tone: ['PAYMENTS_DOWN', 'EXCHANGE_DOWN'].includes(command) ? 'warning' : 'neutral',
    label: (command || 'EVENT').replaceAll('_', ' ').toLowerCase(),
    detail: explanations[command] || event.message };
}

export function snapshotAt(simulation, requestedStep) {
  const timeline = Array.isArray(simulation?.timeline) ? simulation.timeline : [];
  const step = Math.min(timeline.length, Math.max(0, Math.trunc(Number(requestedStep) || 0)));
  const current = step === 0 ? null : timeline[step - 1];
  let exchangeOnline = true, paymentsOnline = true, rejected = 0;
  for (const event of timeline.slice(0, step)) {
    const command = shortCommand(event.command);
    if (command === 'EXCHANGE_DOWN') exchangeOnline = false;
    if (command === 'EXCHANGE_UP') exchangeOnline = true;
    if (command === 'PAYMENTS_DOWN') paymentsOnline = false;
    if (command === 'PAYMENTS_UP') paymentsOnline = true;
    if (event.status === 'REJECTED') rejected++;
  }
  return {
    step, total: timeline.length, current,
    tradesExecuted: current?.trades_executed ?? 0,
    tradesSettled: current?.trades_settled ?? 0,
    clearingCash: current?.clearing_cash_cents ?? 0,
    pendingTrades: current?.pending_trades ?? 0,
    rejected: current?.rejected_orders ?? rejected,
    exchangeOnline: current?.exchange_online ?? exchangeOnline,
    paymentsOnline: current?.payments_online ?? paymentsOnline,
  };
}

export function eventPoints(simulation, step) {
  const timeline = simulation?.timeline || [];
  const limit = Math.min(timeline.length, Math.max(0, step));
  return [{ index: 0, clearingCash: 0 }, ...timeline.slice(0, limit).map((event, index) => ({
    index: index + 1, clearingCash: event.clearing_cash_cents,
  }))];
}

export function classification(simulation) {
  const metrics = simulation.metrics;
  if (metrics.pending_trades > 0 && metrics.rejected_orders > 0) {
    return { level: 'high', label: 'Cascading disruption', note: 'Unsettled trades and rejected orders appear in the same run.' };
  }
  if (metrics.pending_trades > 0) {
    return { level: 'medium', label: 'Settlement backlog', note: 'At least one matched trade remains undelivered.' };
  }
  if (metrics.rejected_orders > 0) {
    return { level: 'medium', label: 'Trading constrained', note: 'At least one order was rejected by funding, availability, or risk rules.' };
  }
  return { level: 'low', label: 'No unresolved trades', note: 'No pending trades or rejected orders remain in this run.' };
}

export function pairComparison(base, alternative) {
  const byId = (a) => new Map(a.map(item => [item.id, item]));
  const catalog = byId(base?.comparison || []);
  const other = catalog.get(alternative);
  return other || null;
}

export function fileSafeName(text) {
  return String(text || 'scenario').toLowerCase().replace(/[^a-z0-9-_]+/g, '-').replace(/^-+|-+$/g, '').slice(0, 64) || 'scenario';
}

export function buildTradeCsv(trades) {
  const rows = ['trade_id,seller,buyer,shares,price_cents,notional_cents,status'];
  for (const trade of trades) {
    rows.push([trade.id, trade.seller, trade.buyer, trade.quantity, trade.price_cents,
      trade.cash_cents, trade.settled ? 'settled' : 'pending'].join(','));
  }
  return `${rows.join('\n')}\n`;
}

// Readable outcomes without turning these simulations into probability estimates.
export function resilienceOutcome(row) {
  if (row.settled === 3) return 'complete';
  if (row.settled > 0) return 'partial';
  return 'blocked';
}
