# MP3 UI HTML 原型 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 构建 MP3 播放器 UI 的 HTML 原型（240×320 模拟屏幕、链表导航交互、三主题），验证布局与交互后迁移 LVGL。

**Architecture:** 单文件 HTML（内嵌 CSS/JS），JS 状态机模拟硬件（旋钮/双键/顶部按钮），三页面 + 导航页 + 主题切换。无触屏，全部键盘操作。

**Tech Stack:** 纯 HTML/CSS/JS（零依赖，浏览器直接打开）

## Global Constraints

- 屏幕模拟：固定 240×320 容器居中
- 交互：**无触屏**，键盘映射（↑↓=编码器、A=侧键A、S=侧键B、T=顶部按钮）
- 主题：三套预设（白色极简默认/深色 hifi/渐变），CSS 变量实现
- 设计语言：卡片式（圆角 16px、轻阴影、圆角控件）
- 播放页布局顺序（自上而下）：状态栏→封面卡片→歌名/歌手→音量条→进度条→控制区（模式/上首/播放/下首）
- YES/NO：绿红细条贴右缘（绿上=是/A、红下=否/B）
- 歌单判定：文件夹名匹配 `music/歌曲/音乐/mp3`（忽略大小写）→ 歌单模式
- 交付文件：`F:\ESP32idf\MP3_project\ui\mp3_player.html`

---

### Task 1: 骨架 + 屏幕容器 + 主题系统

**Files:**
- Create: `F:\ESP32idf\MP3_project\ui\mp3_player.html`

**Interfaces:**
- Produces: 全局 `theme` 对象（`{name, apply()}`）、CSS 变量三主题、240×320 屏幕容器 `#screen`、页面容器 `#page`

- [ ] **Step 1: 创建文件与基础结构**

```html
<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<title>MP3 Player UI Prototype</title>
<style>
/* CSS 变量：三主题 token */
:root {
  --bg: #f2f3f5; --card: #ffffff; --text: #1a1a1a; --text2: #8a8f99;
  --accent: #4f8cff; --shadow: 0 2px 8px rgba(0,0,0,.08); --radius: 16px;
}
.theme-dark { --bg: #0e1116; --card: #151a22; --text: #e8ecf2; --text2: #93a0b3; --accent: #d9a441; --shadow: 0 2px 10px rgba(0,0,0,.4); }
.theme-grad { --bg: linear-gradient(160deg,#6a5af9,#d9a441); --card: rgba(255,255,255,.92); --text: #1a1a1a; --text2: #666; --accent: #6a5af9; --shadow: 0 4px 16px rgba(0,0,0,.2); }
body { display:flex; justify-content:center; align-items:center; min-height:100vh; background:#dfe1e5; margin:0; font-family:system-ui,sans-serif; }
/* 屏幕模拟 */
#screen { width:240px; height:320px; background:var(--bg); border-radius:24px; overflow:hidden; position:relative;
  box-shadow:0 10px 40px rgba(0,0,0,.25); border:6px solid #333; }
/* 提示条（模拟机身右侧按钮/编码器位置） */
#hint { position:fixed; left:16px; top:16px; font-size:12px; color:#666; line-height:1.7; background:#fff; padding:10px 14px; border-radius:8px; box-shadow:0 2px 8px rgba(0,0,0,.1); }
</style>
</head>
<body>
<div id="hint"></div>
<div id="screen"><div id="page"></div></div>
<script>
/* 状态（全局） */
const S = {
  page: 'player',        // player | files | settings | nav
  theme: 'light',        // light | dark | grad
  focusIdx: 0,           // 当前焦点索引
  selected: false,       // 是否已选中元素（旋钮交互模式）
  power: true, screenOn: true,
  playing: false, time: 0, duration: 183, volume: 70, mode: 0, // 0顺序 1单曲 2随机
  song: { title:'勾指起誓', artist:'泠鸢yousa' },
  seekMode: false, seekPos: 0,
  navIdx: 0, filesPath: '/', fileFocus: 0,
  confirm: null,         // {msg, onYes, onNo} | null
};
/* 主题切换 */
const THEMES = { light:'light', dark:'dark', grad:'grad' };
function applyTheme() {
  const s = document.getElementById('screen');
  s.className = S.theme === 'dark' ? 'theme-dark' : S.theme === 'grad' ? 'theme-grad' : '';
}
</script>
</body>
</html>
```

