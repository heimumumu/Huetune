// ===== 模拟数据与状态 =====
const songs = [
  { title: "アイドル", artist: "YOASOBI", album: "アイドル", src: "【我推的孩子】OP", dur: 213, cover: "01.jpg" },
  { title: "紅蓮華", artist: "LiSA", album: "紅蓮華", src: "鬼灭之刃 OP", dur: 239, cover: "02.jpg" },
  { title: "KICK BACK", artist: "米津玄師", album: "KICK BACK", src: "电锯人 OP", dur: 193, cover: "03.jpg" },
  { title: "新時代 (ウタ from ONE PIECE FILM RED)", artist: "Ado", album: "新時代", src: "海贼王 FILM RED", dur: 228, cover: "04.jpg" },
  { title: "SPECIALZ", artist: "King Gnu", album: "SPECIALZ", src: "咒术回战 第二季 OP", dur: 239, cover: "05.jpg" },
  { title: "残響散歌", artist: "Aimer", album: "残響散歌 / 朝が来る", src: "鬼灭之刃 游郭篇 OP", dur: 185, cover: "06.jpg" },
  { title: "勇者", artist: "YOASOBI", album: "勇者", src: "葬送的芙莉莲 OP", dur: 194, cover: "07.jpg" },
  { title: "スパークル", artist: "幾田りら", album: "スパークル", src: "你的名字。（翻唱）", dur: 214, cover: "08.jpg" },
  { title: "ミックスナッツ", artist: "Official髭男dism", album: "Rejoice", src: "间谍过家家 OP", dur: 211, cover: "09.jpg" },
  { title: "晴天", artist: "周杰倫", album: "葉惠美", src: "华语流行", dur: 270, cover: "10.jpg" },
  { title: "Bohemian Rhapsody", artist: "Queen", album: "A Night At The Opera", src: "英伦摇滚", dur: 354, cover: "11.jpg" },
];
const S = { idx: 0, pos: 37, playing: true, vol: 40, muted: false, linked: true, sens: 1, sound: true, page: "music" };
const $ = (id) => document.getElementById(id);
const fmt = (s) => `${Math.floor(s / 60)}:${String(Math.floor(s % 60)).padStart(2, "0")}`;

function log(msg) {
  const li = document.createElement("li");
  li.textContent = `${new Date().toLocaleTimeString()}  ${msg}`;
  $("log").prepend(li);
  while ($("log").children.length > 40) $("log").lastChild.remove();
}

let audioCtx;
function click() {
  if (!S.sound) return;
  try {
    audioCtx = audioCtx || new AudioContext();
    const o = audioCtx.createOscillator(), g = audioCtx.createGain();
    o.frequency.value = 1800; g.gain.setValueAtTime(0.06, audioCtx.currentTime);
    g.gain.exponentialRampToValueAtTime(0.0001, audioCtx.currentTime + 0.04);
    o.connect(g).connect(audioCtx.destination); o.start(); o.stop(audioCtx.currentTime + 0.05);
  } catch (e) {}
}

