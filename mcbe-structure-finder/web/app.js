'use strict';

// 次元。番号は src/bedrock.h の be_kind_dim() と同じ。
const DIMS = [
  { name: 'オーバーワールド', tab: '地上' },
  { name: 'ネザー', tab: 'ネザー' },
  { name: 'エンド', tab: 'エンド' },
];

// 構造物の種類。順番は src/bedrock.h の enum BEKind と同じ。
// conf: ok=実機/Chunkbase と一致を確認, est=生成条件が推定, part=一部だけ, ref=参考
// dim: 次元 (省略は地上), chunk: チャンク単位の印 (拡大したときだけ描く),
// lazy: 数が多くて重いので、表示をオンにしたときに計算する
const KINDS = [
  { name: '村', main: true, group: 0, conf: 'ok', y: '地上' },
  { name: '砂漠のピラミッド', main: true, group: 1, conf: 'ok', y: '地上' },
  { name: 'ジャングルの寺院', main: true, group: 1, conf: 'ok', y: '地上' },
  { name: 'ウィッチの小屋', main: true, group: 1, conf: 'ok', y: '地上' },
  { name: 'イグルー', main: true, group: 1, conf: 'ok', y: '地上（地下室がある場合も）' },
  { name: '海底神殿', main: true, group: 2, conf: 'est', y: '海底', parts: 3 },
  { name: '森の洋館', main: true, group: 0, conf: 'est', y: '地上' },
  { name: 'ピリジャーの前哨基地', main: true, group: 0, conf: 'est', y: '地上' },
  { name: '難破船', group: 2, conf: 'est', y: '海底・砂浜' },
  { name: '海底遺跡', group: 2, conf: 'est', y: '海底' },
  { name: '埋もれた宝', group: 2, conf: 'est', y: '砂浜の砂の中', noisy: true, parts: 16 },
  { name: '荒廃したポータル', group: 4, conf: 'est', y: '地上〜地下' },
  { name: '古代都市', main: true, group: 3, conf: 'est', y: 'Y −51 付近（ディープダーク）' },
  { name: '旅路の遺跡', group: 4, conf: 'est', y: '地中に埋まっている' },
  { name: '試練の間', group: 3, conf: 'ok', y: 'Y −40〜−20 付近' },
  { name: '放棄されたキャンプ', group: 4, conf: 'est', y: '地上' },
  { name: '廃坑', group: 3, conf: 'est', y: '地下', noisy: true, parts: 24 },
  { name: '要塞', main: true, group: 3, conf: 'part', y: '地下', big: true },
  { name: '砂漠の井戸', group: 4, conf: 'ref', y: '地上', parts: 24 },
];
// 種類のまとまり。印の形と色はまとまりごと (種類の shape / color で上書きできる)
const GROUPS = [
  { name: '集落', shape: 'circle', color: '--g1' },
  { name: '神殿', shape: 'square', color: '--g2' },
  { name: '海', shape: 'triangle', color: '--g3' },
  { name: '地下', shape: 'diamond', color: '--g4' },
  { name: 'その他', shape: 'plus', color: '--g5' },
  { name: '建造物', shape: 'square', color: '--g2', dim: 1 },
  { name: 'その他', shape: 'plus', color: '--g5', dim: 1 },
  { name: '建造物', shape: 'circle', color: '--g1', dim: 2 },
  { name: 'その他', shape: 'diamond', color: '--g4', dim: 2 },
];
const BADGE = { est: '推定', part: '3つだけ', ref: '参考' };
const dimOf = (k) => KINDS[k].dim || 0;
const shapeOf = (k) => KINDS[k].shape || GROUPS[KINDS[k].group].shape;
const colorVar = (k) => KINDS[k].color || GROUPS[KINDS[k].group].color;
// 件数が多すぎて一覧が埋まるので最初は隠しておく種類
const NOISY = new Set(KINDS.map((k, i) => (k.noisy || k.lazy ? i : -1)).filter((i) => i >= 0));
// 重い種類は分けて計算して進捗を出す (±10000 のときの分割数)
const PARTS = Object.fromEntries(KINDS.map((k, i) => [i, k.parts || 0]).filter(([, n]) => n));

// 統合版のバージョン → cubiomes のバージョン定数 (biomes.h の MCVersion)
const VERSIONS = [
  { label: '26.50 以降（最新）', mc: 35 },
  { label: '26.30〜26.40', mc: 34 },
  { label: '1.21.60〜26.2x', mc: 33 },
  { label: '1.21.50', mc: 28 },
  { label: '1.21.0〜1.21.40', mc: 27 },
  { label: '1.20.x', mc: 25 },
  { label: '1.19.x', mc: 24 },
  { label: '1.18.x', mc: 22 },
];

