'use strict';

var S = {
  status: null,
  config: null,
  custom: [],
  deleted: {},
  defaults: [],
  page: 'overview',
  pollTimer: null,
  logs: { date: '', service: 'all', level: '', keyword: '', maintenanceOnly: false, auto: false, timer: null, loading: false }
};

var PAGE_TITLES = {
  overview: '概览',
  mqtt: 'MQTT 主题',
  points: '监控点',
  logs: '日志',
  apply: '应用 / 历史',
  settings: '设置'
};

var LOG_SERVICE_LABELS = {
  all: '全部',
  lobby: '大厅/状态机',
  washcar: '洗车服务',
  mqtt: 'MQTT/命令',
  plc: 'PLC/采集',
  detection: 'AI/摄像头',
  config: '配置台',
  auth: '登录鉴权',
  system: '系统/其他'
};

var LOG_SERVICE_ORDER = ['all', 'lobby', 'washcar', 'mqtt', 'plc', 'detection', 'config', 'auth', 'system'];

var TOPIC_LABELS = {
  monitor: '变化上报',
  readdata: '立即读取响应',
  read: '立即读取（订阅）',
  pushalarm: '故障上报',
  command: '命令下发（订阅）',
  commandreply: '命令响应',
  reboot: '重启（订阅）'
};

var TOPIC_DIRS = {
  monitor: '发布', readdata: '发布', pushalarm: '发布', commandreply: '发布',
  read: '订阅', command: '订阅', reboot: '订阅'
};

// 重启后 session 失效, 页面会退回登录页。这里留个一次性标记,
// 让登录页说清"程序刚重启过", 而不是甩一句"登录已过期"。
var RESTART_NOTICE_KEY = 'washcar.restarted';

// "只看维修相关" 是 OR 检索: 后端 keyword 只吃单个子串,
// 维修相关的日志一半英文(maintain)一半中文(维修模式), 分两次查再合并去重。
// 中文词用"维修模式"而不是"维修": 状态机每条判定日志都带"维修=是/否",
// 用"维修"会把当天所有状态机日志都捞进来, 等于没筛。
var MAINTENANCE_KEYWORDS = ['maintain', '维修模式'];

function $(id) { return document.getElementById(id); }

function esc(value) {
  return String(value == null ? '' : value)
    .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;').replace(/'/g, '&#39;');
}

function toast(message, isError) {
  var el = $('toast');
  el.textContent = message;
  el.className = 'toast' + (isError ? ' error' : '');
  el.classList.remove('hidden');
  clearTimeout(el._timer);
  el._timer = setTimeout(function () { el.classList.add('hidden'); }, 3600);
}

function api(path, body) {
  return fetch(path, {
    method: 'POST',
    cache: 'no-store',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body || {})
  }).then(function (res) {
    return res.json().catch(function () { return {}; }).then(function (data) {
      if (res.status === 401) {
        var error = new Error(data.error || '请先登录');
        error.auth = path !== '/api/login';
        if (error.auth) showLogin('登录已过期，请重新登录');
        throw error;
      }
      if (!res.ok || data.ok === false) {
        throw new Error(data.error || ('请求失败 HTTP ' + res.status));
      }
      return data;
    });
  });
}

function showLogin(message) {
  stopLogAuto();
  $('login').classList.remove('hidden');
  $('app').classList.add('hidden');
  // 重启提示优先: 重启后 cookie 里的 session 失效, 直接显示"登录已过期"
  // 会让人以为程序挂了, 实际只是重启完了。
  var notice = consumeRestartNotice();
  if ($('login-error')) $('login-error').textContent = notice || (message === undefined ? '' : message);
  window.scrollTo(0, 0);
}

// 只消费一次, 之后的正常登录不受影响。
function consumeRestartNotice() {
  try {
    if (sessionStorage.getItem(RESTART_NOTICE_KEY)) {
      sessionStorage.removeItem(RESTART_NOTICE_KEY);
      return '程序已重启，请重新登录';
    }
  } catch (err) {
    // 存储被禁用时忽略, 不影响登录
  }
  return '';
}

function showApp() {
  $('login').classList.add('hidden');
  $('app').classList.remove('hidden');
}

function normalizeReport(report) {
  if (Array.isArray(report)) return report.slice();
  if (report === 'property') return ['monitor', 'readdata'];
  if (report === 'event') return ['pushalarm'];
  if (report === 'monitor' || report === 'readdata' || report === 'pushalarm') return [report];
  return [];
}

function reportText(report) {
  var list = normalizeReport(report);
  if (!list.length) return '<span class="badge">仅本地</span>';
  var map = { monitor: '变化', readdata: '读取', pushalarm: '故障' };
  return list.map(function (item) {
    return '<span class="badge blue">' + esc(map[item] || item) + '</span>';
  }).join(' ');
}