// ===== 音乐卡片 =====
function cardHTML() {
  const pad = (n) => String(n).padStart(2, "0");
  // 有封面就用真实图片，没有就用灰色占位
  const layers = (img) => {
    const a = img ? ` style="background-image:url('covers/${img}')"` : "";
    const cls = img ? "" : " cv-off";
    return `<div class="art-blur${cls}"${a}></div><div class="art-sharp${cls}"${a}></div><div class="tint"></div>`;
  };
  if (!S.linked) {
    return `${layers(null)}
      <div class="sheet"><div class="title">未连接配套程序</div>
      <div class="artist">手势控制仍可用，歌曲信息需要电脑端程序</div></div>`;
  }
  const s = songs[S.idx];
  // 只保留扫一眼要看的：歌名 / 歌手 / 播放状态 / 进度；专辑、出处等放进长按详情
  return `${layers(s.cover)}
    <div class="sheet"><div class="title"><span>${s.title}</span></div>
      <div class="artist">${s.artist}</div>
      <div class="play-st">${S.playing ? "▶ 播放中" : "❚❚ 已暂停"}</div>
      <div class="prog"><i style="width:${(S.pos / s.dur) * 100}%"></i></div>
      <div class="times"><span class="t-pos">${fmt(S.pos)}</span><span class="t-rem">-${fmt(s.dur - S.pos)}</span></div></div>
    <div class="detail"><div class="d-row"><small>专辑</small><b>${s.album}</b></div>
      <div class="d-row"><small>出处</small><b>${s.src}</b></div>
      <div class="d-row"><small>时长</small><b>${fmt(s.dur)}</b></div>
      <div class="d-row"><small>曲目</small><b>${pad(S.idx + 1)} / ${pad(songs.length)}</b></div>
      <div class="d-hint">轻点任意处关闭</div></div>`;
}

let card;
function makeCard() {
  const el = document.createElement("div");
  el.className = "card" + (!S.linked ? " offline" : S.playing ? "" : " paused");
  el.innerHTML = cardHTML();
  $("cardStage").appendChild(el);
  el.classList.toggle("sub", S.page !== "music");
  el.classList.toggle("classic", S.cover === "classic");
  applyPalette(el);
  // 歌名两行放得下就换行显示；放不下改成单行来回滚动
  requestAnimationFrame(() => {
    const t = el.querySelector(".title"), sp = t && t.querySelector("span");
    // 数歌名实际占几行：行内元素每一行对应一个矩形（不同字体混排时同一行可能多个，按 top 去重）
    if (!sp) return;
    const lines = new Set([...sp.getClientRects()].map((r) => Math.round(r.top))).size;
    if (lines <= 2) return;
    t.classList.add("scroll");
    const dist = sp.scrollWidth - t.clientWidth + 24;      // 多滚 24px，让结尾离开淡出区
    if (dist <= 0) return;
    sp.dataset.dist = dist; sp.dataset.t0 = performance.now();
  });
  return el;
}

// ===== 随封面变化的配色（数据来自 palette.js，由 tools/cover_palette.py 生成） =====
// 背景模式：blur = 透出模糊封面（浓度取保证对比度的最低值）；color = 封面主色（浓度固定较高）
S.bg = "blur";
// 封面显示模式：immersive = 沉浸封面（左侧清晰渐变到右侧模糊）；classic = 经典封面（左侧独立封面框）
S.cover = "classic"; // 默认经典封面
function setCoverMode(mode) {
  S.cover = mode; if (card) card.classList.toggle("classic", mode === "classic");
  document.querySelectorAll('.seg[data-set="cover"] span').forEach((x) => x.classList.toggle("on", x.dataset.v === mode));
  log("封面显示 → " + (mode === "classic" ? "经典封面" : "沉浸封面"));
}
function applyPalette(el) {
  const p = S.linked && window.COVER_PALETTE ? COVER_PALETTE[songs[S.idx].cover] : null;
  const v = p
    ? { tint: p.tint, a: S.bg === "color" ? p.ac : p.a, as: S.bg === "color" ? Math.max(p.as, p.ac) : p.as, dark: p.dark }
    : { tint: "60,63,70", a: 0.9, as: 0.9, dark: false };
  // 卡片和整个屏幕都设置：切歌动画时旧卡片保持旧配色，顶部标签等跟随新封面
  [el, $("screen")].forEach((x) => {
    x.style.setProperty("--tint", v.tint); x.style.setProperty("--a", v.a); x.style.setProperty("--as", v.as);
    x.classList.toggle("txt-dark", v.dark); x.classList.toggle("txt-light", !v.dark);
  });
}
function setBg(mode) {
  S.bg = mode; if (card) applyPalette(card);
  document.querySelectorAll('.seg[data-set="bg"] span').forEach((x) => x.classList.toggle("on", x.dataset.v === mode));
  log("背景 → " + (mode === "blur" ? "模糊封面" : "封面主色"));
}
function renderCard() { if (card) card.remove(); card = makeCard(); }
function updateCardState() {
  if (!card || !S.linked) return;
  card.classList.toggle("paused", !S.playing);
  card.querySelector(".play-st").textContent = S.playing ? "▶ 播放中" : "❚❚ 已暂停";
}