- [ ] **Step 2: 浏览器验证骨架**

打开文件：`file:///F:/ESP32idf/MP3_project/ui/mp3_player.html`
预期：居中显示 240×320 模拟屏幕（白底圆角黑边框）、左下角提示条。

- [ ] **Step 3: Commit**

```bash
cd F:/ESP32idf/MP3_project && git add ui/mp3_player.html && git commit -m "ui: MP3 原型骨架+主题系统" 2>/dev/null || echo "非 git 仓库，跳过 commit"
```

---

### Task 2: 播放页静态布局

**Files:**
- Modify: `F:\ESP32idf\MP3_project\ui\mp3_player.html`

**Interfaces:**
- Consumes: `S`（Task 1）
- Produces: 播放页元素（`#page` 内结构），渲染函数 `renderPlayer()`，焦点元素列表 `playerElems`

- [ ] **Step 1: 添加播放页渲染**

```js
/* 播放页元素链表（焦点顺序） */
const playerElems = [
  { id:'cover', label:'封面' },
  { id:'title', label:'歌名' },
  { id:'volume', label:'音量' },
  { id:'progress', label:'进度' },
  { id:'mode', label:'播放模式' },
  { id:'prev', label:'上一首' },
  { id:'play', label:'播放' },
  { id:'next', label:'下一首' },
];
function renderPlayer() {
  const p = document.getElementById('page');
  p.innerHTML = `
  <div class="status"><span>${S.playing?'⏸':'♪'} ${S.playing?'正在播放':'已暂停'}</span><span>${fmtTime(S.time)}</span></div>
  <div class="cover">${S.song.title[0]}</div>
  <div class="songinfo"><div class="title">${S.song.title}</div><div class="artist">${S.song.artist}</div></div>
  <div class="bar vol"><span>🔊</span><div class="track"><div class="fill" style="width:${S.volume}%"></div></div><span>${S.volume}%</span></div>
  <div class="bar prog"><div class="track"><div class="fill" style="width:${S.time/S.duration*100}%"></div></div><span>${fmtTime(S.time)}/${fmtTime(S.duration)}</span></div>
  <div class="controls">
    <button class="ctl" data-act="mode">${['↻顺序','🔂单曲','🔀随机'][S.mode]}</button>
    <button class="ctl" data-act="prev">⏮</button>
    <button class="ctl play" data-act="play">${S.playing?'⏸':'▶'}</button>
    <button class="ctl" data-act="next">⏭</button>
  </div>`;
  paintFocus();
}
function fmtTime(t){ const m=Math.floor(t/60), s=Math.floor(t%60); return m+':'+String(s).padStart(2,'0'); }
```

CSS（Task 2 追加）：

```css
.status { display:flex; justify-content:space-between; padding:10px 12px 6px; font-size:11px; color:var(--text2); }
.cover { width:160px; height:160px; margin:14px auto 10px; border-radius:var(--radius); background:linear-gradient(145deg,#7c9cf5,#b0a6f0); display:flex; align-items:center; justify-content:center; font-size:56px; color:#fff; box-shadow:var(--shadow); }
.songinfo { text-align:center; padding:4px 12px; }
.songinfo .title { font-size:20px; font-weight:700; color:var(--text); white-space:nowrap; overflow:hidden; }
.songinfo .artist { font-size:13px; color:var(--text2); margin-top:2px; }
.bar { display:flex; align-items:center; gap:6px; padding:6px 14px; }
.bar .track { flex:1; height:6px; border-radius:3px; background:rgba(128,128,128,.25); overflow:hidden; }
.bar .fill { height:100%; background:var(--accent); border-radius:3px; }
.bar span { font-size:10px; color:var(--text2); min-width:30px; }
.controls { display:flex; justify-content:space-around; padding:10px 8px 0; }
.ctl { width:52px; height:44px; border-radius:12px; border:1px solid rgba(128,128,128,.3); background:var(--card); color:var(--text); font-size:18px; }
.ctl.play { background:var(--accent); color:#fff; border:none; font-size:20px; }
/* 焦点高亮 */
.focus { outline:2px solid var(--accent); outline-offset:2px; }
/* YES/NO 细条 */
.confirm { position:absolute; inset:0; background:rgba(0,0,0,.4); display:flex; align-items:center; justify-content:center; }
.confirm .msg { background:var(--card); color:var(--text); padding:18px 22px; border-radius:12px; font-size:14px; }
.confirm .strip { position:absolute; right:6px; width:44px; height:14px; border-radius:7px; font-size:9px; display:flex; align-items:center; justify-content:center; color:#fff; }
.confirm .strip.yes { top:40%; background:#3fae6a; }
.confirm .strip.no { top:56%; background:#e05c5c; }
```