function signalValue(point) {
  if (!S.status || !S.status.plc || !S.status.plc.points) return '-';
  var value = S.status.plc.points[point];
  return value === undefined ? '-' : value;
}

function findSignal(name) {
  if (!S.config || !S.config.effectiveMachine) return null;
  var signals = S.config.effectiveMachine.signals || [];
  for (var i = 0; i < signals.length; i++) if (signals[i].name === name) return signals[i];
  return null;
}

function isDefaultSignal(name) {
  return S.defaults.indexOf(name) >= 0;
}

function loadAll() {
  return api('/api/session', {}).then(function (session) {
    if (!session.authenticated) { showLogin(); return; }
    return Promise.all([api('/api/status', {}), api('/api/config', {})]).then(function (results) {
      S.status = results[0];
      S.config = results[1];
      S.defaults = S.config.defaultSignalNames || [];
      S.deleted = {};
      S.custom = (S.config.customSignals || []).map(function (signal) {
        var copy = Object.assign({}, signal);
        copy.report = normalizeReport(copy.report);
        return copy;
      });
      showApp();
      clearIssues();
      renderAll();
      loadHistory();
      var hashPage = (window.location.hash || '').replace('#', '');
      if (PAGE_TITLES[hashPage]) setPage(hashPage);
      else if (S.page === 'logs') loadLogs();
    });
  }).catch(function (err) {
    showLogin(err.auth ? undefined : (err.message || ''));
  });
}

function renderAll() {
  if (!S.status || !S.config) return;
  $('side-boxno').textContent = S.status.boxNo || '-';
  $('top-sub').textContent = (S.status.boxType || '-') + ' · 配置版本 ' + String(S.status.revision || '-').slice(0, 8);
  var mqttOn = S.status.mqtt && S.status.mqtt.connected;
  var plcOn = S.status.plc && S.status.plc.connected;
  $('dot-mqtt').className = 'dot ' + (mqttOn ? 'on' : 'off');
  $('dot-plc').className = 'dot ' + (plcOn ? 'on' : 'off');
  $('side-dot-mqtt').className = 'dot ' + (mqttOn ? 'on' : 'off');
  $('side-dot-plc').className = 'dot ' + (plcOn ? 'on' : 'off');
  $('side-status-mqtt').textContent = mqttOn ? 'MQTT 在线' : 'MQTT 离线';
  $('side-status-plc').textContent = plcOn ? 'PLC 在线' : 'PLC 离线';
  $('side-status-time').textContent = '更新 ' + new Date().toLocaleTimeString();
  renderOverview();
  renderMqtt();
  renderPoints();
  renderApply();
  renderSettings();
}

function renderOverview() {
  var status = S.status || {};
  var wash = status.washcar || {};
  var mqttOn = status.mqtt && status.mqtt.connected;
  var plcOn = status.plc && status.plc.connected;
  $('ov-boxno').textContent = status.boxNo || '-';
  $('ov-box-type').textContent = status.boxType || '-';
  $('ov-wash').textContent = wash.washing ? '洗车中' : '空闲';
  $('ov-step').textContent = '当前步骤 ' + (wash.currentStep === undefined ? '-' : wash.currentStep) +
    (wash.faultCode ? ' · 故障码 ' + wash.faultCode : '');
  renderAdmission(wash);
  $('ov-rev').textContent = String(status.revision || '-').slice(0, 12);
  $('ov-degraded').textContent = status.degraded ? '已回退 lastgood，请检查配置' : '正常';
  $('ov-degraded').className = 'card-foot' + (status.degraded ? ' error' : '');
  $('ov-config').textContent = status.degraded ? '回退' : '正常';
  $('ov-conn').textContent = (mqttOn ? 'MQTT 在线' : 'MQTT 离线') + ' / ' + (plcOn ? 'PLC 在线' : 'PLC 离线');
  $('ov-broker').textContent = status.mqtt ? (status.mqtt.brokerIp + ':' + status.mqtt.port) : '-';
  $('ov-mqtt').textContent = mqttOn ? '在线' : '离线';
  $('ov-plc').textContent = plcOn ? '在线' : '离线';
  $('ov-plc-foot').textContent = plcOn ? '采集缓存正常' : '等待 PLC 数据';

  var topicRows = (status.topics || []).map(function (item) {
    return '<tr><td>' + esc(TOPIC_LABELS[item.key] || item.key) + '</td><td class="mono">' + esc(item.topic) + '</td></tr>';
  }).join('');
  $('ov-topics').innerHTML = topicRows || '<tr><td colspan="2" class="muted">暂无</td></tr>';

  var signals = (S.config.effectiveMachine && S.config.effectiveMachine.signals) || [];
  $('ov-point-count').textContent = signals.length;
  $('ov-point-hint').textContent = signals.length + ' 个点位';
  var pointRows = signals.map(function (signal) {
    return '<tr><td>' + esc(signal.comment || signal.name) + '</td><td class="mono">' + esc(signal.point) +
      '</td><td>' + esc(signalValue(signal.point)) + '</td><td>' + reportText(signal.report) + '</td></tr>';
  }).join('');
  $('ov-points').innerHTML = pointRows || '<tr><td colspan="4" class="muted">暂无点位</td></tr>';
  renderMaintenance();
}

