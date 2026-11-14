// End-to-end browser tests: real page, real server core (desktop simulator),
// real controller reports. Run with `npm test` after building the simulator.
const {chromium} = require('playwright');
const {spawn} = require('node:child_process');
const net = require('node:net');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const assert = require('node:assert/strict');

const root = path.join(__dirname, '..');
const sim = process.env.OMNIWEBPAD_SIM || path.join(root, 'build/host/omniwebpad-sim');
const shots = process.env.SCREENSHOTS === '1';
const sleep = ms => new Promise(r => setTimeout(r, ms));

function startSimulator(port, links) {
  const settings = path.join(fs.mkdtempSync(path.join(os.tmpdir(), 'owp-')), 'settings.bin');
  const child = spawn(sim, ['--port', String(port), '--links', links, '--settings', settings], {stdio: ['pipe', 'pipe', 'inherit']});
  const state = {child, reports: [], events: [], mode: '', url: 'http://127.0.0.1:' + port + '/'};
  let pending = '';
  child.stdout.on('data', chunk => {
    pending += chunk;
    const lines = pending.split('\n');
    pending = lines.pop();
    for (const line of lines) {
      const [kind, mode, hex] = line.split(' ');
      if (kind === 'REPORT') state.reports.push(Buffer.from(hex, 'hex'));
      else { state.events.push(kind); if (kind === 'READY') state.mode = mode; }
    }
  });
  state.last = () => state.reports.at(-1);
  state.mark = () => state.reports.length;
  state.since = mark => state.reports.slice(mark);
  return state;
}
async function until(check, what, ms = 3000) {
  const end = Date.now() + ms;
  for (;;) {
    let value;
    try { value = await check(); } catch (_) {}
    if (value) return value;
    if (Date.now() > end) throw new Error('timed out waiting for ' + what);
    await sleep(15);
  }
}
const overlaps = (a, b) => a.x < b.x + b.w - .5 && a.x + a.w > b.x + .5 && a.y < b.y + b.h - .5 && a.y + a.h > b.y + .5;

async function checkLayout(page, width, height, label) {
  const boxes = await page.locator('#pad > .key:not(.hide), #bar > *, #dpad, #face, .stick').evaluateAll(els => els.map(el => {
    const r = el.getBoundingClientRect();
    return {name: el.id || el.className, x: r.x, y: r.y, w: r.width, h: r.height};
  }));
  assert(boxes.length >= 16, 'controls present');
  for (let i = 0; i < boxes.length; i++) {
    const a = boxes[i];
    assert(a.w > 20 && a.h > 20, label + ': too small ' + a.name);
    assert(a.x >= -.5 && a.y >= -.5 && a.x + a.w <= width + .5 && a.y + a.h <= height + .5, label + ': outside viewport ' + a.name);
    for (let j = i + 1; j < boxes.length; j++) assert(!overlaps(a, boxes[j]), label + ': overlap ' + a.name + ' / ' + boxes[j].name);
  }
}