- [ ] **Step 2: 验证**

打开页面，预期：播放页完整显示（状态栏/渐变封面卡/歌名歌手/音量条/进度条/4 控件），白底卡片风。

- [ ] **Step 3: Commit**

```bash
cd F:/ESP32idf/MP3_project && git add ui/mp3_player.html && git commit -m "ui: 播放页布局" 2>/dev/null || echo skip
```

---

### Task 3: 文件列表页（文件管理器 + 歌单模式）

**Files:**
- Modify: `F:\ESP32idf\MP3_project\ui\mp3_player.html`

**Interfaces:**
- Produces: `renderFiles()`、模拟文件树 `fileTree`、歌单关键词判定 `isPlaylistFolder(name)`

- [ ] **Step 1: 添加文件树数据与渲染**

```js
/* 模拟文件树 */
const fileTree = {
  '/': [
    { name:'MUSIC', type:'dir' },
    { name:'录音', type:'dir' },
    { name:'花海.mp3', type:'mp3', dur:'4:12' },
    { name:'勾指起誓.mp3', type:'mp3', dur:'3:03' },
    { name:'笔记.txt', type:'file' },
  ],
  '/MUSIC': [
    { name:'Summertime', type:'mp3', dur:'3:24' },
    { name:'夏日时光', type:'mp3', dur:'3:24' },
    { name:'勾指起誓', type:'mp3', dur:'3:03' },
    { name:'花海', type:'mp3', dur:'4:12' },
  ],
  '/录音': [ { name:'会议录音.wav', type:'mp3', dur:'12:00' } ],
};
const PLAYLIST_NAMES = ['music','歌曲','音乐','mp3'];
function isPlaylistFolder(n){ return PLAYLIST_NAMES.some(k=>n.toLowerCase().includes(k)); }
function renderFiles() {
  const items = fileTree[S.filesPath] || [];
  const playlist = isPlaylistFolder(S.filesPath.split('/').pop());
  const p = document.getElementById('page');
  let rows = items.map((it,i)=>{
    const icon = it.type==='dir' ? '📁' : it.type==='mp3' ? '🎵' : '📄';
    const right = it.type==='dir' ? '›' : it.dur || '';
    return `<div class="frow ${i===S.fileFocus?'focus':''}" data-i="${i}"><span class="fico">${icon}</span><span class="fname">${it.name}</span><span class="fdur">${right}</span></div>`;
  }).join('');
  p.innerHTML = `<div class="status"><span>◀ ${S.filesPath}</span><span>${playlist?'歌单':''}</span></div><div class="flist">${rows}</div>`;
}
```

CSS：

```css
.flist { padding:4px 0; }
.frow { display:flex; align-items:center; gap:8px; height:36px; padding:0 12px; border-radius:10px; margin:2px 8px; }
.frow.focus { background:var(--accent); color:#fff; }
.frow .fico { width:20px; text-align:center; }
.frow .fname { flex:1; overflow:hidden; white-space:nowrap; }
.frow .fdur { font-size:11px; opacity:.8; }
```

- [ ] **Step 2: 验证**

临时在 JS 末尾调 `renderFiles()`（或交互后验证），预期：`/` 显示文件夹+文件混合列表，`/MUSIC` 显示歌单样式（纯歌曲行）。

- [ ] **Step 3: Commit**

---

### Task 4: 设置页 + 导航页

