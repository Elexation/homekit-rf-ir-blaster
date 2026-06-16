// Device detail page. Every write carries the page's loaded rev so a second
// tab's write is detected, not clobbered.

(function () {
	'use strict';

	var UI = window.BlasterUI;
	var Data = window.BlasterData;
	var NAME_MAX = 32;
	var REPEAT_MAX = 10;

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
				'<div class="dev-form__row">' +
					'<label class="dev-form__label" for="dev-repeat">Repeat count</label>' +
					'<input class="input" id="dev-repeat" type="number" min="1" max="' + REPEAT_MAX + '" value="' + (parseInt(d.options && d.options.repeatCount, 10) || 1) + '">' +
					'<div class="field-error" data-repeat-err hidden>Repeat count must be between 1 and ' + REPEAT_MAX + '.</div>' +
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
		if (state.editing === c.name) {
			return '<div class="list__row">' +
				'<div class="cmd__edit">' +
					'<input class="input" type="text" maxlength="' + NAME_MAX + '" value="' + UI.escapeHtml(c.name) + '" data-rename-input>' +
					'<button class="btn btn--soft" data-rename-save="' + UI.escapeHtml(c.name) + '">Save</button>' +
					'<button class="btn btn--ghost" data-rename-cancel>Cancel</button>' +
				'</div>' +
			'</div>';
		}
		return '<div class="list__row">' +
			'<div class="list__grow">' +
				'<div>' + UI.escapeHtml(c.name) + '</div>' +
				'<div class="cmd__detail">' + UI.escapeHtml(UI.codeDetail(c)) + '</div>' +
			'</div>' +
			'<div class="cmd__actions">' +
				'<button class="btn btn--ghost btn--sm" data-rename="' + UI.escapeHtml(c.name) + '">Rename</button>' +
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
		var d = device();
		var nameEl = document.getElementById('dev-name');
		var repeatEl = document.getElementById('dev-repeat');
		var name = nameEl.value.trim();
		var repeat = parseInt(repeatEl.value, 10);
		var nameErr = document.querySelector('[data-name-err]');
		var repeatErr = document.querySelector('[data-repeat-err]');
		var ok = true;
		if (!name) {
			nameEl.classList.add('is-invalid');
			nameErr.textContent = 'Name cannot be empty.';
			nameErr.hidden = false;
			ok = false;
		} else {
			nameEl.classList.remove('is-invalid');
			nameErr.hidden = true;
		}
		if (!(repeat >= 1 && repeat <= REPEAT_MAX)) {
			repeatEl.classList.add('is-invalid');
			repeatErr.hidden = false;
			ok = false;
		} else {
			repeatEl.classList.remove('is-invalid');
			repeatErr.hidden = true;
		}
		if (!ok) return;
		Data.saveDevice(state.id, { name: name, options: { repeatCount: repeat } }, state.rev)
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

	function rebuildCommands(oldName, newName) {
		var cmds = device().commands;
		var out = {};
		Object.keys(cmds).forEach(function (k) { out[k === oldName ? newName : k] = cmds[k]; });
		return out;
	}

	function saveRename(oldName) {
		var input = document.querySelector('[data-rename-input]');
		var newName = input.value.trim();
		if (!newName) { UI.toast('Command name cannot be empty.'); return; }
		var cmds = device().commands;
		if (newName !== oldName && Object.prototype.hasOwnProperty.call(cmds, newName)) {
			UI.toast('A command named “' + newName + '” already exists.');
			return;
		}
		if (newName === oldName) { state.editing = null; render(); return; }
		Data.saveDevice(state.id, { commands: rebuildCommands(oldName, newName) }, state.rev)
			.then(function (res) { state.editing = null; afterWrite(res, 'Renamed', render); });
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

		page.querySelectorAll('[data-rename]').forEach(function (b) {
			b.addEventListener('click', function () { state.editing = b.getAttribute('data-rename'); render(); });
		});
		page.querySelectorAll('[data-rename-save]').forEach(function (b) {
			b.addEventListener('click', function () { saveRename(b.getAttribute('data-rename-save')); });
		});
		var cancel = page.querySelector('[data-rename-cancel]');
		if (cancel) cancel.addEventListener('click', function () { state.editing = null; render(); });
		var renameInput = page.querySelector('[data-rename-input]');
		if (renameInput) renameInput.focus();

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