// 歌曲详情：长按后不滑动直接松手打开，再轻点一下关闭
function showDetail(on) {
  if (!card || !S.linked || S.page !== "music") return;
  card.classList.toggle("show-detail", on);
  log(on ? "长按松手 → 显示歌曲详情" : "轻点 → 关闭详情");
}

// 切歌：dir=+1 下一首（新卡片从上方落下），dir=-1 上一首（新卡片从下方升起）
let switching = false;
function switchTrack(dir, fromDy = 0) {
  if (switching) return;
  switching = true;
  click();
  log(dir > 0 ? "双指下滑 → 下一首（HID: Next Track）" : "双指上滑 → 上一首（HID: Previous Track）");
  const old = card, H = $("cardStage").clientHeight;
  old.classList.add("anim");
  old.style.transform = `translateY(${dir * H}px)`; old.style.opacity = "0";
  if (S.linked) { S.idx = (S.idx + dir + songs.length) % songs.length; S.pos = 0; S.playing = true; }
  card = makeCard();
  card.style.transform = `translateY(${-dir * H * 0.6}px)`; card.style.opacity = "0";
  requestAnimationFrame(() => requestAnimationFrame(() => {
    card.classList.add("anim"); card.style.transform = "translateY(0)"; card.style.opacity = "1";
  }));
  setTimeout(() => { old.remove(); card.classList.remove("anim"); switching = false; }, 300);
  hideHint();
  if (S.page !== "music") { // 其他页面没有卡片动画，用顶部提示代替
    const h = $("trackHint");
    h.textContent = (dir > 0 ? "下一首" : "上一首") + (S.linked ? "：" + songs[S.idx].title : "");
    h.className = "overlay track-hint show top";
    clearTimeout(hintTimer); hintTimer = setTimeout(hideHint, 1200);
  }
}

let hintTimer;
function showHint(dir) {
  const h = $("trackHint");
  h.textContent = dir > 0 ? "松手：下一首 ↓" : "松手：上一首 ↑";
  h.className = "overlay track-hint show " + (dir > 0 ? "top" : "bottom");
}
function hideHint() { $("trackHint").classList.remove("show"); }

// ===== 音量 / 静音 / 播放 =====
let hudTimer;
function showVolHud(keep) {
  const hud = $("volHud");
  hud.classList.add("show"); hud.classList.toggle("muted", S.muted);
  $("volNum").textContent = S.muted ? "静音" : S.vol + "%";
  $("volIcon").textContent = "音量";
  $("volBar").style.width = S.vol + "%";
  clearTimeout(hudTimer);
  if (!keep) hudTimer = setTimeout(() => hud.classList.remove("show"), 1200);
}
// 状态角标：只在异常时出现（静音 / 未连接）
function updateStatus() {
  $("muteIcon").classList.toggle("hidden", !S.muted);
  $("linkState").classList.toggle("hidden", S.linked);
}
function setVol(v, keep) {
  v = Math.max(0, Math.min(100, Math.round(v)));
  if (S.muted && v !== S.vol) { S.muted = false; log("调节音量 → 自动取消静音"); }
  S.vol = v; updateStatus(); showVolHud(keep);
}
function toggleMute() {
  S.muted = !S.muted; click(); updateStatus(); showVolHud();
  log(S.muted ? "双指双击 → 静音（HID: Mute）" : "双指双击 → 取消静音");
}
function togglePlay() {
  S.playing = !S.playing; click(); updateCardState();
  const fb = $("tapFb");
  fb.textContent = S.playing ? "▶ 继续播放" : "❚❚ 已暂停";
  fb.classList.remove("show"); void fb.offsetWidth; fb.classList.add("show");
  log(S.playing ? "单指轻点 → 播放（HID: Play/Pause）" : "单指轻点 → 暂停（HID: Play/Pause）");
}