**Files:**
- Modify: `F:\ESP32idf\MP3_project\ui\mp3_player.html`

**Interfaces:**
- Produces: `renderSettings()`、`renderNav()`、导航页链表 `navElems`

- [ ] **Step 1: 添加两页渲染**

```js
const navElems = ['播放页','文件列表','设置'];
const settingElems = ['播放模式','主题','系统信息'];
function renderSettings() {
  const vals = [['↻顺序 🔂单曲 🔀随机'][S.mode], {light:'白色极简',dark:'深色 hifi',grad:'渐变'}[S.theme], 'v1.0'];
  document.getElementById('page').innerHTML =
    `<div class="status"><span>◀ 设置</span></div><div class="flist">` +
    settingElems.map((n,i)=>`<div class="frow ${i===S.focusIdx?'focus':''}"><span class="fname">${n}</span><span class="fdur">${vals[i]}</span></div>`).join('') +
    `</div>`;
}
function renderNav() {
  document.getElementById('page').innerHTML =
    `<div class="status"><span>☰ 导航</span></div><div class="flist">` +
    navElems.map((n,i)=>`<div class="frow ${i===S.navIdx?'focus':''}"><span class="fname">${n}</span></div>`).join('') +
    `</div>`;
}
```

- [ ] **Step 2: 验证**

临时调用两函数，预期：设置页（模式/主题/系统信息三行）、导航页（三标签）正常显示。

- [ ] **Step 3: Commit**

---

### Task 5: 交互引擎（链表导航 + 按键映射）

**Files:**
- Modify: `F:\ESP32idf\MP3_project\ui\mp3_player.html`

**Interfaces:**
- Consumes: 各页 render 函数
- Produces: `handleRotate(dir)`、`handleA()`、`handleB()`、`handleTop(dur)`、`renderAll()` 分发、`paintFocus()`

- [ ] **Step 1: 实现核心交互**

