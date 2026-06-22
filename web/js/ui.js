// Shared render helpers for the config pages.

window.BlasterUI = (function () {
	'use strict';

	// HomeKit service -> sprite symbol id.
	var SERVICE_ICON = {
		Television: 'i-tv',
		Fan: 'i-fan',
		LightBulb: 'i-bulb',
		Switch: 'i-switch',
		Outlet: 'i-outlet',
		WindowCovering: 'i-blinds'
	};

	var SERVICE_LABEL = {
		Television: 'Television',
		Fan: 'Fan',
		LightBulb: 'Light',
		Switch: 'Switch',
		Outlet: 'Outlet',
		WindowCovering: 'Window covering'
	};

	function serviceIcon(service) { return SERVICE_ICON[service] || 'i-zap'; }
	function serviceLabel(service) { return SERVICE_LABEL[service] || 'Device'; }

	// Command-name dispatch contract; MUST mirror firePower/keyCommand + the WindowCovering
	// up/down/stop map in src/accessory_builder.cpp (any other name never transmits).
	var POWER_CMDS = [
		{ value: 'on', label: 'On' },
		{ value: 'off', label: 'Off' },
		{ value: 'toggle', label: 'Toggle' }
	];
	var COMMANDS = {
		Switch: POWER_CMDS,
		Outlet: POWER_CMDS,
		LightBulb: POWER_CMDS,
		Fan: POWER_CMDS,
		WindowCovering: [
			{ value: 'up', label: 'Open' },
			{ value: 'down', label: 'Close' },
			{ value: 'stop', label: 'Stop' }
		],
		Television: POWER_CMDS.concat([
			{ value: 'key_up', label: 'Up' },
			{ value: 'key_down', label: 'Down' },
			{ value: 'key_left', label: 'Left' },
			{ value: 'key_right', label: 'Right' },
			{ value: 'key_select', label: 'Select' },
			{ value: 'key_back', label: 'Back' },
			{ value: 'key_play_pause', label: 'Play / Pause' },
			{ value: 'key_info', label: 'Info' },
			{ value: 'volume_up', label: 'Volume Up' },
			{ value: 'volume_down', label: 'Volume Down' }
		])
	};
	var COMMAND_LABEL = {};
	Object.keys(COMMANDS).forEach(function (svc) {
		COMMANDS[svc].forEach(function (c) { COMMAND_LABEL[c.value] = c.label; });
	});

	function commandsForService(service) { return COMMANDS[service] || []; }
	function commandLabel(name) { return COMMAND_LABEL[name] || name; }

	function escapeHtml(s) {
		return String(s == null ? '' : s)
			.replace(/&/g, '&amp;')
			.replace(/</g, '&lt;')
			.replace(/>/g, '&gt;')
			.replace(/"/g, '&quot;')
			.replace(/'/g, '&#39;');
	}

	function icon(id, size) {
		var s = size || 18;
		return '<svg class="icon" width="' + s + '" height="' + s + '">' +
			'<use href="#' + id + '"></use></svg>';
	}

	function isLearned(cmd) { return cmd && cmd.kind && cmd.kind !== 'none'; }

	function protoLabel(cmd) {
		if (!cmd) return null;
		if (cmd.kind === 'ir') return 'IR';
		if (cmd.kind === 'rf') return cmd.freqMHz === 315 ? 'RF 315' : 'RF 433';
		return null;
	}

	function codeDetail(cmd) {
		if (!cmd || cmd.kind === 'none' || !cmd.kind) return 'Not learned';
		if (cmd.kind === 'ir') {
			var khz = cmd.carrierHz ? (cmd.carrierHz / 1000) : 0;
			return 'IR · ' + khz + ' kHz';
		}
		return 'RF · ' + (cmd.freqMHz || 0) + ' MHz';
	}

	function commandList(device) {
		return Object.keys(device.commands).map(function (name) {
			var c = device.commands[name];
			return {
				name: name, kind: c.kind, freqMHz: c.freqMHz,
				carrierHz: c.carrierHz, rolling: c.rolling, pulses: c.pulses,
				repeatCount: c.repeatCount, repeatDelayMs: c.repeatDelayMs
			};
		});
	}

	function commandCount(device) { return Object.keys(device.commands).length; }

	function learnedCount(device) {
		return commandList(device).filter(isLearned).length;
	}

	function deviceProtos(device) {
		var order = ['IR', 'RF 433', 'RF 315'];
		var seen = {};
		commandList(device).forEach(function (c) {
			var p = protoLabel(c);
			if (p) seen[p] = true;
		});
		return order.filter(function (p) { return seen[p]; });
	}

	function setActiveNav(name) {
		var els = document.querySelectorAll('[data-nav]');
		for (var i = 0; i < els.length; i++) {
			if (els[i].getAttribute('data-nav') === name) els[i].classList.add('is-active');
		}
	}

	var toastTimer = null;
	function toast(msg) {
		var el = document.querySelector('[data-toast]');
		if (!el) return;
		el.textContent = msg;
		el.classList.add('is-visible');
		clearTimeout(toastTimer);
		toastTimer = setTimeout(function () { el.classList.remove('is-visible'); }, 2400);
	}

	// Styled confirm; resolves true/false. Escapes all option strings itself.
	function confirmDialog(opts) {
		return new Promise(function (resolve) {
			var prev = document.activeElement;
			var scrim = document.createElement('div');
			scrim.className = 'modal-scrim';
			scrim.innerHTML = '<div class="modal" role="alertdialog" aria-modal="true" aria-label="' + escapeHtml(opts.title) + '">' +
				'<div class="modal__head"><div class="modal__title">' + escapeHtml(opts.title) + '</div></div>' +
				(opts.message ? '<p class="modal__text">' + escapeHtml(opts.message) + '</p>' : '') +
				'<div class="modal__actions">' +
					'<button class="btn btn--ghost" data-cancel>Cancel</button>' +
					'<button class="btn ' + (opts.danger ? 'btn--danger' : 'btn--primary') + '" data-ok>' +
						escapeHtml(opts.confirmLabel || 'Confirm') + '</button>' +
				'</div></div>';
			document.body.appendChild(scrim);
			function close(result) {
				document.removeEventListener('keydown', onKey);
				scrim.classList.remove('is-open');
				// matches the 0.2s scrim fade-out before removal
				setTimeout(function () {
					scrim.remove();
					if (prev && prev.focus) prev.focus();
					resolve(result);
				}, 250);
			}
			function onKey(e) {
				if (e.key === 'Escape') close(false);
			}
			scrim.addEventListener('click', function (e) {
				if (e.target === scrim) close(false);
			});
			scrim.querySelector('[data-cancel]').addEventListener('click', function () { close(false); });
			scrim.querySelector('[data-ok]').addEventListener('click', function () { close(true); });
			document.addEventListener('keydown', onKey);
			// force a style pass so the .is-open entrance transition runs
			void scrim.offsetWidth;
			scrim.classList.add('is-open');
			scrim.querySelector('[data-cancel]').focus();
		});
	}

	// "Restarting…" overlay. mode 'reconnect' polls until the device reboots, then goes to login;
	// otherwise just shows the message.
	function restarting(opts) {
		opts = opts || {};
		var scrim = document.createElement('div');
		scrim.className = 'modal-scrim';
		scrim.innerHTML = '<div class="modal modal--restart" role="alertdialog" aria-modal="true" aria-label="Restarting">' +
			'<div class="restart-spin" aria-hidden="true"></div>' +
			'<div class="restart-title">' + escapeHtml(opts.title || 'Restarting…') + '</div>' +
			(opts.message ? '<p class="modal__text">' + escapeHtml(opts.message) + '</p>' : '') +
			'<div class="restart-extra" hidden></div>' +
			'</div>';
		document.body.appendChild(scrim);
		void scrim.offsetWidth;
		scrim.classList.add('is-open');
		if (opts.mode !== 'reconnect') return;

		var extra = scrim.querySelector('.restart-extra');
		var tries = 0, MAX = 50;  // ~60 s at 1.2 s cadence
		function again() {
			if (++tries >= MAX) {
				extra.hidden = false;
				extra.innerHTML = 'Taking longer than expected. <a href="login.html">Reload</a>';
				return;
			}
			setTimeout(loop, 1200);
		}
		// Session secret is per-boot, so after a reboot our cookie fails: 401 = back, 200 = not yet, error = down.
		function loop() {
			fetch('/api/status', { credentials: 'same-origin', cache: 'no-store', headers: { Accept: 'application/json' } })
				.then(function (r) {
					if (r.status === 401) { location.href = 'login.html'; return; }
					again();
				})
				.catch(again);
		}
		setTimeout(loop, 800);  // let the response flush and the reboot begin first
	}

	// Themed dropdown; items = [{value,label,icon}], onChange(value) on select.
	function buildDropdown(root, items, value, id, onChange) {
		function find(v) {
			for (var i = 0; i < items.length; i++) {
				if (items[i].value === v) return items[i];
			}
			return items[0];
		}
		function triggerHtml(it) {
			return (it.icon ? icon(it.icon, 16) : '') +
				'<span class="dropdown__val">' + escapeHtml(it.label) + '</span>' +
				'<span class="dropdown__chev">' + icon('i-chev-d', 16) + '</span>';
		}
		var current = find(value);
		root.className = 'dropdown';
		root.setAttribute('data-value', current.value);
		root.innerHTML = '<button type="button" class="input dropdown__btn" id="' + id + '" aria-haspopup="listbox" aria-expanded="false">' +
				triggerHtml(current) + '</button>' +
			'<div class="dropdown__list" role="listbox" hidden>' +
				items.map(function (it) {
					var active = it === current;
					return '<button type="button" class="dropdown__item' + (active ? ' is-active' : '') + '" role="option"' +
						' aria-selected="' + active + '" data-value="' + escapeHtml(it.value) + '">' +
						(it.icon ? icon(it.icon, 16) : '') + escapeHtml(it.label) + '</button>';
				}).join('') +
			'</div>';
		var btn = root.querySelector('.dropdown__btn');
		var list = root.querySelector('.dropdown__list');
		function onDocClick(e) {
			if (!root.contains(e.target)) setOpen(false);
		}
		function setOpen(open) {
			list.hidden = !open;
			root.classList.toggle('is-open', open);
			btn.setAttribute('aria-expanded', open ? 'true' : 'false');
			if (open) {
				document.addEventListener('click', onDocClick);
				var act = list.querySelector('.is-active') || list.querySelector('.dropdown__item');
				if (act) act.focus();
			} else {
				document.removeEventListener('click', onDocClick);
			}
		}
		function select(item) {
			var v = item.getAttribute('data-value');
			root.setAttribute('data-value', v);
			list.querySelectorAll('.dropdown__item').forEach(function (o) {
				o.classList.toggle('is-active', o === item);
				o.setAttribute('aria-selected', o === item ? 'true' : 'false');
			});
			btn.innerHTML = triggerHtml(find(v));
			setOpen(false);
			btn.focus();
			if (onChange) onChange(v);
		}
		btn.addEventListener('click', function () { setOpen(list.hidden); });
		btn.addEventListener('keydown', function (e) {
			if (e.key === 'ArrowDown' && list.hidden) {
				e.preventDefault();
				setOpen(true);
			} else if (e.key === 'Escape' && !list.hidden) {
				e.stopPropagation(); // close the list, not the modal
				setOpen(false);
			}
		});
		list.addEventListener('keydown', function (e) {
			var els = Array.prototype.slice.call(list.querySelectorAll('.dropdown__item'));
			var i = els.indexOf(document.activeElement);
			if (e.key === 'ArrowDown') {
				e.preventDefault();
				if (i < els.length - 1) els[i + 1].focus();
			} else if (e.key === 'ArrowUp') {
				e.preventDefault();
				if (i > 0) els[i - 1].focus();
			} else if (e.key === 'Escape') {
				e.stopPropagation(); // close the list, not the modal
				setOpen(false);
				btn.focus();
			}
		});
		list.querySelectorAll('.dropdown__item').forEach(function (it) {
			it.addEventListener('click', function () { select(it); });
		});
	}

	return {
		serviceIcon: serviceIcon,
		serviceLabel: serviceLabel,
		commandsForService: commandsForService,
		commandLabel: commandLabel,
		buildDropdown: buildDropdown,
		escapeHtml: escapeHtml,
		icon: icon,
		isLearned: isLearned,
		protoLabel: protoLabel,
		codeDetail: codeDetail,
		commandList: commandList,
		commandCount: commandCount,
		learnedCount: learnedCount,
		deviceProtos: deviceProtos,
		setActiveNav: setActiveNav,
		toast: toast,
		confirmDialog: confirmDialog,
		restarting: restarting
	};
})();