// ===== 手势识别 =====
const LOCK_PX = 15, SWITCH_PX = 60, TAP_MOVE_PX = 10, TAP_MS = 300, TWO_FINGER_WAIT_MS = 150, DOUBLE_TAP_MS = 300, LONG_MS = 500;
let twoTapUntil = 0, longTimer = null;
const G = { pts: new Map(), sim: false, max: 0, axis: null, sx: 0, sy: 0, bx: 0, baseVol: 0, dy: 0, move: 0, t0: 0, frozen: false };

function toScreen(e) {
  const r = $("screen").getBoundingClientRect(), k = r.width / 480;
  return { x: (e.clientX - r.left) / k, y: (e.clientY - r.top) / k };
}
function centroid() {
  let x = 0, y = 0;
  G.pts.forEach((p) => { x += p.x; y += p.y; });
  return { x: x / G.pts.size, y: y / G.pts.size };
}
function fingerCount() { return G.sim ? 2 : G.pts.size; }
function drawFingers() {
  const box = $("fingers"); box.innerHTML = "";
  const list = [];
  G.pts.forEach((p) => { if (G.sim) list.push({ x: p.x - 28, y: p.y }, { x: p.x + 28, y: p.y }); else list.push(p); });
  list.forEach((p) => {
    const d = document.createElement("div"); d.className = "finger";
    d.style.left = p.x + "px"; d.style.top = p.y + "px"; box.appendChild(d);
  });
}
function isControl(t) { return t.closest(".seg, .slider, .gbtn"); }

$("screen").addEventListener("pointerdown", (e) => {
  if (G.pts.size === 0 && !e.shiftKey && isControl(e.target)) return; // 单指点控件 → 交给控件
  e.preventDefault();
  $("screen").setPointerCapture(e.pointerId);
  const p = toScreen(e);
  if (G.pts.size === 0) {
    Object.assign(G, { sim: e.pointerType === "mouse" && e.shiftKey, axis: null, sx: p.x, sy: p.y, dy: 0, move: 0, t0: performance.now(), frozen: false });
    G.pts.set(e.pointerId, p);
  } else if (G.pts.size === 1) {
    G.pts.set(e.pointerId, p);
    if (!G.axis) { const c = centroid(); G.sx = c.x; G.sy = c.y; } // 第二根手指落下，重设起点
  }
  G.max = G.pts.size === 1 ? fingerCount() : Math.max(G.max, fingerCount());
  drawFingers();
});

$("screen").addEventListener("pointermove", (e) => {
  if (!G.pts.has(e.pointerId)) return;
  G.pts.set(e.pointerId, toScreen(e));
  drawFingers();
  if (G.frozen) return;
  const c = centroid(), dx = c.x - G.sx, dy = c.y - G.sy;
  G.move = Math.max(G.move, Math.hypot(dx, dy));
  if (fingerCount() < 2) return; // 单指拖动留给页面，这里不处理
  if (!G.axis && G.move > LOCK_PX) {
    G.axis = Math.abs(dx) >= Math.abs(dy) ? "h" : "v"; // 锁定方向，松手前不再改变
    G.bx = c.x; G.by = c.y; G.baseVol = S.vol;
    log(G.axis === "h" ? "方向锁定：横向（音量）" : "方向锁定：纵向（切歌）");
    if (G.axis === "v" && card) card.classList.remove("anim");
  }
  if (G.axis === "h") {
    setVol(G.baseVol + ((c.x - G.bx) / 480) * 100 * S.sens, true);
  } else if (G.axis === "v") {
    G.dy = c.y - G.by;
    if (S.page === "music" && card && !switching) card.style.transform = `translateY(${G.dy * 0.5}px)`;
    if (Math.abs(G.dy) >= SWITCH_PX) showHint(Math.sign(G.dy)); else hideHint();
  }
});