// cubiomes のバイオーム ID → 日本語名
const BIOMES = {
  0: '海洋', 1: '平原', 2: '砂漠', 3: '吹きさらしの丘', 4: '森林', 5: 'タイガ', 6: '沼地', 7: '川',
  10: '凍った海洋', 11: '凍った川', 12: '雪原', 14: 'キノコ島', 16: '砂浜', 21: 'ジャングル',
  23: 'まばらなジャングル', 24: '深海', 25: '石だらけの海岸', 26: '雪の砂浜', 27: 'シラカバの森',
  29: '暗い森', 30: '雪のタイガ', 32: '巨大マツのタイガ', 34: '吹きさらしの森', 35: 'サバンナ',
  36: 'サバンナの台地', 37: '荒野', 38: '森のある荒野', 44: '暖かい海洋', 45: 'ぬるい海洋',
  46: '冷たい海洋', 47: '暖かい深海', 48: 'ぬるい深海', 49: '冷たい深海', 50: '凍った深海',
  129: 'ヒマワリ平原', 131: '吹きさらしの砂利の丘', 132: '花の森', 140: '樹氷',
  155: '巨大シラカバの森', 160: '巨大トウヒのタイガ', 163: '吹きさらしのサバンナ',
  165: '侵食された荒野', 168: '竹林', 174: '鍾乳洞', 175: '繁茂した洞窟', 177: '牧草地',
  178: '林', 179: '雪の斜面', 180: '尖った山頂', 181: '凍った山頂', 182: '石の山頂',
  183: 'ディープダーク', 184: 'マングローブの沼地', 185: 'サクラの林', 186: 'ペールガーデン',
  187: 'Sulfur Caves', 188: 'Dappled Forest',
};
const biomeName = (id) => BIOMES[id] || `バイオーム ${id}`;
const escapeHtml = (s) => s.replace(/[&<>"']/g, (c) => `&#${c.charCodeAt(0)};`);

const $ = (id) => document.getElementById(id);
const fmt = new Intl.NumberFormat('ja-JP');
const store = {
  get(k) { try { return localStorage.getItem(k); } catch { return null; } },
  set(k, v) { try { localStorage.setItem(k, v); } catch { /* 保存できなくても動く */ } },
};

// ---------------------------------------------------------------------------
// 計算エンジン。Web Worker の中で動かす (作れなければ同じコードを画面側で動かす)。
function engineMain(self) {
  const STRIDE = 4;  // 1 件 = [x, z, バイオーム, 付加情報]
  let ex = null;
  let current = '';
  // 今の計算 ({id, seedLo, seedHi, mc, rects, kdim}) と、残りの仕事 [種類, 帯, 帯の数]
  let job = null;
  let queue = [];
  let done = 0;
  let total = 0;
  let pumping = false;
  const pause = () => new Promise((r) => setTimeout(r, 0));
  const setup = (m) => {
    const key = `${m.seedLo},${m.seedHi},${m.mc}`;
    if (key !== current) {
      ex.wasm_setup(m.seedLo, m.seedHi, m.mc);
      current = key;
    }
  };
  const enqueue = (kinds, parts) => {
    setup(job);
    for (const k of kinds) {
      if (!ex.wasm_available(k)) {
        self.postMessage({ op: 'missing', id: job.id, kind: k });
        continue;
      }
      const n = parts[k] || 1;
      for (let p = 0; p < n; p++) queue.push([k, p, n]);
      total += n;
    }
  };
  // 新しい計算が来たら queue ごと差し替わるので、1 件ごとに job を読み直す
  const pump = async () => {
    if (pumping) return;
    pumping = true;
    while (queue.length) {
      const [k, p, n] = queue.shift();
      setup(job);
      const [x0, z0, x1, z1] = job.rects[job.kdim[k]];
      const c = ex.wasm_scan(k, x0, z0, x1, z1, p, n);
      if (c > 0) {
        const data = new Int32Array(ex.memory.buffer, ex.wasm_buffer(), c * STRIDE).slice();
        self.postMessage({ op: 'hits', id: job.id, kind: k, data }, [data.buffer]);
      }
      done++;
      self.postMessage({ op: 'progress', id: job.id, kind: k, last: p === n - 1, done, total });
      await pause();
    }
    pumping = false;
    self.postMessage({ op: 'done', id: job.id });
  };
  self.onmessage = async (e) => {
    const m = e.data;
    try {
      if (m.op === 'init') {
        const wasi = new Proxy({}, { get: () => () => 0 });
        const { instance } = await WebAssembly.instantiate(m.wasm, { wasi_snapshot_preview1: wasi });
        ex = instance.exports;
        if (ex._initialize) ex._initialize();
        self.postMessage({ op: 'ready' });
      } else if (m.op === 'run') {
        job = m;
        queue = [];
        done = 0;
        total = 0;
        enqueue(m.kinds, m.parts);
        pump();
      } else if (m.op === 'add') {
        // 同じ計算に種類を足す (重い種類を後からオンにしたとき)
        if (!job || m.id !== job.id) return;
        enqueue(m.kinds, m.parts);
        pump();
      } else if (m.op === 'map') {
        setup(m);
        const err = ex.wasm_biome_rgba(m.dim, m.x0, m.z0, m.w, m.h, m.scale);
        if (err) return;
        const rgba = new Uint8ClampedArray(ex.memory.buffer, ex.wasm_buffer(), m.w * m.h * 4).slice();
        self.postMessage({ op: 'map', id: m.id, key: m.key, dim: m.dim, x0: m.x0, z0: m.z0, w: m.w, h: m.h, scale: m.scale, rgba }, [rgba.buffer]);
      }
    } catch (err) {
      self.postMessage({ op: 'error', message: String(err && err.message || err) });
    }
  };
}

function startEngine(wasmBytes, onMessage) {
  const inPage = () => {
    // Worker が使えない環境: 同じ処理を画面側で動かす
    const inner = { onmessage: null, postMessage: (d) => setTimeout(() => onMessage({ data: d })) };
    engineMain(inner);
    const api = { postMessage: (d) => setTimeout(() => inner.onmessage({ data: d })) };
    api.postMessage({ op: 'init', wasm: wasmBytes.slice(0) });
    return api;
  };
  return new Promise((resolve) => {
    let worker;
    try {
      const src = `(${engineMain.toString()})(self);`;
      worker = new Worker(URL.createObjectURL(new Blob([src], { type: 'text/javascript' })));
    } catch {
      resolve(inPage());
      return;
    }
    let ready = false;
    const fallback = () => {
      if (ready) return;
      ready = true;
      worker.terminate();
      resolve(inPage());
    };
    worker.onerror = fallback;
    worker.onmessage = (e) => {
      if (!ready && e.data.op === 'ready') {
        ready = true;
        worker.onmessage = onMessage;
        worker.onerror = null;
        onMessage(e);
        resolve(worker);
      } else if (!ready && e.data.op === 'error') {
        fallback();
      }
    };
    setTimeout(fallback, 20000);
    worker.postMessage({ op: 'init', wasm: wasmBytes.slice(0) });
  });
}

async function loadWasm() {
  if (window.MCBE_WASM_B64) {
    const bin = atob(window.MCBE_WASM_B64);
    const bytes = new Uint8Array(bin.length);
    for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
    return bytes.buffer;
  }
  const res = await fetch('engine.wasm');
  if (!res.ok) throw new Error(`engine.wasm を読み込めません (${res.status})`);
  return res.arrayBuffer();
}

// ---------------------------------------------------------------------------
// シード値: 整数ならそのまま (64 ビット)、それ以外はゲームと同じく文字列のハッシュ
function parseSeed(text) {
  const t = text.trim();
  if (!t) return null;
  if (/^[+-]?\d+$/.test(t)) {
    const v = BigInt(t);
    if (v >= -(2n ** 63n) && v < 2n ** 63n) return BigInt.asUintN(64, v);
  }
  let h = 0;
  for (let i = 0; i < t.length; i++) h = (Math.imul(31, h) + t.charCodeAt(i)) | 0;
  return BigInt.asUintN(64, BigInt(h));
}

// ---------------------------------------------------------------------------
const state = {
  engine: null,
  runId: 0,
  running: false,
  seed: null,
  mc: VERSIONS[0].mc,
  cx: 0, cz: 0, range: 10000,
  dim: 0,                        // 表示している次元
  hits: KINDS.map(() => []),     // [{x, z, b, info, d}]
  missing: new Set(),
  asked: new Set(),              // 計算を頼んだ種類
  finished: new Set(),           // 計算が終わった種類
  enabled: new Set(KINDS.map((_, i) => i).filter((i) => !NOISY.has(i))),
  sort: 'dist',
  shown: 200,
  list: [],
  selected: null,
  example: false,
};

// 次元ごとの計算の中心。ネザーは地上の 1/8。
function centerOf(dim) {
  return dim === 1 ? [Math.floor(state.cx / 8), Math.floor(state.cz / 8)] : [state.cx, state.cz];
}

function cssVar(name) {
  return getComputedStyle(document.documentElement).getPropertyValue(name).trim();
}

function markHtml(kind) {
  return `<span class="mark" data-shape="${shapeOf(kind)}" style="--c: var(${colorVar(kind)})" aria-hidden="true"></span>`;
}

// --- 次元の切り替え ---
function renderDims() {
  $('dims').innerHTML = DIMS.map((d, i) => {
    let n = 0;
    for (const k of state.enabled) if (dimOf(k) === i) n += state.hits[k].length;
    return `<button type="button" role="tab" data-dim="${i}" aria-selected="${state.dim === i}">${d.tab}<span class="n">${state.seed === null ? '' : fmt.format(n)}</span></button>`;
  }).join('');
}

$('dims').addEventListener('click', (e) => {
  const b = e.target.closest('[data-dim]');
  if (!b) return;
  const d = Number(b.dataset.dim);
  if (d === state.dim) return;
  state.dim = d;
  state.selected = null;
  state.shown = 200;
  $('pick').hidden = true;
  store.set('mcbe.dim', String(d));
  renderDims();
  renderChips();
  fitMap();
  if (!map.bg[d].base) requestBase(d);
  refresh();
});

// --- 種類のチップ ---
function chipCount(i) {
  if (state.missing.has(i)) return '—';
  if (state.seed === null || !state.asked.has(i)) return '';
  return fmt.format(state.hits[i].length) + (state.finished.has(i) ? '' : '…');
}

function renderChips() {
  const box = $('groups');
  box.innerHTML = GROUPS.map((g, gi) => ({ g, gi })).filter(({ g }) => (g.dim || 0) === state.dim).map(({ g, gi }) => {
    const chips = KINDS.map((k, i) => ({ k, i })).filter(({ k }) => k.group === gi).map(({ k, i }) => {
      const missing = state.missing.has(i);
      const badge = BADGE[k.conf] ? `<span class="badge">${BADGE[k.conf]}</span>` : '';
      return `<button type="button" class="chip" data-kind="${i}" aria-pressed="${state.enabled.has(i)}" ${missing ? 'disabled title="このバージョンにはありません"' : ''}>${markHtml(i)}${k.name}<span class="n">${chipCount(i)}</span>${badge}</button>`;
    }).join('');
    return `<div class="group"><span class="label">${g.name}</span><div class="chips">${chips}</div></div>`;
  }).join('');
}

// 重い種類はオンにしたときに計算する
function askLazy() {
  if (state.seed === null) return;
  const kinds = [...state.enabled].filter((k) => !state.asked.has(k));
  if (!kinds.length) return;
  for (const k of kinds) state.asked.add(k);
  state.running = true;
  $('go').disabled = true;
  state.engine.postMessage({ op: 'add', id: state.runId, kinds, parts: partsFor(kinds) });
}

$('groups').addEventListener('click', (e) => {
  const b = e.target.closest('.chip');
  if (!b || b.disabled) return;
  const k = Number(b.dataset.kind);
  if (state.enabled.has(k)) state.enabled.delete(k); else state.enabled.add(k);
  saveEnabled();
  askLazy();
  renderChips();
  refresh();
});
// 「全部」「主要だけ」は表示中の次元の種類だけを切り替える
function setDimKinds(pred) {
  KINDS.forEach((k, i) => {
    if (dimOf(i) !== state.dim) return;
    if (pred(i)) state.enabled.add(i); else state.enabled.delete(i);
  });
  saveEnabled(); askLazy(); renderChips(); refresh();
}
$('all-on').addEventListener('click', () => setDimKinds(() => true));
$('main-on').addEventListener('click', () => setDimKinds((i) => !NOISY.has(i)));
function saveEnabled() {
  store.set('mcbe.enabled', [...state.enabled].join(','));
  store.set('mcbe.known', String(KINDS.length));
}

// --- 一覧 ---
function buildList() {
  const list = [];
  for (const k of state.enabled) {
    if (dimOf(k) !== state.dim) continue;
    for (const h of state.hits[k]) list.push({ k, ...h });
  }
  const by = {
    dist: (a, b) => a.d - b.d,
    kind: (a, b) => a.k - b.k || a.d - b.d,
    x: (a, b) => a.x - b.x || a.z - b.z,
    z: (a, b) => a.z - b.z || a.x - b.x,
  }[state.sort];
  list.sort(by);
  state.list = list;
}

// 一覧と詳細に添える一言: ネザーは地上の座標、チャンク単位の種類はチャンク座標
function extraText(h) {
  const k = KINDS[h.k];
  const parts = [];
  if (k.info) {
    const t = k.info(h.info);
    if (t) parts.push(t);
  }
  if (k.chunk) parts.push(`チャンク ${h.x >> 4}, ${h.z >> 4}`);
  if (dimOf(h.k) === 1) parts.push(`地上 ${h.x * 8}, ${h.z * 8}`);
  return parts.join(' · ');
}

function rowHtml(h, i) {
  const k = KINDS[h.k];
  const sel = state.selected && state.selected.k === h.k && state.selected.x === h.x && state.selected.z === h.z;
  const extra = extraText(h);
  const where = h.b >= 0 ? `${biomeName(h.b)} · ` : '';
  return `<li class="r${sel ? ' sel' : ''}" data-i="${i}">${markHtml(h.k)}<div class="what"><b>${k.name}</b><small>${where}<span class="dist">${fmt.format(Math.round(h.d))} ブロック</span>${extra ? ` · ${extra}` : ''}</small></div><div class="xz"><span><em>X</em>${h.x}</span><span><em>Z</em>${h.z}</span></div><button type="button" class="tp" data-i="${i}" aria-label="${k.name} の tp コマンドをコピー">tp</button></li>`;
}

function renderRows() {
  const rows = $('rows');
  const n = state.list.length;
  if (!n) {
    rows.innerHTML = `<li class="empty">${state.running ? '計算中…' : '表示する種類がありません。上の種類をタップしてください。'}</li>`;
    $('more').hidden = true;
    return;
  }
  const end = Math.min(n, state.shown);
  let html = '';
  for (let i = 0; i < end; i++) html += rowHtml(state.list[i], i);
  rows.innerHTML = html;
  $('more').hidden = end >= n;
  $('more').textContent = `さらに表示（残り ${fmt.format(n - end)} 件）`;
}

function renderHead() {
  const total = state.list.length;
  const dim = DIMS[state.dim].name;
  $('ledger-h').textContent = total ? `${dim}の座標 ${fmt.format(total)} 件` : `${dim}の座標`;
}

function refresh() {
  buildList();
  renderHead();
  renderRows();
  renderDims();
  drawMap();
}

$('more').addEventListener('click', () => { state.shown += 400; renderRows(); });
$('sort').addEventListener('change', (e) => { state.sort = e.target.value; state.shown = 200; refresh(); });

async function copyText(text) {
  try {
    await navigator.clipboard.writeText(text);
    return true;
  } catch {
    return false;
  }
}

const tpCommand = (h) => `/tp @s ${h.x} ~ ${h.z}`;

$('rows').addEventListener('click', async (e) => {
  const li = e.target.closest('[data-i]');
  if (!li) return;
  const h = state.list[Number(li.dataset.i)];
  if (!h) return;
  if (e.target.closest('.tp')) {
    const btn = e.target.closest('.tp');
    if (await copyText(tpCommand(h))) {
      btn.textContent = 'コピー済';
      btn.classList.add('done');
      setTimeout(() => { btn.textContent = 'tp'; btn.classList.remove('done'); }, 1600);
    } else {
      btn.textContent = tpCommand(h);
    }
    return;
  }
  select(h, true);
});

$('copy-csv').addEventListener('click', async () => {
  const lines = ['種類,次元,X,Z,距離,バイオーム'];
  for (const h of state.list) {
    lines.push(`${KINDS[h.k].name},${DIMS[dimOf(h.k)].name},${h.x},${h.z},${Math.round(h.d)},${h.b >= 0 ? biomeName(h.b) : ''}`);
  }
  const text = lines.join('\n');
  const box = $('csvbox');
  if (await copyText(text)) {
    $('copy-csv').textContent = `${fmt.format(state.list.length)} 件をコピーしました`;
    setTimeout(() => { $('copy-csv').textContent = '表示中を CSV でコピー'; }, 2000);
    box.hidden = true;
  } else {
    box.value = text;
    box.hidden = false;
    box.focus();
    box.select();
  }
});

// ---------------------------------------------------------------------------
// 地図
const map = {
  canvas: $('map'),
  // 表示: 中心 (ブロック) と 1 ピクセルあたりのブロック数
  vx: 0, vz: 0, bpp: 1,
  // 次元ごとの背景。base: 全体 {img, x0, z0, scale, w, h}、fine: 拡大時の細かい背景
  bg: DIMS.map(() => ({ base: null, fine: null })),
  mapId: 0,
  want: {},     // `${次元}:${base|fine}` → 最後に頼んだ画像の番号
  fineTimer: 0,
};

function canvasSize() {
  const r = map.canvas.getBoundingClientRect();
  return { w: Math.max(1, r.width), h: Math.max(1, r.height) };
}

function fitMap() {
  const { w } = canvasSize();
  [map.vx, map.vz] = centerOf(state.dim);
  map.bpp = (state.range * 2) / w;
  drawMap();
  requestFine();
}

function toScreen(x, z) {
  const { w, h } = canvasSize();
  return [(x - map.vx) / map.bpp + w / 2, (z - map.vz) / map.bpp + h / 2];
}

function toWorld(px, py) {
  const { w, h } = canvasSize();
  return [(px - w / 2) * map.bpp + map.vx, (py - h / 2) * map.bpp + map.vz];
}

function drawShape(ctx, shape, x, y, r) {
  ctx.beginPath();
  if (shape === 'circle') ctx.arc(x, y, r, 0, Math.PI * 2);
  else if (shape === 'square') ctx.rect(x - r * 0.9, y - r * 0.9, r * 1.8, r * 1.8);
  else if (shape === 'triangle') { ctx.moveTo(x, y - r * 1.1); ctx.lineTo(x + r, y + r * 0.8); ctx.lineTo(x - r, y + r * 0.8); ctx.closePath(); }
  else if (shape === 'diamond') { ctx.moveTo(x, y - r * 1.2); ctx.lineTo(x + r * 1.1, y); ctx.lineTo(x, y + r * 1.2); ctx.lineTo(x - r * 1.1, y); ctx.closePath(); }
  else {
    const a = r * 0.42;
    ctx.moveTo(x - a, y - r); ctx.lineTo(x + a, y - r); ctx.lineTo(x + a, y - a); ctx.lineTo(x + r, y - a);
    ctx.lineTo(x + r, y + a); ctx.lineTo(x + a, y + a); ctx.lineTo(x + a, y + r); ctx.lineTo(x - a, y + r);
    ctx.lineTo(x - a, y + a); ctx.lineTo(x - r, y + a); ctx.lineTo(x - r, y - a); ctx.lineTo(x - a, y - a); ctx.closePath();
  }
}

function drawBg(ctx, bg) {
  if (!bg || !bg.img) return;
  const [sx, sy] = toScreen(bg.x0, bg.z0);
  ctx.drawImage(bg.img, sx, sy, (bg.w * bg.scale) / map.bpp, (bg.h * bg.scale) / map.bpp);
}

function drawMap() {
  const cv = map.canvas;
  const { w, h } = canvasSize();
  const dpr = Math.min(window.devicePixelRatio || 1, 3);
  if (cv.width !== Math.round(w * dpr) || cv.height !== Math.round(h * dpr)) {
    cv.width = Math.round(w * dpr);
    cv.height = Math.round(h * dpr);
  }
  const ctx = cv.getContext('2d');
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  const css = getComputedStyle(document.documentElement);
  ctx.fillStyle = css.getPropertyValue('--map-bg').trim();
  ctx.fillRect(0, 0, w, h);

  // バイオームの背景は薄くして印を目立たせる
  ctx.imageSmoothingEnabled = false;
  ctx.globalAlpha = Number(css.getPropertyValue('--map-dim')) || 0.5;
  drawBg(ctx, map.bg[state.dim].base);
  drawBg(ctx, map.bg[state.dim].fine);
  ctx.globalAlpha = 1;

  // 1000 ブロックごとの方眼 (縮小時は 5000)
  const step = map.bpp > 25 ? 5000 : map.bpp > 6 ? 1000 : map.bpp > 1.5 ? 500 : 100;
  const [wx0, wz0] = toWorld(0, 0);
  const [wx1, wz1] = toWorld(w, h);
  ctx.lineWidth = 1;
  ctx.strokeStyle = css.getPropertyValue('--line').trim();
  ctx.fillStyle = css.getPropertyValue('--muted').trim();
  ctx.font = `11px ${css.getPropertyValue('--font-num')}`;
  ctx.beginPath();
  for (let x = Math.ceil(wx0 / step) * step; x <= wx1; x += step) {
    const [px] = toScreen(x, 0);
    ctx.moveTo(Math.round(px) + 0.5, 0); ctx.lineTo(Math.round(px) + 0.5, h);
  }
  for (let z = Math.ceil(wz0 / step) * step; z <= wz1; z += step) {
    const [, py] = toScreen(0, z);
    ctx.moveTo(0, Math.round(py) + 0.5); ctx.lineTo(w, Math.round(py) + 0.5);
  }
  ctx.globalAlpha = 0.7;
  ctx.stroke();
  ctx.globalAlpha = 1;
  for (let x = Math.ceil(wx0 / step) * step; x <= wx1; x += step) {
    const [px] = toScreen(x, 0);
    if (px > 24 && px < w - 50) ctx.fillText(String(x), px + 3, 12);
  }
  for (let z = Math.ceil(wz0 / step) * step; z <= wz1; z += step) {
    const [, py] = toScreen(0, z);
    if (py > 24 && py < h - 8) ctx.fillText(String(z), 3, py - 3);
  }

  // 原点
  const [ox, oy] = toScreen(0, 0);
  ctx.strokeStyle = css.getPropertyValue('--ink').trim();
  ctx.lineWidth = 2;
  ctx.beginPath();
  ctx.moveTo(ox - 7, oy); ctx.lineTo(ox + 7, oy); ctx.moveTo(ox, oy - 7); ctx.lineTo(ox, oy + 7);
  ctx.stroke();

  // 構造物: 件数の多い種類を下に、少ない種類を上に描く。
  // 縮小しているときは主要な種類だけ形で描き、ほかは小さな点にする。
  // チャンク単位の種類 (スライムチャンク) は、チャンクが見分けられるまで拡大したときだけ塗る。
  const surface = css.getPropertyValue('--surface').trim();
  const zoomedOut = map.bpp > 12;
  const r = map.bpp > 40 ? 3 : map.bpp > 12 ? 4.5 : 5.5;
  const kinds = [...state.enabled].filter((k) => dimOf(k) === state.dim)
    .sort((a, b) => state.hits[b].length - state.hits[a].length);
  let hiddenChunks = false;
  for (const k of kinds) {
    const fill = cssVar(colorVar(k));
    ctx.fillStyle = fill;
    if (KINDS[k].chunk) {
      if (map.bpp > 4) { hiddenChunks = hiddenChunks || state.hits[k].length > 0; continue; }
      const s = 16 / map.bpp;
      ctx.globalAlpha = 0.55;
      for (const p of state.hits[k]) {
        const [px, py] = toScreen((p.x >> 4) * 16, (p.z >> 4) * 16);
        if (px < -s || py < -s || px > w || py > h) continue;
        ctx.fillRect(px, py, s, s);
      }
      ctx.globalAlpha = 1;
      continue;
    }
    const shape = shapeOf(k);
    const dot = zoomedOut && !KINDS[k].main;
    const size = KINDS[k].big ? r + 3 : r;
    for (const p of state.hits[k]) {
      const [px, py] = toScreen(p.x, p.z);
      if (px < -10 || py < -10 || px > w + 10 || py > h + 10) continue;
      if (dot) {
        ctx.fillRect(px - 1, py - 1, 2, 2);
        continue;
      }
      drawShape(ctx, shape, px, py, size);
      ctx.fill();
      ctx.lineWidth = 1.5;
      ctx.strokeStyle = surface;
      ctx.stroke();
    }
  }
  $('map-hint').hidden = !hiddenChunks;

  if (state.selected) {
    const [px, py] = toScreen(state.selected.x, state.selected.z);
    ctx.lineWidth = 2.5;
    ctx.strokeStyle = css.getPropertyValue('--ink').trim();
    ctx.beginPath();
    ctx.arc(px, py, r + 7, 0, Math.PI * 2);
    ctx.stroke();
  }
}

function requestBase(dim) {
  if (state.seed === null) return;
  const [cx, cz] = centerOf(dim);
  const scale = state.range * 2 / 64 > 700 ? 256 : 64;
  const x0 = Math.floor((cx - state.range) / scale) * scale;
  const z0 = Math.floor((cz - state.range) / scale) * scale;
  const n = Math.ceil((state.range * 2) / scale) + 2;
  sendMap(dim, 'base', x0, z0, n, n, scale);
}

// 拡大したら見えている範囲だけ細かく描き直す
function requestFine() {
  clearTimeout(map.fineTimer);
  map.fineTimer = setTimeout(() => {
    if (state.seed === null) return;
    const bg = map.bg[state.dim];
    const baseScale = bg.base ? bg.base.scale : 64;
    const scale = [4, 16, 64].find((s) => s >= map.bpp) || 64;
    if (scale >= baseScale) { bg.fine = null; drawMap(); return; }
    const { w, h } = canvasSize();
    const [wx0, wz0] = toWorld(0, 0);
    const x0 = Math.floor(wx0 / scale) * scale;
    const z0 = Math.floor(wz0 / scale) * scale;
    const cw = Math.ceil((w * map.bpp) / scale) + 2;
    const ch = Math.ceil((h * map.bpp) / scale) + 2;
    if (cw * ch > 1500000) return;
    sendMap(state.dim, 'fine', x0, z0, cw, ch, scale);
  }, 250);
}

function sendMap(dim, key, x0, z0, w, h, scale) {
  const id = ++map.mapId;
  map.want[`${dim}:${key}`] = id;
  state.engine.postMessage({ op: 'map', id, key, dim, x0, z0, w, h, scale, ...seedParts(), mc: state.mc });
}

function onMapImage(m) {
  if (m.id !== map.want[`${m.dim}:${m.key}`]) return;
  const cv = document.createElement('canvas');
  cv.width = m.w;
  cv.height = m.h;
  cv.getContext('2d').putImageData(new ImageData(m.rgba, m.w, m.h), 0, 0);
  map.bg[m.dim][m.key] = { img: cv, x0: m.x0, z0: m.z0, w: m.w, h: m.h, scale: m.scale };
  if (m.dim === state.dim) drawMap();
}

function zoom(f, px, py) {
  const { w, h } = canvasSize();
  if (px === undefined) { px = w / 2; py = h / 2; }
  const [wx, wz] = toWorld(px, py);
  const maxBpp = (state.range * 2) / w * 1.5;
  map.bpp = Math.min(maxBpp, Math.max(0.25, map.bpp / f));
  map.vx = wx - (px - w / 2) * map.bpp;
  map.vz = wz - (py - h / 2) * map.bpp;
  drawMap();
  requestFine();
}

$('zin').addEventListener('click', () => zoom(2));
$('zout').addEventListener('click', () => zoom(0.5));
$('zfit').addEventListener('click', () => { map.bg[state.dim].fine = null; fitMap(); });

{
  const pts = new Map();
  let moved = 0;
  let pinch = 0;
  const cv = map.canvas;
  const local = (e) => { const r = cv.getBoundingClientRect(); return [e.clientX - r.left, e.clientY - r.top]; };
  cv.addEventListener('pointerdown', (e) => {
    cv.setPointerCapture(e.pointerId);
    pts.set(e.pointerId, local(e));
    moved = 0;
    if (pts.size === 2) {
      const [a, b] = [...pts.values()];
      pinch = Math.hypot(a[0] - b[0], a[1] - b[1]);
    }
  });
  cv.addEventListener('pointermove', (e) => {
    if (!pts.has(e.pointerId)) return;
    const prev = pts.get(e.pointerId);
    const cur = local(e);
    pts.set(e.pointerId, cur);
    if (pts.size === 1) {
      const dx = cur[0] - prev[0], dy = cur[1] - prev[1];
      moved += Math.abs(dx) + Math.abs(dy);
      map.vx -= dx * map.bpp;
      map.vz -= dy * map.bpp;
      drawMap();
    } else if (pts.size === 2) {
      const [a, b] = [...pts.values()];
      const d = Math.hypot(a[0] - b[0], a[1] - b[1]);
      if (pinch > 0) zoom(d / pinch, (a[0] + b[0]) / 2, (a[1] + b[1]) / 2);
      pinch = d;
      moved += 10;
    }
  });
  const end = (e) => {
    if (!pts.has(e.pointerId)) return;
    const p = pts.get(e.pointerId);
    pts.delete(e.pointerId);
    if (pts.size < 2) pinch = 0;
    if (moved < 6 && pts.size === 0) pickAt(p[0], p[1]);
    else requestFine();
  };
  cv.addEventListener('pointerup', end);
  cv.addEventListener('pointercancel', end);
  cv.addEventListener('wheel', (e) => {
    e.preventDefault();
    const [px, py] = local(e);
    zoom(Math.exp(-e.deltaY * 0.0015), px, py);
  }, { passive: false });
}

function pickAt(px, py) {
  let best = null;
  let bestD = 18;
  for (const k of state.enabled) {
    if (dimOf(k) !== state.dim) continue;
    // チャンク単位の種類は塗っているときだけ、チャンクの中をタップしたら選ぶ
    const chunk = KINDS[k].chunk;
    if (chunk && map.bpp > 4) continue;
    for (const h of state.hits[k]) {
      const [sx, sy] = toScreen(h.x, h.z);
      const d = chunk ? Math.max(Math.abs(sx - px), Math.abs(sy - py)) * 0.99 : Math.hypot(sx - px, sy - py);
      if (chunk && d > 8 / map.bpp) continue;
      if (d < bestD) { bestD = d; best = { k, ...h }; }
    }
  }
  if (best) select(best, false);
}

function select(h, center) {
  state.selected = h;
  const k = KINDS[h.k];
  const pick = $('pick');
  pick.hidden = false;
  const extra = extraText(h);
  const where = h.b >= 0 ? `${biomeName(h.b)} · ` : '';
  pick.innerHTML = `${markHtml(h.k)}<b>${k.name}${BADGE[k.conf] ? `（${BADGE[k.conf]}）` : ''}</b><span class="xz">X ${h.x} / Z ${h.z}</span><small>${where}中心から ${fmt.format(Math.round(h.d))} ブロック · 高さの目安: ${k.y}${extra ? ` · ${extra}` : ''}</small><small><button type="button" class="tp" id="pick-tp">${tpCommand(h)} をコピー</button></small>`;
  $('pick-tp').addEventListener('click', async (e) => {
    if (await copyText(tpCommand(h))) e.target.textContent = 'コピーしました';
  });
  if (center) {
    map.vx = h.x;
    map.vz = h.z;
    const { w } = canvasSize();
    map.bpp = Math.min(map.bpp, 1500 / w);
    requestFine();
    map.canvas.scrollIntoView({ block: 'nearest', behavior: 'smooth' });
  }
  renderRows();
  drawMap();
}

new ResizeObserver(() => drawMap()).observe(map.canvas);
matchMedia('(prefers-color-scheme: dark)').addEventListener('change', () => drawMap());
new MutationObserver(() => drawMap()).observe(document.documentElement, { attributes: true, attributeFilter: ['data-theme'] });

// ---------------------------------------------------------------------------
// 計算の実行
function seedParts() {
  const u = state.seed;
  return { seedLo: Number(u & 0xffffffffn), seedHi: Number(u >> 32n) };
}

function setStatus(html) { $('status').innerHTML = html; }

function run() {
  const seed = parseSeed($('seed').value);
  if (seed === null) {
    setStatus('シード値を入力してください。');
    return;
  }
  const range = Math.min(30000, Math.max(500, Math.round(Number($('range').value) || 10000)));
  $('range').value = range;
  state.seed = seed;
  state.mc = Number($('version').value);
  state.cx = Math.round(Number($('cx').value) || 0);
  state.cz = Math.round(Number($('cz').value) || 0);
  state.range = range;
  state.hits = KINDS.map(() => []);
  state.missing = new Set();
  state.selected = null;
  state.shown = 200;
  state.running = true;
  $('pick').hidden = true;
  if (!state.example) {
    store.set('mcbe.seed', $('seed').value.trim());
    store.set('mcbe.version', String(state.mc));
  }

  const signed = BigInt.asIntN(64, seed).toString();
  const typed = $('seed').value.trim();
  state.seedLabel = typed === signed ? signed : `${signed}（「${escapeHtml(typed)}」から変換）`;
  setStatus(`シード <strong>${state.seedLabel}</strong> を計算中…`);
  $('go').disabled = true;
  $('bar').style.width = '0%';

  // 重い種類は表示がオンのときだけ計算する。表示中の次元の、軽い種類から先に出す。
  const kinds = KINDS.map((_, i) => i).filter((i) => !KINDS[i].lazy || state.enabled.has(i));
  const order = kinds.sort((a, b) => (dimOf(a) !== state.dim) - (dimOf(b) !== state.dim)
    || (PARTS[a] || 0) - (PARTS[b] || 0));
  state.asked = new Set(order);
  state.finished = new Set();
  state.runId++;
  const rects = DIMS.map((_, d) => {
    const [cx, cz] = centerOf(d);
    return [cx - range, cz - range, cx + range, cz + range];
  });
  state.engine.postMessage({
    op: 'run', id: state.runId, ...seedParts(), mc: state.mc,
    rects, kdim: KINDS.map((_, i) => dimOf(i)), kinds: order, parts: partsFor(order),
  });
  map.bg = DIMS.map(() => ({ base: null, fine: null }));
  map.want = {};
  fitMap();
  requestBase(state.dim);
  renderChips();
  refresh();
}

// 種類ごとの分割数 (範囲の広さに合わせる)
function partsFor(kinds) {
  const area = (state.range * 2) ** 2 / 20000 ** 2;
  const parts = {};
  for (const k of kinds) if (PARTS[k]) parts[k] = Math.max(1, Math.round(PARTS[k] * area));
  return parts;
}

let refreshTimer = 0;
function onEngine(e) {
  const m = e.data;
  if (m.op === 'map') { onMapImage(m); return; }
  if (m.op === 'error') { setStatus(`計算でエラーが起きました: ${m.message}`); $('go').disabled = false; return; }
  if (m.op === 'ready') return;
  if (m.id !== state.runId) return;
  if (m.op === 'missing') {
    state.missing.add(m.kind);
  } else if (m.op === 'hits') {
    const list = state.hits[m.kind];
    const [cx, cz] = centerOf(dimOf(m.kind));
    const d = m.data;
    for (let i = 0; i < d.length; i += 4) {
      list.push({ x: d[i], z: d[i + 1], b: d[i + 2], info: d[i + 3], d: Math.hypot(d[i] - cx, d[i + 1] - cz) });
    }
  } else if (m.op === 'progress') {
    if (m.last) state.finished.add(m.kind);
    $('bar').style.width = `${(100 * m.done) / m.total}%`;
    // 描き直しはまとめて行う
    if (!refreshTimer) refreshTimer = setTimeout(() => { refreshTimer = 0; renderChips(); refresh(); }, 120);
  } else if (m.op === 'done') {
    state.running = false;
    $('go').disabled = false;
    clearTimeout(refreshTimer);
    refreshTimer = 0;
    renderChips();
    refresh();
    let total = 0;
    for (const k of state.enabled) total += state.hits[k].length;
    const where = state.cx || state.cz ? `（${state.cx}, ${state.cz}）` : '原点';
    const ex = state.example ? '<br>これは例です。自分のシード値を入れて「計算する」を押してください。' : '';
    setStatus(`シード <strong>${state.seedLabel}</strong>：${where}から ±${fmt.format(state.range)} に <strong>${fmt.format(total)}</strong> か所（表示中の種類、3 つの次元の合計）${ex}`);
  }
}

$('form').addEventListener('submit', (e) => {
  e.preventDefault();
  if (!state.engine) return;
  state.example = false;
  run();
});

async function main() {
  $('version').innerHTML = VERSIONS.map((v) => `<option value="${v.mc}">${v.label}</option>`).join('');
  const savedVersion = store.get('mcbe.version');
  if (savedVersion && VERSIONS.some((v) => String(v.mc) === savedVersion)) $('version').value = savedVersion;
  const savedEnabled = store.get('mcbe.enabled');
  if (savedEnabled !== null) {
    state.enabled = new Set(savedEnabled.split(',').filter(Boolean).map(Number).filter((k) => k >= 0 && k < KINDS.length));
    // 保存したあとに増えた種類は最初の表示のまま
    const known = Number(store.get('mcbe.known')) || 19;
    for (let k = known; k < KINDS.length; k++) if (!NOISY.has(k)) state.enabled.add(k);
  }
  const savedDim = Number(store.get('mcbe.dim'));
  if (savedDim > 0 && savedDim < DIMS.length) state.dim = savedDim;
  renderDims();
  renderChips();
  fitMap();
  try {
    const bytes = await loadWasm();
    state.engine = await startEngine(bytes, onEngine);
  } catch (err) {
    setStatus(`計算エンジンを読み込めませんでした: ${err.message}`);
    return;
  }
  const saved = store.get('mcbe.seed');
  if (saved) {
    $('seed').value = saved;
  } else {
    $('seed').value = '6666';
    state.example = true;
  }
  run();
}

main();
