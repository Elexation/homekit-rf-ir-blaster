// Device detail page; every write carries the loaded rev so a second tab can't clobber it.

(function () {
	'use strict';

	var UI = window.BlasterUI;
	var Data = window.BlasterData;
	var NAME_MAX = 32;
	var REPEAT_MAX = 10;
	var DELAY_MAX = 5000;

	var state = { config: null, rev: 0, id: null, editing: null };

	function qid() {
		var m = /[?&]id=(\d+)/.exec(location.search);
		return m ? m[1] : null;
	}

	function device() { return Data.findDevice(state.config, state.id); }

	function afterWrite(res, okMsg, onOk) {
		if (res.conflict) {
			UI.toast('This config changed in another tab. Reloading…');
			setTimeout(function () { location.reload(); }, 1200);
			return;
		}
		state.rev = res.rev;
		state.config = res.config;
		if (okMsg) UI.toast(okMsg);
		if (onOk) onOk();
	}

	function header(d) {
		var count = UI.commandCount(d);
		var learned = UI.learnedCount(d);
		var pill;
		if (count === 0) pill = '<span class="pill pill--quiet">No commands</span>';
		else if (learned === count) pill = '<span class="pill pill--good"><span class="pill__dot"></span>Ready</span>';
		else pill = '<span class="pill pill--quiet">' + (count - learned) + ' not learned</span>';
		return '<header class="page__header">' +
			'<a class="pill pill--plain page__back" href="dashboard.html" aria-label="Back">' + UI.icon('i-back', 17) + '</a>' +
			'<div class="badge badge--lg' + (learned > 0 ? ' is-active' : '') + '">' + UI.icon(UI.serviceIcon(d.service), 20) + '</div>' +
			'<div class="page__grow">' +
				'<h1 class="page__title">' + UI.escapeHtml(d.name) + '</h1>' +
				'<div class="page__meta">' + UI.escapeHtml(UI.serviceLabel(d.service)) + ' · ' + count + (count === 1 ? ' command' : ' commands') + '</div>' +
			'</div>' + pill +
		'</header>';
	}

	function deviceCard(d) {
		return '<div class="card dev-card">' +
			'<div class="dev-form">' +
				'<div class="dev-form__row">' +
					'<label class="dev-form__label" for="dev-name">Name</label>' +
					'<input class="input" id="dev-name" type="text" maxlength="' + NAME_MAX + '" value="' + UI.escapeHtml(d.name) + '">' +
					'<div class="field-error" data-name-err hidden></div>' +
				'</div>' +
				'<button class="btn btn--primary" data-save-device>' + UI.icon('i-check', 16) + 'Save changes</button>' +
			'</div>' +
			'<div class="dev-danger">' +
				'<div class="dev-danger__grow">' +
					'<div class="dev-form__label">Delete device</div>' +
					'<div class="dev-danger__hint">Also removed from the Home app.</div>' +
				'</div>' +
				'<button class="btn btn--danger" data-delete-device>' + UI.icon('i-trash', 16) + 'Delete</button>' +
			'</div>' +
		'</div>';
	}

	function commandRow(c) {
		var rc = parseInt(c.repeatCount, 10) || 1;
		var rd = parseInt(c.repeatDelayMs, 10) || 0;
		if (state.editing === c.name) {
			return '<div class="list__row">' +
				'<div class="cmd__editor">' +
					'<div data-dd-cmd></div>' +
					'<div class="cmd__opts">' +
						'<label class="dev-form__label" for="cmd-repeat">Sends</label>' +
						'<input class="input cmd__num" id="cmd-repeat" type="number" min="1" max="' + REPEAT_MAX + '" value="' + rc + '" data-repeat-input>' +
						'<label class="dev-form__label" for="cmd-delay">Gap (ms)</label>' +
						'<input class="input cmd__num" id="cmd-delay" type="number" min="0" max="' + DELAY_MAX + '" step="50" value="' + rd + '" data-delay-input>' +
					'</div>' +
					'<div class="field-error" data-cmd-err hidden></div>' +
					'<div class="cmd__opts-actions">' +
						'<button class="btn btn--soft" data-cmd-save="' + UI.escapeHtml(c.name) + '">Save</button>' +
						'<button class="btn btn--ghost" data-rename-cancel>Cancel</button>' +
					'</div>' +
				'</div>' +
			'</div>';
		}
		var badge = rc > 1
			? '<span class="cmd__repeat">×' + rc + (rd ? ' · ' + rd + ' ms' : '') + '</span>'
			: '';
		return '<div class="list__row">' +
			'<div class="list__grow">' +
				'<div>' + UI.escapeHtml(UI.commandLabel(c.name)) + badge + '</div>' +
				'<div class="cmd__detail">' + UI.escapeHtml(UI.codeDetail(c)) + '</div>' +
			'</div>' +
			'<div class="cmd__actions">' +
				'<button class="btn btn--ghost btn--sm" data-edit="' + UI.escapeHtml(c.name) + '">Edit</button>' +
				'<button class="icon-btn" data-delete-cmd="' + UI.escapeHtml(c.name) + '" aria-label="Delete command">' + UI.icon('i-trash', 16) + '</button>' +
			'</div>' +
		'</div>';
	}

	function commandsCard(d) {
		var rows = UI.commandList(d);
		var body = rows.length
			? '<div class="list">' + rows.map(commandRow).join('') + '</div>'
			: '<div class="cmds-empty">No commands learned yet. Use “Learn another” to capture one.</div>';
		return '<div class="card cmds-card">' +
			'<div class="cmds-card__head">' +
				'<h2 class="section-title">Learned commands</h2>' +
				'<button class="btn btn--soft btn--sm" data-action="learn">' + UI.icon('i-plus', 15) + 'Learn another</button>' +
			'</div>' + body +
		'</div>';
	}

	function render() {
		var page = document.querySelector('[data-page]');
		var d = device();
		if (!d) {
			page.innerHTML = '<div class="card"><div class="not-found">' +
				'<div class="empty__badge">' + UI.icon('i-signal', 34) + '</div>' +
				'<div class="empty__title">Device not found</div>' +
				'<a class="btn btn--primary" href="dashboard.html">Back to dashboard</a>' +
			'</div></div>';
			return;
		}
		page.innerHTML = header(d) +
			'<div class="dev-grid">' + deviceCard(d) + commandsCard(d) + '</div>';
		wire();
	}

	function saveDevice() {
		var nameEl = document.getElementById('dev-name');
		var name = nameEl.value.trim();
		var nameErr = document.querySelector('[data-name-err]');
		if (!name) {
			nameEl.classList.add('is-invalid');
			nameErr.textContent = 'Name cannot be empty.';
			nameErr.hidden = false;
			return;
		}
		nameEl.classList.remove('is-invalid');
		nameErr.hidden = true;
		Data.saveDevice(state.id, { name: name }, state.rev)
			.then(function (res) { afterWrite(res, 'Saved', render); });
	}

	function deleteDevice() {
		var d = device();
		UI.confirmDialog({
			title: 'Delete “' + d.name + '”?',
			message: 'It also disappears from the Home app.',
			confirmLabel: 'Delete',
			danger: true
		}).then(function (ok) {
			if (!ok) return;
			Data.deleteDevice(state.id, state.rev).then(function (res) {
				if (res.conflict) { afterWrite(res); return; }
				location.href = 'dashboard.html';
			});
		});
	}

	// Persist the command's name, send count, and gap in one write, preserving the learned code.
	function saveCommand(oldName) {
		var ddHost = document.querySelector('[data-dd-cmd]');
		var repeatEl = document.querySelector('[data-repeat-input]');
		var delayEl = document.querySelector('[data-delay-input]');
		var err = document.querySelector('[data-cmd-err]');
		var newName = ddHost ? ddHost.getAttribute('data-value') : oldName;
		var repeat = parseInt(repeatEl.value, 10);
		var delay = parseInt(delayEl.value, 10);
		var cmds = device().commands;
		function fail(msg) { err.textContent = msg; err.hidden = false; }
		if (!newName) { fail('Command name cannot be empty.'); return; }
		if (newName !== oldName && Object.prototype.hasOwnProperty.call(cmds, newName)) {
			fail('A command named “' + newName + '” already exists.'); return;
		}
		if (!(repeat >= 1 && repeat <= REPEAT_MAX)) { fail('Sends must be between 1 and ' + REPEAT_MAX + '.'); return; }
		if (!(delay >= 0 && delay <= DELAY_MAX)) { fail('Gap must be between 0 and ' + DELAY_MAX + ' ms.'); return; }
		var out = {};
		Object.keys(cmds).forEach(function (k) {
			var key = k === oldName ? newName : k;
			out[key] = k === oldName
				? Object.assign({}, cmds[k], { repeatCount: repeat, repeatDelayMs: delay })
				: cmds[k];
		});
		Data.saveDevice(state.id, { commands: out }, state.rev)
			.then(function (res) { state.editing = null; afterWrite(res, 'Saved', render); });
	}

	function deleteCommand(name) {
		UI.confirmDialog({
			title: 'Delete the command “' + name + '”?',
			confirmLabel: 'Delete',
			danger: true
		}).then(function (ok) {
			if (!ok) return;
			var cmds = device().commands;
			var out = {};
			Object.keys(cmds).forEach(function (k) { if (k !== name) out[k] = cmds[k]; });
			Data.saveDevice(state.id, { commands: out }, state.rev)
				.then(function (res) { afterWrite(res, 'Deleted', render); });
		});
	}

	function wire() {
		var page = document.querySelector('[data-page]');
		var saveBtn = page.querySelector('[data-save-device]');
		if (saveBtn) saveBtn.addEventListener('click', saveDevice);
		var delBtn = page.querySelector('[data-delete-device]');
		if (delBtn) delBtn.addEventListener('click', deleteDevice);

		page.querySelectorAll('[data-edit]').forEach(function (b) {
			b.addEventListener('click', function () { state.editing = b.getAttribute('data-edit'); render(); });
		});
		page.querySelectorAll('[data-cmd-save]').forEach(function (b) {
			b.addEventListener('click', function () { saveCommand(b.getAttribute('data-cmd-save')); });
		});
		var cancel = page.querySelector('[data-rename-cancel]');
		if (cancel) cancel.addEventListener('click', function () { state.editing = null; render(); });
		var ddHost = page.querySelector('[data-dd-cmd]');
		if (ddHost) {
			var d = device();
			var editing = state.editing;
			var used = {};
			Object.keys(d.commands).forEach(function (k) { if (k !== editing) used[k] = true; });
			var cmdOpts = UI.commandsForService(d.service).filter(function (o) { return !used[o.value]; });
			if (!cmdOpts.some(function (o) { return o.value === editing; })) {
				cmdOpts.unshift({ value: editing, label: UI.commandLabel(editing) });
			}
			UI.buildDropdown(ddHost, cmdOpts, editing, 'cmd-name-edit', function () {});
			var ddBtn = ddHost.querySelector('.dropdown__btn');
			if (ddBtn) { ddBtn.setAttribute('aria-label', 'Command name'); ddBtn.focus(); }
		}

		page.querySelectorAll('[data-delete-cmd]').forEach(function (b) {
			b.addEventListener('click', function () { deleteCommand(b.getAttribute('data-delete-cmd')); });
		});
		var learn = page.querySelector('[data-action="learn"]');
		if (learn) learn.addEventListener('click', function () {
			window.BlasterLearn.open({
				config: state.config,
				rev: state.rev,
				deviceId: state.id,
				onSaved: function (res) {
					state.config = res.config;
					state.rev = res.rev;
					render();
				}
			});
		});
	}

	state.id = qid();
	Data.getConfig().then(function (r) {
		state.config = r.config;
		state.rev = r.rev;
		render();
	});
})();