let tapTimer;
function endGesture(e) {
  if (!G.pts.has(e.pointerId)) return;
  G.pts.delete(e.pointerId);
  drawFingers();
  if (G.pts.size > 0) { G.frozen = true; return; } // 双指先抬起一根：冻结，等全部抬起再结算
  if (LP.used) { LP.used = false; G.max = 0; twoTapUntil = 0; return; } // 长按切页已处理
  const now = performance.now(), dt = now - G.t0, n = G.max;
  if (n >= 2 && !G.axis && dt < TAP_MS && G.move < TAP_MOVE_PX) {
    if (twoTapUntil && G.t0 <= twoTapUntil) {
      twoTapUntil = 0;
      toggleMute();
    } else {
      twoTapUntil = now + DOUBLE_TAP_MS;
    }
    G.max = 0;
    return;
  }
  twoTapUntil = 0;
  if (n >= 2) {
    if (G.axis === "h") {
      log(`双指横滑 → 音量 ${S.vol}%`);
      clearTimeout(hudTimer); hudTimer = setTimeout(() => $("volHud").classList.remove("show"), 1200);
    } else if (G.axis === "v") {
      if (Math.abs(G.dy) >= SWITCH_PX) switchTrack(Math.sign(G.dy));
      else { springBack(); hideHint(); log("纵向距离不足，取消切歌"); }
    }
  } else if (dt < TAP_MS && G.move < TAP_MOVE_PX) {
    // 单指轻点：等待确认没有第二根手指（真机上用于区分双指轻点）
    clearTimeout(tapTimer);
    if (card && card.classList.contains("show-detail")) { showDetail(false); G.max = 0; return; } // 详情打开时，轻点 = 关闭
    tapTimer = setTimeout(() => { if (G.pts.size === 0 && S.page === "music") togglePlay(); }, TWO_FINGER_WAIT_MS);
  }
  G.max = 0;
}
$("screen").addEventListener("pointerup", endGesture);
$("screen").addEventListener("pointercancel", endGesture);

function springBack() {
  if (!card) return;
  card.classList.add("anim"); card.style.transform = "translateY(0)";
  setTimeout(() => card && card.classList.remove("anim"), 300);
}