// 启动准入的判定结论(允许启动 / 需复位 / 挡住启动)目前只存在于 washcar.message 文本里,
// 后端没给结构化字段。解析不出来就不显示 —— 宁可空着, 也不给现场一个猜出来的状态。
function renderAdmission(wash) {
  var el = $('ov-admission');
  if (!el) return;
  var text = String((wash && wash.message) || '');
  var start = text.indexOf('状态=');
  if (start < 0) {
    el.textContent = '';
    return;
  }
  var raw = text.slice(start);
  var allow = /允许启动=(是|否)/.exec(raw);
  var reset = /需复位=(是|否)/.exec(raw);
  var reason = /挡住启动=(.*?)(?:\s+故障码=|$)/.exec(raw);
  var parts = [];
  if (allow) parts.push(allow[1] === '是' ? '允许启动' : '不允许启动');
  if (reason && reason[1].trim()) parts.push('原因：' + reason[1].trim());
  if (reset && reset[1] === '是') parts.push('需先复位');
  el.textContent = parts.length ? parts.join(' · ') : raw;
}

// 维修模式面板。盒子没返回 maintaining 字段就整块隐藏 —— 宁可没有这个入口,
// 也不能显示一个猜出来的状态(现场会照着它做操作)。
function renderMaintenance() {
  var panel = $('maint-panel');
  var wash = (S.status && S.status.washcar) || {};
  if (typeof wash.maintaining !== 'boolean') {
    panel.classList.add('hidden');
    return;
  }
  panel.classList.remove('hidden');
  var on = wash.maintaining;
  var badge = $('maint-badge');
  badge.textContent = on ? '维修中' : '正常';
  badge.className = 'badge ' + (on ? 'warn' : 'ok');
  $('maint-toggle').textContent = on ? '退出维修模式' : '进入维修模式';
  $('maint-hint').textContent = on
    ? '当前是维修模式：云端下发的启动、复位命令会被盒子拒绝，本地单点控制（强制点位）允许执行。退出后云端命令恢复生效。'
    : '当前不是维修模式：云端控制命令正常生效，但本地单点控制（强制点位）会被拒绝，需要先进入维修模式。';
}

function detectTopicStyle(topics) {
  var sample = topics.monitor || topics.command || topics.read || '';
  sample = sample.replace('{boxNo}', 'BOX');
  var sep = sample.indexOf('.') >= 0 ? '.' : '/';
  var prefix = sample.split(sep)[0] || 'flexem';
  return { prefix: prefix, sep: sep };
}

function generateTopicInputs() {
  var prefix = $('topic-prefix').value.trim() || 'flexem';
  var sep = $('topic-sep').value || '/';
  Array.prototype.forEach.call(document.querySelectorAll('.topic-input'), function (input) {
    input.value = prefix + sep + '{boxNo}' + sep + input.getAttribute('data-key');
  });
}

function renderMqtt() {
  var raw = (S.config && S.config.raw) || {};
  var mqtt = raw.mqtt || {};
  var box = raw.box || {};
  $('cfg-boxno').value = box.boxNo || '';
  $('cfg-boxtype').value = box.boxType || '';
  $('cfg-broker').value = mqtt.brokerIp || '';
  $('cfg-port').value = mqtt.port || 1883;
  $('cfg-user').value = mqtt.username || '';
  $('cfg-pass').value = '';
  $('cfg-pass-hint').textContent = mqtt.passwordSet
    ? '密码已设置；留空表示不修改。'
    : '当前无密码；填写后保存。';

  var topics = mqtt.topics || {};
  var style = detectTopicStyle(topics);
  $('topic-prefix').value = style.prefix;
  $('topic-sep').value = style.sep;
  var keys = ['monitor', 'readdata', 'read', 'pushalarm', 'command', 'commandreply', 'reboot'];
  var rows = keys.map(function (key) {
    var value = topics[key] || ('flexem/{boxNo}/' + key);
    return '<tr><td>' + esc(TOPIC_LABELS[key] || key) + '</td><td>' + esc(TOPIC_DIRS[key] || '') +
      '</td><td><input class="topic-input" data-key="' + key + '" value="' + esc(value) + '"></td></tr>';
  }).join('');
  $('topic-rows').innerHTML = rows;
}