async function controllerTests(browser, simulator, width, height) {
  const context = await browser.newContext({viewport: {width, height}, hasTouch: true, deviceScaleFactor: shots ? 2 : 1});
  await context.addInitScript(() => { window.__vibrations = []; navigator.vibrate = p => { window.__vibrations.push(p); return true; }; });
  const page = await context.newPage();
  const errors = [];
  page.on('pageerror', e => errors.push(String(e)));
  await page.goto(simulator.url);
  await page.locator('#status[data-link="host"]').waitFor();
  await until(() => page.evaluate(() => document.querySelector('#pad').dataset.family === 'xbox'), 'xbox skin');

  const portrait = height > width;
  for (const choice of ['dpad', 'stick']) {
    await page.evaluate(v => { localStorage.setItem('owp.layout', v); }, choice);
    await page.reload();
    await page.locator('#status[data-link="host"]').waitFor();
    await checkLayout(page, width, height, width + 'x' + height + ' ' + choice);
  }

  // Logical offsets -> screen offsets (the stage is rotated in portrait).
  const centre = async (selector, dx = 0, dy = 0) => {
    const b = await page.locator(selector).boundingBox();
    const k = await page.evaluate(() => document.querySelector('#pad').getBoundingClientRect().width / document.querySelector('#pad').offsetWidth);
    const s = portrait ? await page.evaluate(() => document.querySelector('#pad').getBoundingClientRect().height / document.querySelector('#pad').offsetWidth) : k;
    return {x: b.x + b.width / 2 + (portrait ? -dy : dx) * s, y: b.y + b.height / 2 + (portrait ? dx : dy) * s};
  };
  const cdp = await context.newCDPSession(page);
  const touch = (type, points) => cdp.send('Input.dispatchTouchEvent', {type, touchPoints: points.map((p, id) => ({...p, id: p.id ?? id, radiusX: 3, radiusY: 3, force: 1}))});
  const s16 = (report, offset) => report.readInt16LE(offset);
  const idle = r => r[2] === 0 && r[3] === 0 && s16(r, 6) === 0 && s16(r, 8) === 0 && s16(r, 10) === 0 && s16(r, 12) === 0;
  await until(() => simulator.last() && idle(simulator.last()), 'idle report');

  // Two sticks at once; touching down must not move the stick.
  let mark = simulator.mark();
  await touch('touchStart', [await centre('#ls', 8, 0), await centre('#rs')]);
  await sleep(60);
  assert(simulator.since(mark).every(idle), 'touch start must not jump');
  await touch('touchMove', [await centre('#ls', 60, 0), await centre('#rs', 0, -60)]);
  await until(() => s16(simulator.last(), 6) === 32767 && s16(simulator.last(), 12) === 32767, 'left right + right up');
  assert.equal(s16(simulator.last(), 8), 0); assert.equal(s16(simulator.last(), 10), 0);
  await touch('touchMove', [await centre('#ls', 8, 20), await centre('#rs', -60, 0)]);
  await until(() => s16(simulator.last(), 8) < -10000 && s16(simulator.last(), 10) === -32768, 'left down + right left');
  await touch('touchEnd', []);
  await until(() => idle(simulator.last()), 'sticks centred on release');

  // A tap shorter than one frame still reaches the host exactly once.
  mark = simulator.mark();
  await touch('touchStart', [await centre('#face .south')]);
  await touch('touchEnd', []);
  await until(() => simulator.since(mark).some(r => r[3] === 0x10) && idle(simulator.last()), 'short tap');
  assert.equal(simulator.since(mark).filter(r => r[3] === 0x10).length, 1);

  // D-pad: one zone, diagonals, sliding.
  await touch('touchStart', [await centre('#dpad', 0, -50)]);
  await until(() => simulator.last()[2] === 0x01, 'dpad up');
  await touch('touchMove', [await centre('#dpad', 45, -45)]);
  await until(() => simulator.last()[2] === 0x09, 'dpad up+right');
  await touch('touchMove', [await centre('#dpad', 50, 0)]);
  await until(() => simulator.last()[2] === 0x08, 'dpad right');
  await touch('touchMove', [await centre('#dpad', -40, 40)]);
  await until(() => simulator.last()[2] === 0x06, 'dpad down+left');
  await touch('touchEnd', []);
  await until(() => idle(simulator.last()), 'dpad released');

  // Face buttons: thumb between two buttons presses both; sliding re-targets.
  await touch('touchStart', [await centre('#face', 24, 24)]);
  await until(() => simulator.last()[3] === 0x30, 'A+B together');
  await touch('touchMove', [await centre('#face', 0, -48)]);
  await until(() => simulator.last()[3] === 0x80, 'slide to Y');
  await touch('touchEnd', []);
  await until(() => idle(simulator.last()), 'face released');

  // Chords across controls: shoulder + system + stick click with three fingers.
  await touch('touchStart', [await centre('#l1'), await centre('#start'), await centre('#r3')]);
  await until(() => simulator.last()[2] === (0x10 | 0x80) && simulator.last()[3] === 0x01, 'three finger chord');
  await touch('touchEnd', []);
  await touch('touchStart', [await centre('#r2')]);
  await until(() => simulator.last()[5] === 255, 'digital trigger is a full analog pull');
  await touch('touchEnd', []);
  await until(() => idle(simulator.last()) && simulator.last()[5] === 0, 'released');

  // Cancelled touches and losing focus release everything.
  await touch('touchStart', [await centre('#ls'), await centre('#face .east')]);
  await touch('touchMove', [await centre('#ls', 50, 0), await centre('#face .east')]);
  await until(() => s16(simulator.last(), 6) > 20000 && simulator.last()[3] === 0x20, 'held');
  await touch('touchCancel', []);
  await until(() => idle(simulator.last()), 'cancel releases');
  await touch('touchStart', [await centre('#rs')]);
  await touch('touchMove', [await centre('#rs', 50, 0)]);
  await until(() => s16(simulator.last(), 10) > 20000, 'right stick held');
  await page.evaluate(() => window.dispatchEvent(new Event('blur')));
  await until(() => idle(simulator.last()), 'blur releases');
  await touch('touchEnd', []);
  await page.evaluate(() => window.dispatchEvent(new Event('focus')));

  // Host feedback: rumble reaches the phone, link loss is shown.
  simulator.child.stdin.write('rumble 200 0\n');
  await page.locator('#status.rumble').waitFor();
  assert(await page.evaluate(() => window.__vibrations.some(v => v === 220)), 'rumble vibrates the phone');
  simulator.child.stdin.write('rumble 0 0\nled 5\ncolor 0 0 255\n');
  await until(() => page.evaluate(() => !document.querySelector('#status').classList.contains('rumble') && document.querySelectorAll('#leds .on').length === 2), 'rumble off, LEDs on');
  simulator.child.stdin.write('link 0\nled 0\ncolor 0 0 0\n');
  await page.locator('#status[data-link="board"]').waitFor();
  simulator.child.stdin.write('link 1\n');
  await page.locator('#status[data-link="host"]').waitFor();
  await until(() => page.evaluate(() => /\d+ ms/.test(document.querySelector('#statusText').textContent)), 'round trip time shown');

  assert.deepEqual(errors, []);
  if (shots && width === 844) await page.screenshot({path: path.join(root, 'docs/controller-xbox.png')});
  if (shots && width === 390) await page.screenshot({path: path.join(root, 'docs/controller-portrait.png')});
  console.log(`${width}x${height}: layout x2, dual sticks, tap latch, 8-way D-pad, thumb roll, chords, release, feedback PASS`);
  await context.close();
}