// ===== 页面切换：单指长按 0.5s → 出现左右提示 → 往左/右拖过 60px 松手切页 =====
// 不拖直接松手：音乐页打开歌曲详情，其他页取消。页面首尾循环：音乐 ⇄ 系统 ⇄ 设置 ⇄ 音乐
const PAGES = ["music", "system", "settings"], PAGE_NAME = { music: "音乐", system: "系统", settings: "设置" };
const PAGE_SWIPE_PX = 60;
const LP = { timer: null, on: false, used: false, sx: 0, sy: 0, dx: 0, id: null };
const pageAt = (k) => PAGES[(PAGES.indexOf(S.page) + k + PAGES.length) % PAGES.length];
function renderDots() {
  const i = PAGES.indexOf(S.page);
  document.querySelectorAll("[data-dots]").forEach((d) => { d.innerHTML = PAGES.map((_, j) => `<i class="${j === i ? "on" : ""}"></i>`).join(""); });
}
let dotsTimer;
function goPage(p, how) {
  if (card) card.classList.remove("show-detail");
  S.page = p;
  document.querySelectorAll(".page").forEach((x) => x.classList.toggle("active", x.dataset.page === p));
  const sub = p !== "music";
  if (card) card.classList.toggle("sub", sub); // 子页面：封面整体变成模糊背景
  $("screen").classList.toggle("on-sub", sub);
  renderDots();
  $("pageDots").classList.add("show"); clearTimeout(dotsTimer);
  dotsTimer = setTimeout(() => $("pageDots").classList.remove("show"), 1200);
  if (how) log(how + " → " + PAGE_NAME[p]);
}
// 浮层：左边显示"‹ 上一页"，右边"下一页 ›"；拖过阈值的那一侧高亮
function updatePager() {
  $("pgCur").textContent = PAGE_NAME[S.page];
  $("pgL").textContent = "‹ " + PAGE_NAME[pageAt(-1)];
  $("pgR").textContent = PAGE_NAME[pageAt(1)] + " ›";
  $("pgL").classList.toggle("hit", LP.dx <= -PAGE_SWIPE_PX);
  $("pgR").classList.toggle("hit", LP.dx >= PAGE_SWIPE_PX);
  $("pgTip").innerHTML = "按住往左或往右拖，松手切换页面" + (S.page === "music" && S.linked ? "<br>不拖直接松手：查看歌曲详情" : "<br>不拖直接松手：取消");
  renderDots();
}
function openPager() {
  LP.on = true; LP.used = true; LP.dx = 0;
  updatePager(); $("pager").classList.add("show");
  if (navigator.vibrate) navigator.vibrate(15); // 真机上可用蜂鸣器/提示音代替
  log("单指长按 → 显示切页提示");
}
// 捕获阶段监听：按在设置页的按钮、滑块上长按也能触发
$("screen").addEventListener("pointerdown", (e) => {
  clearTimeout(LP.timer);
  if (LP.id !== null || e.shiftKey || $("guide").classList.contains("show")) { LP.id = null; return; } // 第二根手指 → 取消长按
  const p = toScreen(e);
  Object.assign(LP, { id: e.pointerId, sx: p.x, sy: p.y, dx: 0, on: false, used: false });
  LP.timer = setTimeout(() => { if (LP.id !== null && G.pts.size <= 1) openPager(); }, LONG_MS);
}, true);
window.addEventListener("pointermove", (e) => {
  if (e.pointerId !== LP.id) return;
  const p = toScreen(e);
  if (!LP.on) { if (Math.hypot(p.x - LP.sx, p.y - LP.sy) > TAP_MOVE_PX) { clearTimeout(LP.timer); LP.id = null; } return; }
  LP.dx = p.x - LP.sx; updatePager();
});
function endLongPress(e) {
  if (e.pointerId !== LP.id) return;
  clearTimeout(LP.timer); LP.id = null;
  if (!LP.on) return;
  LP.on = false; $("pager").classList.remove("show");
  if (LP.dx <= -PAGE_SWIPE_PX) goPage(pageAt(-1), "长按左滑");
  else if (LP.dx >= PAGE_SWIPE_PX) goPage(pageAt(1), "长按右滑");
  else if (S.page === "music") showDetail(true);
  else log("长按松手（没有滑动）→ 取消");
}
window.addEventListener("pointerup", endLongPress);
window.addEventListener("pointercancel", endLongPress);
// 长按之后浏览器还会补发一次 click，拦掉，避免顺手点到设置按钮
$("screen").addEventListener("click", (e) => { if (LP.used) { e.stopPropagation(); e.preventDefault(); LP.used = false; } }, true);



// ===== 系统页（模拟数据） =====
// 只放 4 项，大字号，扫一眼能看清
const meters = ["处理器", "内存", "显卡", "网络"];
$("sys").innerHTML = meters.map((zh, i) =>
  `<div class="meter"><small>${zh}</small><b id="m${i}">0</b><div class="mb"><i id="mb${i}"></i></div></div>`).join("");
function tickSys() {
  meters.forEach((m, i) => {
    const v = Math.round(10 + Math.random() * 70);
    $("m" + i).textContent = i === 3 ? (v / 10).toFixed(1) + " MB/s" : v + "%";
    $("mb" + i).style.width = v + "%";
  });
}