function renderPoints() {
  var effective = (S.config && S.config.effectiveMachine && S.config.effectiveMachine.signals) || [];
  effective = effective.filter(function (signal) {
    return isDefaultSignal(signal.name) || !S.deleted[signal.name];
  });
  var customByName = {};
  S.custom.forEach(function (item) { customByName[item.name] = item; });
  var shown = effective.map(function (signal) {
    if (!isDefaultSignal(signal.name) && customByName[signal.name]) return customByName[signal.name];
    return signal;
  });
  var shownNames = {};
  shown.forEach(function (signal) { shownNames[signal.name] = true; });
  S.custom.forEach(function (item) {
    if (!shownNames[item.name]) shown.push(item);
  });
  var signals = shown;
  var rows = signals.map(function (signal) {
    var isDefault = isDefaultSignal(signal.name);
    var actions = isDefault
      ? '<span class="badge">系统</span>'
      : '<button class="btn ghost small" data-edit="' + esc(signal.name) + '">编辑</button> ' +
        '<button class="btn danger small" data-del="' + esc(signal.name) + '">删除</button>';
    return '<tr><td>' + esc(signal.comment || signal.name) + '</td><td class="mono">' + esc(signal.point) +
      '</td><td>' + esc((signal.point || '').charAt(0) === 'D' ? '字' : '位') +
      '</td><td>' + (normalizeReport(signal.report).indexOf('monitor') >= 0 ? '✓' : '') +
      '</td><td>' + (normalizeReport(signal.report).indexOf('readdata') >= 0 ? '✓' : '') +
      '</td><td>' + (normalizeReport(signal.report).indexOf('pushalarm') >= 0 ? '✓' : '') +
      '</td><td class="mono">' + esc(signal.cloudKey || signal.point) + '</td><td>' + esc(signal.normalValue) +
      '</td><td>' + esc(signalValue(signal.point)) + '</td><td>' + actions + '</td></tr>';
  }).join('');
  $('point-rows').innerHTML = rows || '<tr><td colspan="10" class="muted">暂无点位</td></tr>';

  Array.prototype.forEach.call(document.querySelectorAll('[data-edit]'), function (btn) {
    btn.onclick = function () { openPointModal(btn.getAttribute('data-edit')); };
  });
  Array.prototype.forEach.call(document.querySelectorAll('[data-del]'), function (btn) {
    btn.onclick = function () { deletePoint(btn.getAttribute('data-del')); };
  });
}

function renderApply() {
  $('apply-path').textContent = (S.config && S.config.configPath) || '';
  $('apply-rev').textContent = (S.config && S.config.revision) || '-';
  $('apply-state').textContent = (S.config && S.config.degraded) ? '已回退 lastgood（请检查配置）' : '正常';
}

function renderSettings() {
  $('set-path').textContent = (S.config && S.config.configPath) || '-';
  $('pw-warning').textContent = (S.config && S.config.defaultPassword) ? '当前仍是默认密码，请修改' : '';
  $('pw-warning').className = 'muted' + (S.config && S.config.defaultPassword ? ' error' : '');
}

function loadHistory() {
  if (!S.config) return Promise.resolve();
  return api('/api/config/history', {}).then(function (data) {
    var rows = (data.entries || []).map(function (entry) {
      return '<tr><td class="mono">' + esc(entry.id) + '</td><td>' + esc(entry.size) +
        '</td><td><button class="btn ghost small" data-rollback="' + esc(entry.id) + '">回滚</button></td></tr>';
    }).join('');
    $('history-rows').innerHTML = rows || '<tr><td colspan="3" class="muted">暂无历史</td></tr>';
    Array.prototype.forEach.call(document.querySelectorAll('[data-rollback]'), function (btn) {
      btn.onclick = function () { rollback(btn.getAttribute('data-rollback')); };
    });
  }).catch(function (err) { toast(err.message, true); });
}

function logLevelClass(level) {
  return 'log-level ' + esc(level || 'INFO');
}

function renderLogDates(dates, current) {
  var select = $('log-date');
  if (!dates || !dates.length) {
    select.innerHTML = '<option value="">暂无日志</option>';
    return;
  }
  select.innerHTML = dates.map(function (date) {
    return '<option value="' + esc(date) + '"' + (date === current ? ' selected' : '') + '>' + esc(date) + '</option>';
  }).join('');
}

function renderLogServices(counts) {
  counts = counts || {};
  var html = LOG_SERVICE_ORDER.map(function (key) {
    var count = key === 'all' ? Object.keys(counts).reduce(function (sum, k) { return sum + (counts[k] || 0); }, 0) : (counts[key] || 0);
    return '<button class="service-pill' + (S.logs.service === key ? ' active' : '') + '" data-log-service="' + key + '">' +
      esc(LOG_SERVICE_LABELS[key] || key) + '<span class="count">' + count + '</span></button>';
  }).join('');
  $('log-services').innerHTML = html;
  Array.prototype.forEach.call(document.querySelectorAll('[data-log-service]'), function (btn) {
    btn.onclick = function () {
      S.logs.service = btn.getAttribute('data-log-service');
      loadLogs();
    };
  });
}