```js
/* 统一渲染分发 */
function renderAll(){ if(S.confirm){renderConfirm();return;} if(!S.screenOn){renderOff();return;}
  if(S.page==='player')renderPlayer(); else if(S.page==='files')renderFiles(); else if(S.page==='settings')renderSettings(); else renderNav(); }
/* 焦点绘制（播放页专用：按 playerElems 索引加 .focus） */
function paintFocus(){
  if(S.page!=='player') return;
  document.querySelectorAll('.focus').forEach(e=>e.classList.remove('focus'));
  const el = document.querySelectorAll('.cover,.songinfo,.bar,.controls .ctl')[S.focusIdx];
  if(el) el.classList.add('focus');
}
/* 编码器旋转：dir=+1/-1 */
function handleRotate(dir){
  if(S.confirm || !S.screenOn) return;
  if(S.seekMode){ S.seekPos=Math.max(0,Math.min(S.duration,S.seekPos+dir*5)); renderPlayer(); return; }
  if(S.page==='player'){
    if(S.selected){ // 已选中：操作元素
      const el=playerElems[S.focusIdx].id;
      if(el==='volume') S.volume=Math.max(0,Math.min(100,S.volume+dir*2));
      if(el==='progress'){ S.seekMode=true; S.seekPos=S.time; }
      renderPlayer();
    } else { S.focusIdx=Math.max(0,Math.min(playerElems.length-1,S.focusIdx+dir)); renderPlayer(); }
  } else if(S.page==='files'){
    const n=(fileTree[S.filesPath]||[]).length;
    S.fileFocus=Math.max(0,Math.min(n-1,S.fileFocus+dir)); renderFiles();
  } else if(S.page==='settings'){ S.focusIdx=Math.max(0,Math.min(settingElems.length-1,S.focusIdx+dir)); renderSettings(); }
  else if(S.page==='nav'){ S.navIdx=Math.max(0,Math.min(navElems.length-1,S.navIdx+dir)); renderNav(); }
}
/* 侧键 A：选中/进入/确认 */
function handleA(){
  if(S.confirm){ S.confirm.onYes(); S.confirm=null; renderAll(); return; }
  if(!S.screenOn) return;
  if(S.seekMode){ S.time=S.seekPos; S.seekMode=false; renderPlayer(); return; } // seek 确认
  if(S.page==='player'){
    if(!S.selected){ S.selected=true; renderPlayer(); return; }
    const el=playerElems[S.focusIdx].id;
    if(el==='play') S.playing=!S.playing;
    if(el==='mode') S.mode=(S.mode+1)%3;
    if(el==='prev'||el==='next') S.song.title=S.song.title==='勾指起誓'?'花海':'勾指起誓';
    if(el==='progress'){ S.seekMode=true; S.seekPos=S.time; }
    S.selected=false; renderPlayer();
  } else if(S.page==='files'){
    const it=(fileTree[S.filesPath]||[])[S.fileFocus];
    if(it && it.type==='dir'){ S.filesPath+=(S.filesPath==='/'?'':'/')+it.name; S.fileFocus=0; renderFiles(); }
    else if(it){ S.song={title:it.name.replace(/\.[^.]+$/,''),artist:'未知歌手'}; S.page='player'; S.selected=false; S.focusIdx=6; renderAll(); }
  } else if(S.page==='settings'){
    const i=S.focusIdx;
    if(i===1){ S.theme=S.theme==='light'?'dark':S.theme==='dark'?'grad':'light'; applyTheme(); }
    if(i===0) S.mode=(S.mode+1)%3;
    renderSettings();
  } else if(S.page==='nav'){ S.page=['player','files','settings'][S.navIdx]; S.focusIdx=0; renderAll(); }
}
/* 侧键 B：退出/取消 */
function handleB(){
  if(S.confirm){ S.confirm.onNo(); S.confirm=null; renderAll(); return; }
  if(S.seekMode){ S.seekMode=false; renderPlayer(); return; } // seek 取消
  if(S.page==='player' && S.selected){ S.selected=false; renderPlayer(); return; }
  if(S.page==='files'){ if(S.filesPath!=='/'){ S.filesPath=S.filesPath.slice(0,S.filesPath.lastIndexOf('/'))||'/'; S.fileFocus=0; } renderFiles(); return; }
  if(S.page==='settings'){ S.page='player'; renderAll(); return; }
}
/* 顶部按钮：dur<300 短按（熄屏切换） 双击=导航 长按>2000=关机 */
let topTimer=null, topCount=0;
function handleTop(dur){
  if(dur>=2000){ S.power=false; S.screenOn=false; renderAll(); return; } // 关机
  if(dur<300){ // 短按
    S.screenOn=!S.screenOn; renderAll();
    topCount++; if(topCount>=2){ S.page='nav'; S.screenOn=true; S.navIdx=0; renderAll(); topCount=0; }
    setTimeout(()=>topCount=0,600);
  }
}
function renderOff(){ document.getElementById('page').innerHTML='<div class="off">🔒</div>'; }
function renderConfirm(){
  document.getElementById('page').innerHTML=
    `<div class="confirm"><div class="msg">${S.confirm.msg}</div><div class="strip yes">是</div><div class="strip no">否</div></div>`;
}
/* 键盘映射 */
document.addEventListener('keydown', e=>{
  if(e.key==='ArrowUp') handleRotate(+1);
  if(e.key==='ArrowDown') handleRotate(-1);
  if(e.key==='a'||e.key==='A') handleA();
  if(e.key==='s'||e.key==='S') handleB();
  if(e.key==='t'||e.key==='T'){ const t0=Date.now(); const up=()=>handleTop(Date.now()-t0); document.removeEventListener('keyup',up); document.addEventListener('keyup',up); }
});
/* 顶部按钮长按支持：keydown 记录，keyup 计算时长 */
/* 走秒（播放状态） */
setInterval(()=>{ if(S.playing && S.screenOn && !S.confirm){ S.time=(S.time+1)%S.duration; if(S.page==='player')renderPlayer(); } },1000);
```

- [ ] **Step 2: 验证清单（逐项）**

