/* 洗车机 PLC 模拟器看板 —— 原生 JS，无构建步骤。
 *
 * 数据流：
 *   /api/bootstrap 首屏一次（软元件目录、工序表、盒号主题）
 *   /api/state     300ms 轮询，只带当前表格里可见的软元件（几十个，不刷全量）
 *   /api/mqtt      600ms 增量拉报文（按 id 递增）
 */
'use strict';

const S = {
  boot: null,
  devices: [],
  flow: [],
  values: {},
  shown: {},            // name -> 上次显示的文本，避免每轮重写 DOM
  changedAt: {},
  rendered: [],
  filter: { area: '', q: '', onlyChanged: false },
  showAll: false,
  mqtt: { lastId: 0, items: 0, filter: '', q: '' },
  paused: false,
  speed: 1,
  stepIndex: null,
  trailLen: 0,
  boxNo: '',
  keyPoints: [],
  deviceCountText: '',
};

const $ = (id) => document.getElementById(id);
const LIMIT = 120;
const MAX_MESSAGES = 300;

/* ---------------------------------------------------------------- 小工具 */
function toast(text, isError) {
  const el = $('toast');
  el.textContent = text;
  el.className = 'toast show' + (isError ? ' err' : '');
  clearTimeout(toast._timer);
  toast._timer = setTimeout(() => { el.className = 'toast'; }, 2600);
}

function fmtDuration(seconds) {
  const total = Math.max(0, Math.floor(seconds || 0));
  const h = Math.floor(total / 3600);
  const m = Math.floor((total % 3600) / 60);
  const s = total % 60;
  const pad = (n) => String(n).padStart(2, '0');
  return h ? `${h}:${pad(m)}:${pad(s)}` : `${pad(m)}:${pad(s)}`;
}

function fmtTime(ts) {
  const date = new Date((ts || 0) * 1000);
  const pad = (n, w) => String(n).padStart(w || 2, '0');
  return `${pad(date.getHours())}:${pad(date.getMinutes())}:${pad(date.getSeconds())}.${pad(date.getMilliseconds(), 3)}`;
}

function esc(text) {
  return String(text == null ? '' : text)
    .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
}

async function api(path, body) {
  const options = body === undefined ? {} : {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  };
  const response = await fetch(path, options);
  return response.json();
}

/* ---------------------------------------------------------------- 流程图 */
const FLOW = { left: 56, right: 944, railY: 116, radius: 19 };

function nodeX(index, total) {
  if (total <= 1) return (FLOW.left + FLOW.right) / 2;
  return FLOW.left + (index * (FLOW.right - FLOW.left)) / (total - 1);
}

function buildFlow() {
  const svg = $('flowSvg');
  const steps = S.flow || [];
  const total = steps.length;
  if (!total) {
    svg.innerHTML = '<text x="340" y="120" text-anchor="middle" class="flow-empty">'
      + '该机型没有定义工序表（flow），看板只显示软元件与报文</text>';
    return;
  }

  const parts = [];
  parts.push(`<line class="rail" x1="${FLOW.left}" x2="${FLOW.right}" y1="${FLOW.railY}" y2="${FLOW.railY}"/>`);
  parts.push(`<line class="rail-done" id="railDone" x1="${FLOW.left}" x2="${FLOW.left}" y1="${FLOW.railY}" y2="${FLOW.railY}"/>`);

  steps.forEach((step, index) => {
    const x = nodeX(index, total);
    const classes = ['node', `node-${step.motion || 'idle'}`];
    parts.push(`
      <g class="${classes.join(' ')}" id="node-${index}" data-index="${index}">
        <circle class="node-circle" cx="${x}" cy="${FLOW.railY}" r="${FLOW.radius}"/>
        <text class="node-num" x="${x}" y="${FLOW.railY + 0.5}">${step.value}</text>
        <text class="node-name" x="${x}" y="${FLOW.railY + 40}">${esc(step.title)}</text>
        <text class="node-sub" x="${x}" y="${FLOW.railY + 56}">${esc(step.sStep || '')}</text>
      </g>`);
  });

  // 台车：现在走到哪一站
  parts.push(`
    <g class="cart" id="cart" style="transform: translate(${FLOW.left}px, 0)">
      <line class="cart-pole" x1="0" y1="80" x2="0" y2="95"/>
      <rect class="cart-body" x="-17" y="64" width="34" height="14" rx="4"/>
      <circle class="cart-wheel" cx="-10" cy="79" r="3.6"/>
      <circle class="cart-wheel" cx="10" cy="79" r="3.6"/>
    </g>`);

  svg.innerHTML = parts.join('');
}