function renderLogEntries(entries) {
  var view = $('log-view');
  if (!entries || !entries.length) {
    view.innerHTML = '<div class="log-empty">当前筛选条件下没有日志。换一天、换服务或清空搜索再试。</div>';
    return;
  }
  var nearBottom = view.scrollTop + view.clientHeight >= view.scrollHeight - 40;
  view.innerHTML = entries.map(function (entry) {
    return '<div class="log-row level-' + esc(entry.level || 'INFO') + '">' +
      '<div class="log-time">' + esc(entry.time || '-') + '</div>' +
      '<div class="' + logLevelClass(entry.level) + '">' + esc(entry.level || 'INFO') + '</div>' +
      '<div class="log-service" title="' + esc(entry.serviceLabel || entry.service) + '">' + esc(entry.serviceLabel || entry.service) + '</div>' +
      '<div class="log-message">' + esc(entry.message || '') + '</div>' +
      '</div>';
  }).join('');
  if (nearBottom) view.scrollTop = view.scrollHeight;
}

// 两次查询的结果合并: 按行去重, 再按时间排回原顺序。
function mergeLogEntries(results) {
  var seen = Object.create(null);
  var merged = [];
  results.forEach(function (result) {
    var entries = (result && result.entries) || [];
    entries.forEach(function (entry) {
      var key = (entry.time || '') + '|' + (entry.level || '') + '|' +
        (entry.service || '') + '|' + (entry.message || '');
      if (seen[key]) return;
      seen[key] = true;
      merged.push(entry);
    });
  });
  merged.sort(function (a, b) { return String(a.time || '').localeCompare(String(b.time || '')); });
  return merged.length > 300 ? merged.slice(merged.length - 300) : merged;
}

function loadLogs(silent) {
  if (!S.config || S.logs.loading) return Promise.resolve();
  S.logs.loading = true;
  var base = { date: S.logs.date, service: S.logs.service, level: S.logs.level, limit: 300 };
  var keywords = S.logs.maintenanceOnly ? MAINTENANCE_KEYWORDS
    : (S.logs.keyword ? [S.logs.keyword] : ['']);
  var requests = keywords.map(function (keyword) {
    var body = Object.assign({}, base);
    body.keyword = keyword;
    return api('/api/logs', body);
  });
  return Promise.all(requests).then(function (results) {
    var first = results[0] || {};
    var entries = mergeLogEntries(results);
    S.logs.date = first.date || '';
    renderLogDates(first.dates || [], S.logs.date);
    renderLogServices(first.counts || {});
    renderLogEntries(entries);
    $('log-count').textContent = entries.length + ' 条显示';
    var meta = (first.date || '-') + ' · 当前显示 ' + entries.length + ' 条';
    // 两次查询的"当天匹配"会重复计数, 说不出准确总数, 就别给数字。
    if (keywords.length > 1) meta += ' · 维修相关（英文+中文合并）';
    else meta += ' · 当天匹配 ' + (first.total || 0) + ' 条';
    if (first.truncated) meta += ' · 只读取了日志尾部(2MB)';
    if (first.logDir) meta += ' · ' + first.logDir;
    $('log-meta').textContent = meta;
  }).catch(function (err) {
    if (!silent) toast(err.message, true);
    $('log-view').innerHTML = '<div class="log-empty">日志读取失败：' + esc(err.message) + '</div>';
  }).finally(function () {
    S.logs.loading = false;
  });
}

function stopLogTimer() {
  if (S.logs.timer) {
    clearInterval(S.logs.timer);
    S.logs.timer = null;
  }
}

function startLogTimer() {
  stopLogTimer();
  if (!S.logs.auto) return;
  S.logs.timer = setInterval(function () { loadLogs(true); }, 4000);
}

function stopLogAuto() {
  S.logs.auto = false;
  stopLogTimer();
  if ($('log-auto')) $('log-auto').checked = false;
}

function setPage(page) {
  if (!PAGE_TITLES[page]) page = 'overview';
  S.page = page;
  if (window.location.hash !== '#' + page) window.location.hash = page;
  Array.prototype.forEach.call(document.querySelectorAll('.nav-item'), function (item) {
    item.classList.toggle('active', item.getAttribute('data-page') === page);
  });
  Array.prototype.forEach.call(document.querySelectorAll('.page'), function (section) {
    section.classList.add('hidden');
  });
  var target = $('page-' + page);
  if (target) target.classList.remove('hidden');
  $('page-title').textContent = PAGE_TITLES[page] || page;
  clearIssues();
  if (page === 'apply') loadHistory();
  if (page === 'logs') { loadLogs(); startLogTimer(); }
  else stopLogTimer();
}

function showIssues(id, message) {
  var el = $(id);
  if (!el) return;
  if (!message) { el.classList.add('hidden'); el.textContent = ''; return; }
  el.textContent = message;
  el.classList.remove('hidden');
}

