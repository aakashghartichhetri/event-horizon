import test from 'node:test';
import assert from 'node:assert/strict';
import { STORIES, snapshotAt, eventMeaning, classification, eventPoints, money, fileSafeName, buildTradeCsv, resilienceOutcome } from './insights.mjs';

const scenario = {
  timeline: [
    { command: 'PLACE,a,A,BUY,1,100', status: 'OK', trades_executed: 1, trades_settled: 0, clearing_cash_cents: 100, pending_trades: 1 },
    { command: 'PAYMENTS_DOWN', status: 'OK', trades_executed: 1, trades_settled: 0, clearing_cash_cents: 100, pending_trades: 1 },
    { command: 'SETTLE', status: 'BLOCKED', trades_executed: 1, trades_settled: 0, clearing_cash_cents: 100, pending_trades: 1 },
    { command: 'PLACE,b,A,BUY,1,100', status: 'REJECTED', trades_executed: 1, trades_settled: 0, clearing_cash_cents: 100, pending_trades: 1 },
    { command: 'PAYMENTS_UP', status: 'OK', trades_executed: 1, trades_settled: 0, clearing_cash_cents: 100, pending_trades: 1 },
    { command: 'SETTLE', status: 'OK', trades_executed: 1, trades_settled: 1, clearing_cash_cents: 0, pending_trades: 0 },
  ],
  metrics: { rejected_orders: 1, pending_trades: 0 },
};

test('playback state reflects only processed events, not final result', () => {
  assert.deepEqual([snapshotAt(scenario, 0).tradesExecuted, snapshotAt(scenario, 0).clearingCash], [0, 0]);
  assert.equal(snapshotAt(scenario, 2).paymentsOnline, false);
  assert.equal(snapshotAt(scenario, 4).rejected, 1);
  assert.equal(snapshotAt(scenario, 5).paymentsOnline, true);
  assert.equal(snapshotAt(scenario, 6).clearingCash, 0);
  assert.equal(snapshotAt(scenario, 6).tradesSettled, 1);
  const precise = { timeline: [{ ...scenario.timeline[3], rejected_orders: 0, payments_online: true }] };
  assert.equal(snapshotAt(precise, 1).rejected, 0);
  assert.equal(snapshotAt(precise, 1).paymentsOnline, true);
  assert.equal(snapshotAt(scenario, 999).step, 6);
  assert.equal(snapshotAt(scenario, -12).step, 0);
});

test('chart excludes future events', () => {
  assert.deepEqual(eventPoints(scenario, 1), [{ index: 0, clearingCash: 0 }, { index: 1, clearingCash: 100 }]);
});

test('risk label is descriptive rather than a statistical prediction', () => {
  assert.equal(classification({ metrics: { pending_trades: 1, rejected_orders: 2 } }).level, 'high');
  assert.equal(classification({ metrics: { pending_trades: 0, rejected_orders: 0 } }).level, 'low');
  assert.equal(eventMeaning(scenario.timeline[2]).label, 'Settlement blocked');
  assert.equal(STORIES.payments_outage.category, 'Failure');
});

test('exports and formatting keep financial values in integer cents', () => {
  assert.equal(money(10025), '$100.25');
  assert.equal(fileSafeName('../Bad Filename!'), 'bad-filename');
  assert.match(buildTradeCsv([{id:1,seller:'A',buyer:'B',quantity:2,price_cents:100,cash_cents:200,settled:false}]), /1,A,B,2,100,200,pending/);
});

test('resilience classification reflects delivered trades, not risk probabilities', () => {
  assert.equal(resilienceOutcome({settled:3}), 'complete');
  assert.equal(resilienceOutcome({settled:1}), 'partial');
  assert.equal(resilienceOutcome({settled:0}), 'blocked');
});