async function settingsTests(browser, simulator) {
  const context = await browser.newContext({viewport: {width: 844, height: 390}, hasTouch: true, deviceScaleFactor: shots ? 2 : 1, locale: 'zh-CN'});
  const page = await context.newPage();
  const errors = [];
  page.on('pageerror', e => errors.push(String(e)));
  await page.goto(simulator.url);
  await page.locator('#status[data-link="host"]').waitFor();
  assert.equal(await page.locator('#back').textContent(), '返回手柄', 'language follows the browser');
  await page.locator('#menu').tap();
  assert.equal(await page.locator('.mode').count(), 8);
  assert(await page.locator('.mode', {hasText: 'Bluetooth'}).isDisabled(), 'BLE is not offered on a USB-only board');
  assert(await page.locator('#applyMode').isDisabled());
  if (shots) {
    await page.screenshot({path: path.join(root, 'docs/settings.zh-CN.png')});
    await page.locator('#lang').tap();
    await page.screenshot({path: path.join(root, 'docs/settings.png')});
    await page.locator('#lang').tap();
  }

  // Rejected input is reported and changes nothing.
  await page.locator('#pass').fill('short');
  await page.locator('#saveWifi').tap();
  await until(async () => (await page.locator('#notice').textContent()).includes('8-63'), 'validation error');
  await page.locator('#pass').fill('');

  // Switch personality: the board restarts and the page follows.
  for (const [name, id, family, south, report] of [
    ['Switch wired pad', 'switch', 'switch', 'B', r => r[0] === 0x02 && r[2] === 8],
    ['DualShock 4', 'ds4', 'ps', '✕', r => r[4] === 0x28],
    ['DualSense', 'dualsense', 'ps', '✕', r => r[7] === 0x28],
    ['DualShock 3', 'ds3', 'ps', '✕', r => r[2] === 0x40 && r[23] === 255],
    ['Xbox (original)', 'xid', 'xbox', 'A', r => r[4] === 255],
    ['Generic gamepad', 'hid', 'generic', 'A', r => r[0] === 0x01],
  ]) {
    await page.locator('.mode', {hasText: name}).tap();
    await page.locator('#applyMode').tap();
    await until(() => simulator.mode === id, 'restart as ' + id);
    await until(() => page.evaluate(([f, n]) => document.querySelector('#pad').dataset.family === f &&
      document.querySelector('.mode.current b').textContent.endsWith(n) && !document.querySelector('#notice').textContent &&
      document.querySelector('#status').dataset.link === 'host', [family, name]), 'page follows ' + id, 6000);
    await page.locator('#back').tap();
    assert.equal(await page.locator('#face .south').textContent(), south);
    const b = await page.locator('#face .south').boundingBox();
    await page.touchscreen.tap(b.x + b.width / 2, b.y + b.height / 2);
    await until(() => simulator.reports.slice(-40).some(report), id + ' report for the south button');
    await page.evaluate(() => window.dispatchEvent(new Event('blur')));
    await page.evaluate(() => window.dispatchEvent(new Event('focus')));
    if (shots && (id === 'switch' || id === 'ds4')) { await sleep(150); await page.screenshot({path: path.join(root, 'docs/controller-' + (id === 'ds4' ? 'ps' : id) + '.png')}); }
    await page.locator('#menu').tap();
  }

  // Wi-Fi settings survive the restart; factory reset needs two taps.
  await page.locator('#lang').tap();
  assert.equal(await page.locator('#back').textContent(), 'Back to pad');
  await page.locator('#ssid').fill('Living Room Pad');
  await page.locator('#pass').fill('correct horse');
  const before = simulator.events.filter(e => e === 'REBOOT').length;
  await page.locator('#saveWifi').tap();
  await until(() => simulator.events.filter(e => e === 'REBOOT').length === before + 1, 'restart after Wi-Fi change');
  await until(async () => (await (await fetch(simulator.url + 'api/info')).json()).ssid === 'Living Room Pad', 'ssid stored');
  await page.locator('#factory').tap();
  await sleep(100);
  assert.equal(simulator.events.filter(e => e === 'REBOOT').length, before + 1, 'first tap only arms');
  await page.locator('#factory').tap();
  await until(() => simulator.mode === 'xinput' && simulator.events.filter(e => e === 'REBOOT').length === before + 2, 'factory reset');
  await until(async () => (await (await fetch(simulator.url + 'api/info')).json()).ssid.startsWith('OmniWebPad-'), 'defaults restored');
  assert.deepEqual(errors, []);
  console.log('settings: language, validation, 7 personalities with skins and reports, Wi-Fi, factory reset PASS');
  await context.close();
}