function clearIssues() {
  showIssues('mqtt-issues', '');
  showIssues('points-issues', '');
}

function collectMqttPatch() {
  var boxNo = $('cfg-boxno').value.trim();
  if (!boxNo) throw new Error('盒号不能为空');
  var topics = {};
  Array.prototype.forEach.call(document.querySelectorAll('.topic-input'), function (input) {
    topics[input.getAttribute('data-key')] = input.value.trim();
  });
  var mqtt = {
    brokerIp: $('cfg-broker').value.trim(),
    port: parseInt($('cfg-port').value, 10) || 1883,
    username: $('cfg-user').value.trim(),
    topics: topics
  };
  var password = $('cfg-pass').value;
  if (password) mqtt.password = password;
  return { box: { boxNo: boxNo }, mqtt: mqtt };
}

function collectPointPatch() {
  var machine = {};
  machine[S.config.box.boxType] = { signals: S.custom };
  return { machine: machine };
}

function validatePatch(patch, issueId) {
  showIssues(issueId, '');
  return api('/api/config/validate', { patch: patch, expectedRevision: S.config.revision })
    .then(function (data) { return data; })
    .catch(function (err) {
      if (!err.auth) showIssues(issueId, err.message);
      throw err;
    });
}

function applyPatch(patch, issueId) {
  return validatePatch(patch, issueId).then(function () {
    var message = '应用后程序会重启（洗车中会被拒绝）。确认继续？';
    if (S.page === 'mqtt') message += '\n\n注意：云端订阅/发布主题也要同步修改，否则命令会收不到。';
    if (patch.box && patch.box.boxNo && S.status && patch.box.boxNo !== S.status.boxNo) {
      message += '\n\n改盒号会同时改变 MQTT clientId 和主题里的 {boxNo}，云端必须同步。';
    }
    if (!window.confirm(message)) return null;
    return api('/api/config/apply', { patch: patch, expectedRevision: S.config.revision });
  }).then(function (data) {
    if (data && data.ok) showRestart();
    return data;
  }).catch(function (err) {
    if (err.auth) return;
    if (err.message && err.message.indexOf('洗车') >= 0) showIssues(issueId, err.message);
    else toast(err.message, true);
  });
}

function showRestart() {
  $('restart').classList.remove('hidden');
  // 一开始就说清要重新登录, 不然等 30 秒后弹回登录页, 现场会以为程序挂了。
  $('restart-text').textContent = '配置已保存，盒子正在重启（通常 10~30 秒）。重启完成后需要重新登录。';
  try { sessionStorage.setItem(RESTART_NOTICE_KEY, '1'); } catch (err) {}
  var tries = 0;
  var timer = setInterval(function () {
    tries++;
    if (tries > 40) {
      clearInterval(timer);
      $('restart-text').textContent = '等待超时。请检查日志或到“应用 / 历史”回滚。';
      return;
    }
    fetch('/api/health', { cache: 'no-store' }).then(function (res) {
      if (res.ok) {
        clearInterval(timer);
        $('restart-text').textContent = '盒子已恢复，正在重新加载...';
        setTimeout(function () { window.location.reload(); }, 900);
      }
    }).catch(function () {});
  }, 1500);
}