function updateFlow(step) {
  const total = S.flow.length;
  if (!total) return;
  const index = step && step.index >= 0 ? step.index : -1;

  if (S.stepIndex !== index) {
    S.flow.forEach((_, i) => {
      const node = $('node-' + i);
      if (!node) return;
      node.classList.toggle('current', i === index);
      node.classList.toggle('done', index >= 0 && i < index);
    });
    const done = $('railDone');
    if (done) {
      const endX = index > 0 ? nodeX(index, total) : FLOW.left;
      done.setAttribute('x2', endX);
    }
    const cart = $('cart');
    if (cart) cart.style.transform = `translate(${nodeX(Math.max(index, 0), total)}px, 0)`;
    S.stepIndex = index;
  }

  // 停留时间每轮都变，单独更新
  const meta = $('kpiStepMeta');
  if (meta && step) meta.textContent = `${step.sStep || 'S–'} · 停留在 ${(step.dwell || 0).toFixed(1)}s`;
}

function renderTrail(trail) {
  const container = $('trail');
  if (!trail || !trail.length) {
    if (S.trailLen !== 0) {
      container.innerHTML = '<span class="trail-empty">还没有工序跳变（复位后会开始记录）</span>';
      S.trailLen = 0;
    }
    return;
  }
  if (trail.length === S.trailLen) return;
  S.trailLen = trail.length;
  const last = trail.length - 1;
  container.innerHTML = trail.map((item, index) => `
    <span class="trail-chip ${index === last ? 'last' : ''}" title="${esc(item.title)}">
      <b>${item.value}</b>${esc(item.title)}<i>${(item.dwell || 0).toFixed(1)}s</i>
    </span>`).join('');
  container.scrollLeft = container.scrollWidth;
}

/* ---------------------------------------------------------------- 关键点位 */
function buildKeyPoints(list) {
  const stepDevice = (S.boot.sim && S.boot.sim.stepDevice) || '';
  const faultDevice = (S.boot.sim && S.boot.sim.faultDevice) || '';
  $('keypoints').innerHTML = list.map((item) => {
    const kind = item.name === stepDevice ? ' step' : (item.name === faultDevice ? ' fault' : '');
    return `<div class="kp${kind}" data-name="${item.name}" title="${esc(item.note || '')}">
      <span class="kp-name">${item.name}</span>
      <span class="kp-val zero">–</span>
      ${item.note ? `<span class="kp-note">${esc(item.note)}</span>` : ''}
    </div>`;
  }).join('');
  S.keyPoints = list.map((item) => ({
    name: item.name,
    fault: item.name === faultDevice,
    el: $('keypoints').querySelector(`.kp[data-name="${item.name}"]`),
  }));
}

/* ---------------------------------------------------------------- 启动前置检查 */
function renderChecks(items) {
  const box = $('checks');
  const title = $('checksTitle');
  if (!box) return;
  if (!items || !items.length) {          // 机型没定义前置检查：整块隐藏，不留空壳
    box.hidden = true;
    if (title) title.hidden = true;
    return;
  }
  box.hidden = false;
  if (title) title.hidden = false;
  if (box.dataset.count !== String(items.length)) {
    box.dataset.count = String(items.length);
    box.innerHTML = items.map((item) => `
      <div class="check" data-key="${item.key}">
        <span class="check-dot"></span>
        <span class="check-title" title="${esc(item.hint)}">${esc(item.title)}</span>
        <span class="check-signal">${item.signal}=–</span>
        ${item.button
          ? `<button class="btn tiny" data-action="${item.action}">${esc(item.button)}</button>`
          : '<span class="check-auto">合成</span>'}
      </div>`).join('');
    box.querySelectorAll('button[data-action]').forEach((button) => {
      button.addEventListener('click', async () => {
        const data = await api('/api/control', { action: 'precondition', key: button.dataset.action });
        const items = data.preconditions || [];
        applyChecks(items);
        const target = items.find((item) => item.action === button.dataset.action);
        if (target && !target.passed) {
          toast(`${target.title} 还没通过：${target.hint || '动作已执行，但信号仍未满足'}`);
        } else {
          toast(`${button.textContent}：已执行（只改现场输入，不发命令）`);
        }
        pollState().catch(() => {});
      });
    });
  }
  applyChecks(items);
}