// ===== 首次开机手势引导 =====
function showGuide() { $("guide").classList.add("show"); log("显示手势引导（点任意处关闭）"); }
$("guide").addEventListener("pointerdown", (e) => { e.stopPropagation(); $("guide").classList.remove("show"); });

// ===== 设置 =====
// 亮度：整条都能拖，按下的位置直接就是数值
function setBright(v) {
  v = Math.max(10, Math.min(100, Math.round(v)));
  const b = $("bright"); b.dataset.v = v;
  b.querySelector("i").style.width = v + "%"; b.querySelector(".s-v").textContent = v + "%";
  $("screen").style.filter = `brightness(${0.3 + v / 100 * 0.7})`;
}
$("bright").addEventListener("pointerdown", (e) => {
  const b = $("bright"), move = (ev) => { if (LP.on) return; const r = b.getBoundingClientRect(); setBright(((ev.clientX - r.left) / r.width) * 100); };
  b.setPointerCapture(e.pointerId); move(e);
  b.onpointermove = move;
  b.onpointerup = b.onpointercancel = () => { b.onpointermove = null; log("亮度 → " + b.dataset.v + "%"); };
});
// 分段按钮：点哪个选哪个
const segActions = {
  bg: (v) => setBg(v),
  cover: (v) => setCoverMode(v),
  sens: (v, t) => { S.sens = +v; log("音量灵敏度 → " + t); },
  sound: (v) => { S.sound = v === "1"; log("提示音 → " + (S.sound ? "开" : "关")); },
  sleep: (v, t) => log("自动息屏 → " + t),
};
document.querySelectorAll(".seg").forEach((g) => g.addEventListener("click", (e) => {
  const o = e.target.closest("span[data-v]"); if (!o) return;
  g.querySelectorAll("span").forEach((x) => x.classList.toggle("on", x === o));
  segActions[g.dataset.set](o.dataset.v, o.textContent);
}));

// ===== 侧边面板与键盘 =====
const acts = {
  prev: () => switchTrack(-1), next: () => switchTrack(1),
  voldown: () => { setVol(S.vol - 5); log("音量 → " + S.vol + "%"); },
  volup: () => { setVol(S.vol + 5); log("音量 → " + S.vol + "%"); },
  mute: toggleMute, play: togglePlay,
  theme: () => setBg(S.bg === "blur" ? "color" : "blur"),
  cover: () => setCoverMode(S.cover === "classic" ? "immersive" : "classic"),
  detail: () => { if (S.page !== "music") goPage("music"); showDetail(!(card && card.classList.contains("show-detail"))); },
  guide: showGuide,
  pageprev: () => goPage(pageAt(-1), "长按左滑"),
  pagenext: () => goPage(pageAt(1), "长按右滑"),
  link: () => { S.linked = !S.linked; updateStatus(); renderCard(); log(S.linked ? "配套程序已连接" : "配套程序断开，进入仅 HID 模式"); },
};
document.querySelectorAll("[data-act]").forEach((b) => b.addEventListener("click", () => acts[b.dataset.act]()));
document.addEventListener("keydown", (e) => {
  if (e.target.tagName === "SELECT" || e.target.tagName === "INPUT") return;
  const map = { ArrowUp: "prev", ArrowDown: "next", ArrowLeft: "voldown", ArrowRight: "volup", KeyM: "mute", Space: "play", KeyT: "theme", KeyD: "detail", KeyG: "guide", KeyC: "cover", BracketLeft: "pageprev", BracketRight: "pagenext" };
  if (map[e.code]) { e.preventDefault(); acts[map[e.code]](); }
});
// 缩放：数字 = 倍数；"real:英寸:横向分辨率" = 按显示器换算成真机物理尺寸（屏幕约 165 PPI）
function applyScale(v) {
  let s = +v;
  if (v.startsWith("real")) {
    const [, inch, w] = v.split(":"), h = w * 9 / 16;
    const monitorPPI = Math.hypot(w, h) / inch;               // 显示器每英寸物理像素
    s = monitorPPI / (165 * window.devicePixelRatio);         // 真机 1px = 1/165 英寸，换算成 CSS 像素
  }
  $("screenWrap").style.setProperty("--s", s);
}
$("scale").addEventListener("change", (e) => applyScale(e.target.value));