打开页面后依次验证：
1. `↑`/`↓`：焦点在播放页元素间移动（高亮环）
2. `A` 选中音量条 → `↑`/`↓` 调音量（百分比变化）→ `B` 退出选中
3. `A` 选中进度条 → 进入 seek（`A` 确认跳转 / `B` 取消）
4. `A` 选中播放按钮 → 播放/暂停切换（▶/⏸ + 时间走秒）
5. `A` 选中模式按钮 → 顺序/单曲/随机循环
6. 顶部 `T` 短按：熄屏（🔒）→ 再按点亮
7. `T` 快速两次：进入导航页 → `↑↓`+`A` 选标签进入
8. 播放页 `B` 无选中时无操作

- [ ] **Step 3: Commit**

---

### Task 6: 列表页交互 + YES/NO + 设置页联动

**Files:**
- Modify: `F:\ESP32idf\MP3_project\ui\mp3_player.html`

**Interfaces:**
- Consumes: `handleA/B`（Task 5）
- Produces: `S.confirm` 弹窗（删除确认等）、歌单播放联动

- [ ] **Step 1: 补全列表页交互**

```js
/* 列表页 A：进入文件夹（歌单判定）；B 已在 Task5 */
/* 文件删除 YES/NO：在 renderFiles 的 mp3 行 B 长按？——简化：文件列表页焦点在文件上按 A 播放，删除模拟：
   设置页"恢复出厂"触发 YES/NO */
function demoConfirm(){
  S.confirm = { msg:'确定恢复出厂设置？', onYes:()=>{ S.volume=70; S.mode=0; S.theme='light'; applyTheme(); },
                onNo:()=>{} };
  renderAll();
}
```

- [ ] **Step 2: 设置页触发 YES/NO**

修改 `renderSettings` 增加第四行"恢复出厂"，`handleA` 设置页 `i===3` 时调 `demoConfirm()`；YES/NO 交互：`A`=是（绿条）`B`=否（红条），已在 Task5 handleA/B 的 confirm 分支实现。

- [ ] **Step 3: 验证**

设置页 → 焦点"恢复出厂" → A → 出现遮罩+绿红细条（贴右缘）→ A 执行 / B 取消。音量/主题重置生效。

- [ ] **Step 4: Commit**

---

### Task 7: 主题切换按钮 + 整合验证

**Files:**
- Modify: `F:\ESP32idf\MP3_project\ui\mp3_player.html`

- [ ] **Step 1: 添加主题切换 UI**

在 `#screen` 外（屏幕右侧）加小按钮：

```html
<div style="position:fixed; right:16px; top:16px; display:flex; flex-direction:column; gap:6px;">
  <button onclick="cycleTheme()" style="padding:6px 10px;border-radius:8px;border:1px solid #ccc;background:#fff;cursor:pointer;font-size:12px">🎨 主题</button>
</div>
```

```js
function cycleTheme(){ S.theme=S.theme==='light'?'dark':S.theme==='dark'?'grad':'light'; applyTheme(); renderAll(); }
```

- [ ] **Step 2: 最终全流程验证清单**

1. 三主题切换：白/深/渐变，播放页各元素配色正确
2. 播放页焦点导航 + 音量/进度/播放/模式操作
3. 列表页进入 `/MUSIC`（歌单模式）→ A 播放歌曲 → 回播放页显示歌名
4. 设置页恢复出厂 → YES/NO 绿红细条
5. 熄屏/点亮/导航页/关机（关机后黑屏，`T` 长按开机）

- [ ] **Step 3: Commit**

```bash
cd F:/ESP32idf/MP3_project && git add ui/mp3_player.html && git commit -m "ui: MP3 原型完整交互" 2>/dev/null || echo skip
```

---

## Self-Review

- **Spec 覆盖**：屏幕 240×320 ✓(T1) · 三主题 ✓(T1,T7) · 播放页布局顺序 ✓(T2) · 文件管理器+歌单按名判定 ✓(T3) · 设置页 ✓(T4) · 导航页 ✓(T4) · 链表导航/旋钮双模式/seek 确认 ✓(T5) · YES/NO 绿红细条右缘 ✓(T6) · 顶部按钮三行为 ✓(T5) · 键盘映射 ✓(T5)
- **占位符扫描**：无 TBD；验证清单均为具体行为
- **类型一致性**：`S` 状态字段、`handleRotate/handleA/handleB`、`renderAll` 在各任务间一致