function applyChecks(items) {
  const box = $('checks');
  if (!box) return;
  items.forEach((item) => {
    const row = box.querySelector(`.check[data-key="${item.key}"]`);
    if (!row) return;
    row.classList.toggle('pass', !!item.passed);
    const cell = row.querySelector('.check-signal');
    if (cell) cell.textContent = `${item.signal}=${item.value}`;
  });
}

function applyKeyValues(values) {
  if (!values) return;
  S.keyPoints.forEach((point) => {
    if (!point.el || !(point.name in values)) return;
    const value = values[point.name];
    const cell = point.el.querySelector('.kp-val');
    const text = String(value);
    if (cell.dataset.text === text) return;
    const first = cell.dataset.text === undefined;
    cell.dataset.text = text;
    cell.textContent = text;
    cell.className = 'kp-val ' + (point.fault && value ? 'bad' : (value ? 'on' : 'zero'));
    if (!first) {
      point.el.classList.remove('flash');
      void point.el.offsetWidth;
      point.el.classList.add('flash');
    }
  });
}

/* ---------------------------------------------------------------- 软元件表 */
function maxRefs() {
  return S.devices.reduce((acc, item) => Math.max(acc, item.refs), 1);
}

function filteredDevices() {
  const { area, q, onlyChanged } = S.filter;
  const keyword = q.trim().toUpperCase();
  const now = Date.now();
  return S.devices.filter((item) => {
    if (area && item.area !== area) return false;
    if (keyword && !(item.name.includes(keyword) || (item.note || '').toUpperCase().includes(keyword))) return false;
    if (onlyChanged && !(S.changedAt[item.name] && now - S.changedAt[item.name] < 4000)) return false;
    return true;
  });
}

function renderDevices() {
  const all = filteredDevices();
  const rows = S.showAll ? all : all.slice(0, LIMIT);
  const top = maxRefs();
  const body = $('deviceBody');

  body.innerHTML = rows.map((item) => `
    <tr data-name="${item.name}" data-kind="${item.kind}">
      <td><span class="dev-name">${item.name}</span></td>
      <td><span class="dev-value zero">–</span></td>
      <td>
        <div class="bar">
          <div class="bar-track"><div class="bar-fill" style="width:${Math.max(3, (item.refs / top) * 100)}%"></div></div>
          <span class="bar-num">${item.refs}</span>
        </div>
      </td>
      <td><span class="rw">${item.reads} / ${item.writes}</span></td>
      <td><span class="note">${esc(item.note || '')}</span></td>
    </tr>`).join('');

  S.rendered = rows.map((item) => item.name);
  S.rows = {};
  body.querySelectorAll('tr').forEach((row) => { S.rows[row.dataset.name] = row; });
  S.shown = {};                                   // 重新渲染后强制重写一次值
  paintFromCache();
  S.deviceCountText = `显示 ${rows.length} / 匹配 ${all.length} / 共 ${S.devices.length} 个软元件`;
  $('deviceCount').textContent = S.deviceCountText;
  $('showAllBtn').style.display = all.length > LIMIT ? '' : 'none';
  $('showAllBtn').textContent = S.showAll ? '收起' : `显示全部 ${all.length} 个`;
}

function paintFromCache() {
  Object.entries(S.rows || {}).forEach(([name, row]) => {
    if (!(name in S.values)) return;
    const value = S.values[name];
    const isBit = row.dataset.kind === 'bit';
    const cell = row.querySelector('.dev-value');
    S.shown[name] = String(value);
    cell.textContent = String(value);
    cell.className = 'dev-value ' + (isBit ? (value ? 'on' : 'zero') : (value ? 'num' : 'zero'));
  });
}