function openPointModal(name) {
  var editing = null;
  for (var i = 0; i < S.custom.length; i++) if (S.custom[i].name === name) editing = S.custom[i];
  var signal = editing || { name: '', point: '', report: ['monitor', 'readdata'], cloudKey: '', normalValue: 0, comment: '' };
  var report = normalizeReport(signal.report);

  var body = '' +
    '<div class="form-grid">' +
      '<label>显示名称<input id="pm-comment" value="' + esc(signal.comment) + '" placeholder="例如 水压"></label>' +
      '<label>内部标识<input id="pm-name" value="' + esc(signal.name) + '" placeholder="英文/数字，如 waterPressure"></label>' +
      '<label>软元件<input id="pm-point" value="' + esc(signal.point) + '" placeholder="例如 D150 / X50"></label>' +
      '<label>云端字段<input id="pm-cloud" value="' + esc(signal.cloudKey) + '" placeholder="默认同软元件"></label>' +
      '<label>正常值<input id="pm-normal" type="number" value="' + esc(signal.normalValue) + '"></label>' +
      '<div class="form-action"><button id="pm-test" class="btn ghost" type="button">测试读</button></div>' +
    '</div>' +
    '<div style="margin-top:14px"><div class="muted">上报通道</div>' +
      '<label class="chk"><input id="pm-monitor" type="checkbox" ' + (report.indexOf('monitor') >= 0 ? 'checked' : '') + '> 变化上报</label>' +
      '<label class="chk"><input id="pm-readdata" type="checkbox" ' + (report.indexOf('readdata') >= 0 ? 'checked' : '') + '> 立即读取</label>' +
      '<label class="chk"><input id="pm-pushalarm" type="checkbox" ' + (report.indexOf('pushalarm') >= 0 ? 'checked' : '') + '> 故障上报</label>' +
    '</div>' +
    '<p class="hint">系统内置点位只读；自定义点位保存后应用会重启程序。云端是否接收新字段需云端同步登记。</p>' +
    '<div id="pm-result" class="hint"></div>';

  openModal(editing ? '编辑监控点' : '新增监控点', body, function () {
    var point = $('pm-point').value.trim().toUpperCase();
    var name = $('pm-name').value.trim();
    var comment = $('pm-comment').value.trim();
    if (!/^[DYMXS][0-9]+$/.test(point)) { toast('软元件格式应为 D/Y/M/X/S + 数字', true); return false; }
    if (!name) {
      name = 'custom' + point;
      $('pm-name').value = name;
    }
    if (isDefaultSignal(name)) { toast('内部标识不能和系统点位重名', true); return false; }
    var duplicate = S.custom.some(function (item) { return item !== editing && item.name === name; });
    if (duplicate) { toast('内部标识重复', true); return false; }
    var channels = [];
    if ($('pm-monitor').checked) channels.push('monitor');
    if ($('pm-readdata').checked) channels.push('readdata');
    if ($('pm-pushalarm').checked) channels.push('pushalarm');

    var next = {
      name: name,
      point: point,
      report: channels,
      cloudKey: $('pm-cloud').value.trim(),
      normalValue: parseInt($('pm-normal').value, 10) || 0,
      comment: comment
    };
    delete S.deleted[name];
    if (editing) {
      if (editing.name !== name) S.deleted[editing.name] = true;
      for (var i = 0; i < S.custom.length; i++) if (S.custom[i] === editing) S.custom[i] = next;
    } else {
      S.custom.push(next);
    }
    renderPoints();
    toast('已加入草稿，点“保存并应用”后生效');
    return true;
  });

  $('pm-test').onclick = function () {
    var point = $('pm-point').value.trim().toUpperCase();
    $('pm-result').textContent = '读取中...';
    api('/api/plc/test-read', { point: point }).then(function (data) {
      $('pm-result').textContent = '当前值: ' + data.value;
    }).catch(function (err) {
      $('pm-result').textContent = '读取失败: ' + err.message;
    });
  };
}

function deletePoint(name) {
  if (!window.confirm('删除自定义监控点 ' + name + '？')) return;
  S.custom = S.custom.filter(function (item) { return item.name !== name; });
  S.deleted[name] = true;
  renderPoints();
  toast('已从草稿移除，点“保存并应用”后生效');
}

function openModal(title, bodyHtml, onSave) {
  $('modal-title').textContent = title;
  $('modal-body').innerHTML = bodyHtml;
  $('modal').classList.remove('hidden');
  $('modal-close').onclick = closeModal;
  var foot = $('modal-foot');
  foot.innerHTML = '<button id="modal-cancel" class="btn ghost">取消</button><button id="modal-save" class="btn primary">保存</button>';
  $('modal-cancel').onclick = closeModal;
  $('modal-save').onclick = function () {
    if (onSave()) closeModal();
  };
}

function closeModal() {
  $('modal').classList.add('hidden');
  $('modal-body').innerHTML = '';
  $('modal-foot').innerHTML = '';
}

function rollback(id) {
  if (!window.confirm('回滚到 ' + id + '？程序会重启。')) return;
  api('/api/config/rollback', { id: id }).then(function () {
    showRestart();
  }).catch(function (err) { toast(err.message, true); });
}