// ===== 定时器 =====
function tickClock() { const d = new Date(); $("clock").textContent = `${d.getHours()}:${String(d.getMinutes()).padStart(2, "0")}`; }
setInterval(() => {
  tickClock();
  if (S.linked && S.playing && card) {
    const s = songs[S.idx];
    S.pos = Math.min(s.dur, S.pos + 1);
    card.querySelector(".prog i").style.width = (S.pos / s.dur) * 100 + "%";
    card.querySelector(".t-pos").textContent = fmt(S.pos);
    card.querySelector(".t-rem").textContent = "-" + fmt(s.dur - S.pos);
  }
}, 1000);
setInterval(tickSys, 1500);

// 歌名滚动：开头停 1.2s → 以 30px/s 滚到结尾 → 停 1.2s → 滚回开头，循环（和 LVGL 的来回滚动模式一致）
const SCROLL_PAUSE = 1200, SCROLL_SPEED = 30 / 1000;
setInterval(() => {
  document.querySelectorAll(".title.scroll span[data-dist]").forEach((sp) => {
    const d = +sp.dataset.dist, move = d / SCROLL_SPEED, cycle = 2 * (SCROLL_PAUSE + move);
    const t = (performance.now() - sp.dataset.t0) % cycle;
    const ease = (x) => 0.5 - Math.cos(Math.PI * x) / 2;   // 起止放缓
    let x;
    if (t < SCROLL_PAUSE) x = 0;
    else if (t < SCROLL_PAUSE + move) x = ease((t - SCROLL_PAUSE) / move) * d;
    else if (t < 2 * SCROLL_PAUSE + move) x = d;
    else x = (1 - ease((t - 2 * SCROLL_PAUSE - move) / move)) * d;
    sp.style.transform = `translateX(${-x.toFixed(1)}px)`;
  });
}, 33);

tickClock(); tickSys(); updateStatus(); renderCard();
// 预览用：index.html#night,system / #settings / #volume 直接打开对应画面（逗号可组合）
const h0 = location.hash.slice(1).split(",");
if (h0.includes("color")) setBg("color");
if (h0.includes("immersive")) setCoverMode("immersive");
const tN = h0.find((x) => /^t\d+$/.test(x)); // #t3 = 直接打开第 4 首
if (tN) { S.idx = Math.min(+tN.slice(1), songs.length - 1); renderCard(); }
renderDots();
["system", "settings"].forEach((p) => { if (h0.includes(p)) goPage(p); });
$("pageDots").classList.remove("show");
// #pager = 显示长按浮层；#pagerR / #pagerL = 浮层里已拖到右/左侧
if (h0.some((x) => x.startsWith("pager"))) {
  LP.dx = h0.includes("pagerR") ? 80 : h0.includes("pagerL") ? -80 : 0;
  updatePager(); $("pager").classList.add("show");
}
if (h0.includes("paused")) { S.playing = false; updateCardState(); }
if (h0.includes("volume")) showVolHud(true);
if (h0.includes("detail")) showDetail(true);
if (h0.includes("offline")) acts.link();
// 首次打开显示手势引导；#guide 强制显示，#noguide 跳过
if (h0.includes("guide") || (!h0.includes("noguide") && !localStorage.getItem("mc-guided"))) {
  showGuide(); localStorage.setItem("mc-guided", "1");
}
log("原型已加载，按住 Shift 拖动可模拟双指手势");