function applyValues(values) {
  if (!values) return;
  for (const [name, value] of Object.entries(values)) {
    const previous = S.values[name];
    S.values[name] = value;
    const row = (S.rows || {})[name];
    if (!row) continue;
    if (previous !== undefined && previous !== value) {
      S.changedAt[name] = Date.now();
      row.classList.remove('flash');
      void row.offsetWidth;                       // 重启动画
      row.classList.add('flash');
    }
    const cell = row.querySelector('.dev-value');
    const isBit = row.dataset.kind === 'bit';
    const text = String(value);
    if (S.shown[name] === text) continue;
    S.shown[name] = text;
    cell.textContent = text;
    cell.className = 'dev-value ' + (isBit ? (value ? 'on' : 'zero') : (value ? 'num' : 'zero'));
  }
}

/* ---------------------------------------------------------------- MQTT */
function resolveTopic(template) {
  const boxNo = S.boxNo || '{boxNo}';
  return String(template || '').replace('{boxNo}', boxNo);
}

function setupTopics() {
  const topics = (S.boot.box && S.boot.box.topics) || {};
  const list = $('topicList');
  const seen = new Set();
  Object.values(topics).forEach((template) => seen.add(resolveTopic(template)));
  (S.boot.topicsSeen || []).forEach((topic) => seen.add(topic));
  list.innerHTML = [...seen].map((topic) => `<option value="${esc(topic)}"></option>`).join('');
  $('pubTopic').value = resolveTopic(topics.command || 'flexem/{boxNo}/command');
}

function setPayload(payload) {
  $('pubPayload').value = typeof payload === 'string' ? payload : JSON.stringify(payload);
}

function applyMachineTexts(data) {
  const sim = data.sim || {};
  const services = data.services || {};
  const step = sim.stepDevice || '工序号';

  $('kpiStepLabel').textContent = sim.stepDevice ? `当前工序 ${step}` : '当前工序';
  $('flowSub').textContent = sim.stepDevice
    ? `按 ${step} 推进；机构动作靠现场物理模型补全`
    : '该机型未定义工序软元件';
  $('deviceSearch').placeholder = sim.stepDevice
    ? `搜软元件 / 说明（如 ${step}）` : '搜软元件 / 说明';
  if (services.reset || services.start) {
    $('forceDevice').placeholder = services.reset || services.start;
  }

  const bits = [];
  if (services.start) bits.push(`启动只写 ${services.start}`);
  if (services.stop) bits.push(`停止只写 ${services.stop}`);
  if (services.clearStop && services.reset) {
    bits.push(`复位先写 ${services.clearStop}=0，隔 1 秒再写 ${services.reset}=1`);
  } else if (services.reset) {
    bits.push(`复位写 ${services.reset}=1`);
  }
  $('demoHint').textContent = bits.length
    ? `命令按钮对应盒子的三条命令：${bits.join('；')}。`
      + '前置检查各自独立，不通过时启动会被盒子拒绝（看报文里的 commandreply）。'
    : '该机型未定义命令点位（services）；盒子在线时仍可经 MQTT 下发命令。';

  const ready = sim.readySignal || '';
  $('publishNote').textContent = ready
    ? `启动前先复位：刚上电 PLC 不在就绪步；复位进入就绪后 ${ready} 需要一段确认时间才会变 1，此时启动才通过。`
    : '启动前请先满足左侧前置检查；不通过时盒子会拒绝并在 commandreply 里给出原因。';
}

function presetsFor(sim) {
  // 命令名与盒子的服务选择器一致（"Manual start" / "stop" / "reset"）；
  // 查询哪个软元件取机型的 step_device，不写死 D142。
  const step = (sim && sim.stepDevice) || 'D142';
  return [
    { label: '启动', payload: { 'Manual start': 1 } },
    { label: '停止', payload: { stop: 1 } },
    { label: '复位', payload: { reset: 1 } },
    { label: `查询 ${step}`, payload: { [step]: null }, channel: 'read' },
    { label: '重启盒子', payload: { reboot: 1 }, channel: 'reboot' },
  ];
}

function setupPresets() {
  const topics = (S.boot.box && S.boot.box.topics) || {};
  S.presets = presetsFor(S.boot.sim);
  $('presets').innerHTML = S.presets.map((preset, index) =>
    `<button data-index="${index}">${preset.label}</button>`).join('');
  $('presets').querySelectorAll('button').forEach((button) => {
    button.addEventListener('click', () => {
      const preset = S.presets[Number(button.dataset.index)];
      const template = topics[preset.channel || 'command'];
      if (template) $('pubTopic').value = resolveTopic(template);
      setPayload(preset.payload);
    });
  });
}