async function fallbackTests(browser, simulator) {
  // No WebSocket at all (old proxy, exotic browser): HTTP polling takes over.
  const context = await browser.newContext({viewport: {width: 844, height: 390}, hasTouch: true});
  await context.addInitScript(() => { window.WebSocket = function () { throw new Error('blocked'); }; });
  const page = await context.newPage();
  await page.goto(simulator.url);
  await page.locator('#status[data-link="host"]').waitFor();
  const b = await page.locator('#face .east').boundingBox();
  const cdp = await context.newCDPSession(page);
  await cdp.send('Input.dispatchTouchEvent', {type: 'touchStart', touchPoints: [{x: b.x + b.width / 2, y: b.y + b.height / 2, id: 0}]});
  await until(() => simulator.last()[3] === 0x20, 'polling fallback delivers B');
  await cdp.send('Input.dispatchTouchEvent', {type: 'touchEnd', touchPoints: []});
  await until(() => simulator.last()[3] === 0, 'polling fallback releases');
  console.log('fallback: HTTP polling without WebSocket PASS');
  await context.close();
}

(async () => {
  if (!fs.existsSync(sim)) throw new Error('simulator not built: ' + sim + ' (see README, "Develop")');
  const simulator = startSimulator(18787, 'usb');
  const bleOnly = startSimulator(18788, 'ble');
  const browser = await chromium.launch(process.env.CHROME_PATH ? {executablePath: process.env.CHROME_PATH} : {});
  try {
    await until(() => simulator.mode === 'xinput' && bleOnly.mode === 'ble', 'simulators');
    for (const [w, h] of [[844, 390], [390, 844], [667, 375], [375, 667], [568, 320], [320, 568], [932, 430], [1024, 768]])
      await controllerTests(browser, simulator, w, h);
    // Idle connections parked by a browser must never lock the page out.
    const parked = [];
    for (let i = 0; i < 12; i++) parked.push(await new Promise((resolve, reject) => { const s = net.connect(18787, '127.0.0.1', () => resolve(s)); s.on('error', reject); }));
    assert.equal((await fetch(simulator.url + 'api/info')).status, 200);
    parked.forEach(s => s.destroy());
    console.log('connection slots: 12 parked sockets do not block a new request PASS');
    await settingsTests(browser, simulator);
    await fallbackTests(browser, simulator);
    const info = await (await fetch(bleOnly.url + 'api/info')).json();
    assert.equal(info.mode, 'ble');
    assert.deepEqual(info.modes.filter(m => m.available).map(m => m.id), ['ble']);
    console.log('ble-only board: only the Bluetooth personality is offered PASS');
  } finally {
    await browser.close();
    simulator.child.kill();
    bleOnly.child.kill();
  }
})().catch(e => { console.error(e); process.exitCode = 1; });