function bindEvents() {
  $('login-form').onsubmit = function (event) {
    event.preventDefault();
    var btn = $('login-btn');
    $('login-error').textContent = '';
    btn.disabled = true;
    btn.textContent = '登录中...';
    api('/api/login', { username: $('login-user').value.trim(), password: $('login-pass').value })
      .then(function () { return loadAll(); })
      .catch(function (err) { $('login-error').textContent = err.message; })
      .finally(function () {
        btn.disabled = false;
        btn.textContent = '进入配置台';
      });
  };

  Array.prototype.forEach.call(document.querySelectorAll('.nav-item'), function (item) {
    item.onclick = function () { setPage(item.getAttribute('data-page')); };
  });
  Array.prototype.forEach.call(document.querySelectorAll('[data-jump]'), function (btn) {
    btn.onclick = function () { setPage(btn.getAttribute('data-jump')); };
  });

  $('log-date').onchange = function () { S.logs.date = $('log-date').value; loadLogs(); };
  $('log-level').onchange = function () { S.logs.level = $('log-level').value; loadLogs(); };
  $('log-refresh').onclick = function () { loadLogs(); };
  $('log-auto').onchange = function () { S.logs.auto = $('log-auto').checked; startLogTimer(); };
  // 快捷筛选和搜索框互斥: 两个一起生效, 出来的结果没法解释。
  var savedLogKeyword = '';
  $('log-maint').onclick = function () {
    S.logs.maintenanceOnly = !S.logs.maintenanceOnly;
    var on = S.logs.maintenanceOnly;
    $('log-maint').classList.toggle('active', on);
    $('log-maint').textContent = on ? '只看维修相关 ✓' : '只看维修相关';
    $('log-search').disabled = on;
    if (on) {
      savedLogKeyword = S.logs.keyword;
      S.logs.keyword = '';
      $('log-search').value = '';
    } else {
      S.logs.keyword = savedLogKeyword;
      $('log-search').value = savedLogKeyword;
    }
    loadLogs();
  };
  var logSearchTimer = null;
  $('log-search').oninput = function () {
    clearTimeout(logSearchTimer);
    logSearchTimer = setTimeout(function () {
      S.logs.keyword = $('log-search').value.trim();
      loadLogs();
    }, 350);
  };

  $('logout-btn').onclick = function () {
    api('/api/logout', {}).finally(function () { showLogin(); });
  };

  $('refresh-btn').onclick = function () {
    loadAll().then(function () { toast('已刷新'); }).catch(function (err) { toast(err.message, true); });
  };

  // 维修模式: 传目标状态(幂等), 不靠"翻转"猜当前状态。
  $('maint-toggle').onclick = function () {
    var wash = (S.status && S.status.washcar) || {};
    if (typeof wash.maintaining !== 'boolean') {
      toast('盒子还没返回维修状态，先刷新页面', true);
      return;
    }
    var target = !wash.maintaining;
    var question = target
      ? '进入维修模式？云端下发的启动、复位命令会被拒绝，本地单点控制才允许执行。'
      : '退出维修模式？云端控制命令会恢复生效。';
    if (!window.confirm(question)) return;
    var btn = $('maint-toggle');
    btn.disabled = true;
    api('/93/11/washcar/changeMode', { maintenance: target }).then(function () {
      toast(target ? '已进入维修模式' : '已退出维修模式');
      return loadAll();
    }).catch(function (err) {
      toast(err.message, true);
    }).finally(function () {
      btn.disabled = false;
    });
  };

  $('gen-topics').onclick = generateTopicInputs;
  $('topic-sep').onchange = generateTopicInputs;
  $('topic-prefix').onchange = generateTopicInputs;

  $('mqtt-validate').onclick = function () {
    try { validatePatch(collectMqttPatch(), 'mqtt-issues').then(function () { toast('校验通过'); }).catch(function () {}); }
    catch (err) { showIssues('mqtt-issues', err.message); }
  };
  $('mqtt-apply').onclick = function () {
    try { applyPatch(collectMqttPatch(), 'mqtt-issues'); }
    catch (err) { showIssues('mqtt-issues', err.message); }
  };

  $('add-point').onclick = function () { openPointModal(null); };
  $('points-validate').onclick = function () {
    validatePatch(collectPointPatch(), 'points-issues').then(function () { toast('校验通过'); }).catch(function () {});
  };
  $('points-apply').onclick = function () { applyPatch(collectPointPatch(), 'points-issues'); };

  $('history-refresh').onclick = loadHistory;
  $('apply-restart').onclick = function () {
    if (!window.confirm('只重启程序？')) return;
    api('/api/system/restart', {}).then(showRestart).catch(function (err) { toast(err.message, true); });
  };

  $('password-form').onsubmit = function (event) {
    event.preventDefault();
    if ($('pw-new').value !== $('pw-new2').value) { toast('两次新密码不一致', true); return; }
    api('/api/password', { oldPassword: $('pw-old').value, newPassword: $('pw-new').value })
      .then(function () {
        toast('密码已修改');
        $('pw-old').value = ''; $('pw-new').value = ''; $('pw-new2').value = '';
        return loadAll();
      }).catch(function (err) { toast(err.message, true); });
  };

  $('restart-btn').onclick = function () {
    if (!window.confirm('重启程序？')) return;
    api('/api/system/restart', {}).then(showRestart).catch(function (err) { toast(err.message, true); });
  };
}

document.addEventListener('DOMContentLoaded', function () {
  bindEvents();
  loadAll();
});

window.addEventListener('hashchange', function () {
  var page = (window.location.hash || '').replace('#', '');
  if (PAGE_TITLES[page] && page !== S.page) setPage(page);
});

function showClientError(message) {
  var login = $('login');
  if (login && !login.classList.contains('hidden')) {
    var el = $('login-error');
    if (el) el.textContent = message;
  } else {
    toast(message, true);
  }
}

window.addEventListener('error', function (event) {
  if (event && event.message) showClientError('页面脚本错误: ' + event.message);
});
window.addEventListener('unhandledrejection', function (event) {
  var reason = event && event.reason;
  showClientError('请求异常: ' + (reason && reason.message ? reason.message : String(reason)));
});