function messageNode(message) {
  const up = message.direction === 'up';
  const node = document.createElement('div');
  node.className = 'msg ' + (up ? 'up' : 'down');
  node.dataset.topic = message.topic;
  node.dataset.payload = message.payload;
  node.dataset.direction = message.direction;
  node.innerHTML = `
    <div class="msg-top">
      <span class="msg-dir">${up ? '↑ 上行' : '↓ 下发'}</span>
      <span class="msg-topic">${esc(message.topic)}</span>
      <span class="msg-time">${fmtTime(message.ts)}</span>
    </div>
    <div class="msg-body">${esc(message.payload)}</div>`;
  node.addEventListener('click', () => node.classList.toggle('open'));
  return node;
}

function applyMessageFilter() {
  const container = $('messages');
  const keyword = S.mqtt.q.trim().toLowerCase();
  container.querySelectorAll('.msg').forEach((node) => {
    const byDirection = !S.mqtt.filter || node.dataset.direction === S.mqtt.filter;
    const byKeyword = !keyword
      || node.dataset.topic.toLowerCase().includes(keyword)
      || node.dataset.payload.toLowerCase().includes(keyword);
    node.style.display = byDirection && byKeyword ? '' : 'none';
  });
}

function appendMessages(messages) {
  if (!messages.length) return;
  const container = $('messages');
  const placeholder = container.querySelector('.empty');
  if (placeholder) placeholder.remove();
  const nearBottom = container.scrollHeight - container.scrollTop - container.clientHeight < 70;
  messages.forEach((message) => {
    S.mqtt.lastId = Math.max(S.mqtt.lastId, message.id);
    container.appendChild(messageNode(message));
    S.mqtt.items += 1;
  });
  while (container.querySelectorAll('.msg').length > MAX_MESSAGES) {
    container.querySelector('.msg').remove();
  }
  applyMessageFilter();
  $('msgCount').textContent = String(S.mqtt.items);
  if (nearBottom) container.scrollTop = container.scrollHeight;
}

async function pollMqtt() {
  const data = await api(`/api/mqtt?since=${S.mqtt.lastId}&limit=300`);
  appendMessages(data.messages || []);
  const stats = data.stats || {};
  $('brokerBadge').textContent = stats.running ? '运行中' : '未启动';
  $('brokerBadge').className = 'badge' + (stats.running ? ' live' : '');
  const hasSubscribed = (data.clients || []).some((client) => (client.subscriptions || []).length > 0);
  const lastUp = (data.stats && data.stats.lastUpAt) || 0;
  const boxOnline = hasSubscribed || (lastUp > 0 && (Date.now() / 1000 - lastUp) < 10);
  $('brokerLine').textContent = `${stats.host || '0.0.0.0'}:${stats.port || 1883} · ${stats.clients || 0} 客户端 · ${stats.subscriptions || 0} 订阅 · 盒子${boxOnline ? '已连接' : '未连接'}`;
  $('factBroker').textContent = `${stats.host || '0.0.0.0'}:${stats.port || 1883}`;

  // 主题输入提示：把新出现的主题补进 datalist（变了才重建，否则会打断正在输入的补全）
  const subscribed = (data.topics && data.topics.subscribed) || [];
  const options = new Set([...$('topicList').querySelectorAll('option')].map((item) => item.value));
  const before = options.size;
  subscribed.forEach((item) => options.add(item.filter));
  if (options.size !== before) {
    $('topicList').innerHTML = [...options].map((topic) => `<option value="${esc(topic)}"></option>`).join('');
  }
}

async function publish() {
  const topic = $('pubTopic').value.trim();
  const payload = $('pubPayload').value;
  if (!topic) { toast('主题不能为空', true); return; }
  $('pubBtn').disabled = true;
  try {
    const data = await api('/api/mqtt/publish', { topic, payload, qos: Number($('pubQos').value) });
    if (!data.ok) { toast(data.error || '下发失败', true); return; }
    $('pubResult').textContent = `送达 ${data.delivered} 个订阅者`;
    if (data.delivered) {
      toast(`已下发到 ${data.delivered} 个订阅者`);
    } else {
      toast('没有订阅者：盒子主程序没连上来（要用 --main 启动才有人收命令）', true);
    }
    await pollMqtt();
  } finally {
    $('pubBtn').disabled = false;
  }
}

/* ---------------------------------------------------------------- 状态刷新 */
function applyState(state) {
  const scan = state.scan || {};
  const step = state.step || {};
  const fault = state.fault || {};
  const serial = state.serial || {};
  const broker = state.broker || {};
  const main = state.main || {};

  applyValues(state.values);
  applyKeyValues(state.values);
  renderChecks(state.preconditions);

  const now = Date.now();
  const recent = Object.values(S.changedAt).filter((stamp) => now - stamp < 2000).length;
  if (S.deviceCountText) $('deviceCount').textContent = `${S.deviceCountText} · 近 2 秒变化 ${recent} 个`;

  // "只看变化"是动态筛选：变化集合变了就重排一次表格（否则新变化的点永远不会出现）
  if (S.filter.onlyChanged) {
    const names = filteredDevices().slice(0, S.showAll ? 100000 : LIMIT).map((item) => item.name);
    if (names.join(',') !== S.rendered.join(',')) renderDevices();
  }

  $('scanRate').textContent = Math.round(scan.rate || 0).toLocaleString();
  $('uptime').textContent = fmtDuration(scan.uptime);
  $('kpiStep').textContent = step.value == null ? '–' : step.value;
  $('kpiStepName').textContent = step.title || '未开始';
  $('kpiSerial').textContent = (serial.reads || 0).toLocaleString();
  $('kpiWrites').textContent = (serial.writes || 0).toLocaleString();
  $('kpiPty').textContent = serial.link || '未启用串口桩';
  $('factPty').textContent = serial.link || '未启用';
  $('kpiClients').textContent = broker.clients || 0;
  $('kpiBroker').textContent = broker.running
    ? `监听 ${broker.host || '0.0.0.0'}:${broker.port}` : (broker.error || '未启用');

  const faultCode = fault.code || 0;
  $('kpiFault').textContent = faultCode;
  $('kpiFaultName').textContent = faultCode ? (fault.name || '未收录的码值') : '正常';
  $('kpiFaultName').className = faultCode ? 'danger' : 'ok';
  $('kpiFaultMeta').textContent = faultCode ? (fault.device || 'D2') + ' 有码' : '无故障上报';

  // 盒子在不在线，以 broker 上的连接/上行判断，而不是只看"本看板托管的主程序"：
  // 用 [R] 在另一个终端起的 main 也会连到同一个 broker，那不算"未启动"。
  const clientList = broker.clientList || [];
  const mqttClients = clientList.length;
  const hasSubscription = clientList.some((client) => (client.subscriptions || []).length > 0);
  const lastUpAt = broker.lastUpAt || 0;
  const recentUp = lastUpAt > 0 && (Date.now() / 1000 - lastUpAt) < 10;
  const boxSeen = hasSubscription || recentUp;

  if (main.enabled) {
    $('kpiMain').textContent = main.running ? '运行中' : '已退出';
    $('kpiMainMeta').textContent = main.running ? `pid ${main.pid}` : `退出码 ${main.returncode}`;
    $('kpiMainMeta').className = 'kpi-s' + (main.running ? '' : ' danger');
  } else if (mqttClients > 0) {
    $('kpiMain').textContent = '外部连接';
    $('kpiMainMeta').textContent = hasSubscription
      ? `${mqttClients} 个 MQTT 客户端（不是本看板起的）`
      : `${mqttClients} 个客户端，但没订阅命令主题`;
    $('kpiMainMeta').className = 'kpi-s';
  } else if (recentUp) {
    $('kpiMain').textContent = '刚断开';
    $('kpiMainMeta').textContent = '最近 10 秒还有上行，连接已断';
    $('kpiMainMeta').className = 'kpi-s';
  } else {
    $('kpiMain').textContent = '未启动';
    $('kpiMainMeta').textContent = '加 --main 托管，或用 [R] 直跑（会连到本 broker）';
  }
  $('factMainLog').textContent = main.log || '–';

  // 没有盒子程序时把话说死：这里下发的命令没人收，不是"盒子离线"
  const banner = $('banner');
  if (!main.enabled) {
    if (boxSeen && hasSubscription) {
      banner.hidden = true;                       // 外部起的盒子程序已连上，别再报"没有接收方"
    } else if (boxSeen) {
      banner.hidden = false;
      banner.textContent = '检测到外部程序在向本 broker 上报，但它没有订阅命令主题，下发命令可能收不到。';
    } else {
      banner.hidden = false;
      banner.textContent = '本看板没有接盒子主程序（启动时没加 --main）：这里下发的 MQTT 命令没有接收方，'
        + '只会记录在下面的报文列表里。可以用 [R] 直跑 main（会连到本 broker），或 python3 start.py --main <路径>。';
    }
  } else if (!main.running) {
    banner.hidden = false;
    banner.textContent = `盒子主程序已退出（退出码 ${main.returncode}），命令没人接收。看 main.log 查原因。`;
  } else if (broker && broker.running === false) {
    banner.hidden = false;
    banner.textContent = `MQTT 测试服务器没起来：${broker.error || '端口被占用？'}。`;
  } else {
    banner.hidden = true;
  }
  $('factMainLog').title = main.log || '';

  if (state.box) {
    S.boxNo = state.box.boxNo || S.boxNo;
  }

  // 暂停态
  S.paused = !!scan.paused;
  $('pauseBtn').textContent = S.paused ? '继续' : '暂停';
  $('alive').className = 'pulse' + (S.paused ? ' paused' : '');
  S.speed = scan.timeScale || S.speed;
  document.querySelectorAll('#speedSeg button').forEach((button) => {
    button.classList.toggle('on', Number(button.dataset.speed) === Math.round(S.speed));
  });

  updateFlow(step);
  renderTrail(step.trail || []);
  renderOverrides(state.overrides || []);
}

async function pollState() {
  const watch = [...new Set([...S.keyPoints.map((point) => point.name), ...S.rendered])].slice(0, 330);
  const data = await api('/api/state', { watch });
  applyState(data);
}

function renderOverrides(overrides) {
  const container = $('overrides');
  if (!overrides.length) {
    if (!container.querySelector('.empty-inline')) {
      container.innerHTML = '<span class="empty-inline">没有生效中的强制值</span>';
    }
    return;
  }
  container.innerHTML = overrides.map((item) =>
    `<span class="override">${item.name}=${item.value}<button data-device="${item.name}" title="解除">×</button></span>`).join('');
  container.querySelectorAll('button').forEach((button) => {
    button.addEventListener('click', async () => {
      await api('/api/control', { action: 'clear-overrides', device: button.dataset.device });
      toast(`已解除 ${button.dataset.device}`);
    });
  });
}

/* ---------------------------------------------------------------- 事件 */
function bindEvents() {
  $('pauseBtn').addEventListener('click', async () => {
    await api('/api/control', { action: S.paused ? 'resume' : 'pause' });
    if (S.paused) toast('已继续');
  });
  $('stepBtn').addEventListener('click', async () => {
    await api('/api/control', { action: 'step' });
  });
  document.querySelectorAll('#speedSeg button').forEach((button) => {
    button.addEventListener('click', async () => {
      const speed = Number(button.dataset.speed);
      await api('/api/control', { action: 'speed', timeScale: speed });
      toast(`虚拟倍速 ${speed}×（扫描周期 ${Math.max(1, Math.round(50 / speed))} ms）`);
    });
  });
  $('clearTrail').addEventListener('click', async () => {
    await api('/api/control', { action: 'clear-trail' });
    S.trailLen = 0;
    $('trail').innerHTML = '';
  });

  $('areaSeg').addEventListener('click', (event) => {
    const button = event.target.closest('button');
    if (!button) return;
    $('areaSeg').querySelectorAll('button').forEach((item) => item.classList.toggle('on', item === button));
    S.filter.area = button.dataset.area;
    renderDevices();
    pollState().catch(() => {});
  });
  $('deviceSearch').addEventListener('input', (event) => {
    S.filter.q = event.target.value;
    renderDevices();
    pollState().catch(() => {});
  });
  $('onlyChanged').addEventListener('change', (event) => {
    S.filter.onlyChanged = event.target.checked;
    renderDevices();
    pollState().catch(() => {});
  });
  $('showAllBtn').addEventListener('click', () => {
    S.showAll = !S.showAll;
    renderDevices();
    pollState().catch(() => {});
  });
  $('deviceBody').addEventListener('click', (event) => {
    const row = event.target.closest('tr');
    if (!row) return;
    $('forceDevice').value = row.dataset.name;
    $('forceValue').focus();
  });

  $('pubBtn').addEventListener('click', publish);
  $('pubTopic').addEventListener('keydown', (event) => {
    if (event.key === 'Enter') publish();
  });
  $('msgFilter').addEventListener('click', (event) => {
    const button = event.target.closest('button');
    if (!button) return;
    $('msgFilter').querySelectorAll('button').forEach((item) => item.classList.toggle('on', item === button));
    S.mqtt.filter = button.dataset.filter;
    applyMessageFilter();
  });
  $('msgSearch').addEventListener('input', (event) => {
    S.mqtt.q = event.target.value;
    applyMessageFilter();
  });

  $('forceBtn').addEventListener('click', async () => {
    const device = $('forceDevice').value.trim().toUpperCase();
    const value = Number($('forceValue').value);
    if (!device) { toast('先填软元件名', true); return; }
    const data = await api('/api/control', { action: 'override', device, value });
    if (!data.ok) { toast(data.error || '写入失败', true); return; }
    toast(`${device} 强制为 ${value}`);
  });
  $('clearOverrides').addEventListener('click', async () => {
    await api('/api/control', { action: 'clear-overrides' });
    toast('已解除全部强制值');
  });

  $('demoStart').addEventListener('click', async () => {
    const data = await api('/api/control', { action: 'demo-start' });
    const start = S.boot.services && S.boot.services.start;
    if (data.ok === false) { toast(data.error || '命令未下发', true); return; }
    if (data.via === 'mqtt') toast('已下发启动命令（经盒子准入）');
    else if (!start) toast('该机型没有定义启动点位，命令未下发', true);
    else toast(`无盒子在跑：直接写 ${start}（绕过准入）`);
  });
  $('demoStop').addEventListener('click', async () => {
    const data = await api('/api/control', { action: 'demo-stop' });
    const stop = (S.boot.services && S.boot.services.stop) || '停止位';
    if (data.ok === false) { toast(data.error || '命令未下发', true); return; }
    toast(data.via === 'mqtt' ? '已下发停止命令（经盒子）' : `无盒子在跑：直接写 ${stop}=true`);
  });
  $('demoReset').addEventListener('click', async () => {
    const data = await api('/api/control', { action: 'demo-reset' });
    if (data.ok === false) { toast(data.error || '命令未下发', true); return; }
    const svc = S.boot.services || {};
    const seq = svc.clearStop && svc.reset
      ? `${svc.clearStop}=0 → 1s → ${svc.reset}=1`
      : `${svc.reset || '复位位'}=1`;
    toast(data.via === 'mqtt' ? `已下发复位命令（经盒子：${seq}）` : `无盒子在跑：直接写 ${seq}`);
  });

  // 后台标签页会被浏览器降频，切回来先立刻拉一次，别让人对着旧值发呆
  document.addEventListener('visibilitychange', () => {
    if (document.hidden) return;
    pollState().catch(() => {});
    pollMqtt().catch(() => {});
  });
}

/* ---------------------------------------------------------------- 启动 */
async function boot() {
  const data = await api('/api/bootstrap');
  S.boot = data;
  S.devices = data.devices || [];
  S.flow = data.flow || [];
  S.boxNo = (data.box && data.box.boxNo) || '';

  $('simTitle').textContent = data.sim.title;
  $('ladderName').textContent = data.sim.ladder;
  $('factRunDir').textContent = data.runDir || '–';
  $('factPid').textContent = data.pid || '–';
  $('factRunDir').title = data.runDir || '';
  document.title = `${data.sim.title} · PLC 模拟器看板`;
  applyMachineTexts(data);

  buildFlow();
  buildKeyPoints(data.keyPoints || []);
  setupTopics();
  setupPresets();
  renderDevices();
  bindEvents();

  await Promise.all([pollState(), pollMqtt()]);
  setInterval(() => pollState().catch(() => {}), 320);
  setInterval(() => pollMqtt().catch(() => {}), 650);
}

boot().catch((error) => {
  document.body.insertAdjacentHTML('afterbegin',
    `<div style="padding:16px;color:#f2545b;font-family:monospace">看板加载失败：${esc(error.message)}<br>检查模拟器是否还在跑。</div>`);
});
