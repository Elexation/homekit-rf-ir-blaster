// Learn modal (shared by dashboard and device pages). The opener passes its
// config/rev and refreshes via onSaved. Capture is stubbed until the radios exist.

window.BlasterLearn = (function () {
	'use strict';

	var UI = window.BlasterUI;
	var Data = window.BlasterData;
	var NAME_MAX = 32;
	var MAX_DEVICES = 32;
	var SERVICES = ['Switch', 'Outlet', 'LightBulb', 'Fan', 'Television', 'WindowCovering'];

	// One learn at a time, matching the device's single-learn lock.
	var active = false;
	var seq = 0;

	var FAIL = {
		noisy: {
			title: 'Couldn’t read that signal',
			note: 'Move the remote closer to the device, hold the button for a full second, and try again.',
			retry: true
		},
		timeout: {
			title: 'Nothing captured',
			note: 'No signal arrived. Check the remote’s battery and try again.',
			retry: true
		},
		rolling: {
			title: 'This remote can’t be learned',
			note: 'Every press sends a new rolling code, so a captured one is already spent when it is replayed.',
			retry: false
		},
		unavailable: {
			title: 'Learning isn’t ready yet',
			note: 'Capturing codes needs the radio hardware, which isn’t connected on this build. It will work once the receivers are wired in.',
			retry: false
		}
	};

	function head(title, step) {
		var dots = [1, 2, 3].map(function (n) {
			return '<span class="modal__dot' + (n === step ? ' is-active' : '') + '"></span>';
		}).join('');
		return '<div class="modal__head"><div class="modal__title">' + title + '</div>' +
			'<div class="modal__dots">' + dots + '</div></div>';
	}

	// Bar heights quantized to the shortest pulse; even index = mark.
	function bars(pulses) {
		var vals = pulses.slice(0, 32);
		var min = Math.min.apply(null, vals);
		return '<div class="learn-bars">' + vals.map(function (v, i) {
			var b = v <= min * 1.5 ? 1 : v <= min * 3 ? 2 : v <= min * 8 ? 3 : 4;
			return '<span class="learn-bar--' + b + (i % 2 === 0 ? ' is-mark' : '') + '"></span>';
		}).join('') + '</div>';
	}

	function readMeta(code) {
		var detail = code.kind === 'ir'
			? 'Carrier <strong>' + (code.carrierHz / 1000) + ' kHz</strong>'
			: 'Frequency <strong>' + (code.freqMHz || 0) + ' MHz</strong>';
		return '<div class="learn-read__meta">' +
			'<span>Band <strong>' + (code.kind === 'ir' ? 'Infrared' : 'RF') + '</strong></span>' +
			'<span>' + detail + '</span>' +
			'<span>Pulses <strong>' + code.pulses.length + '</strong></span>' +
		'</div>';
	}

	function showError(el, errEl, msg) {
		if (msg) {
			el.classList.add('is-invalid');
			errEl.textContent = msg;
			errEl.hidden = false;
		} else {
			el.classList.remove('is-invalid');
			errEl.hidden = true;
		}
	}

	// Themed dropdown. items = [{value,label,icon}]; current value in root's
	// data-value; onChange(value) on select.
	function buildDropdown(root, items, value, id, onChange) {
		function find(v) {
			for (var i = 0; i < items.length; i++) {
				if (items[i].value === v) return items[i];
			}
			return items[0];
		}
		function triggerHtml(it) {
			return (it.icon ? UI.icon(it.icon, 16) : '') +
				'<span class="dropdown__val">' + UI.escapeHtml(it.label) + '</span>' +
				'<span class="dropdown__chev">' + UI.icon('i-chev-d', 16) + '</span>';
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
						' aria-selected="' + active + '" data-value="' + UI.escapeHtml(it.value) + '">' +
						(it.icon ? UI.icon(it.icon, 16) : '') + UI.escapeHtml(it.label) + '</button>';
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

	function open(opts) {
		if (active) return;
		active = true;
		var prev = document.activeElement;
		var state = {
			step: 'listen',
			code: null,
			reason: 'noisy',
			target: opts.deviceId != null ? String(opts.deviceId) : 'new',
			service: 'Switch'
		};
		var scrim = document.createElement('div');
		scrim.className = 'modal-scrim';
		scrim.innerHTML = '<div class="modal" role="dialog" aria-modal="true"></div>';
		var modal = scrim.querySelector('.modal');
		document.body.appendChild(scrim);

		function close() {
			active = false;
			document.removeEventListener('keydown', onKey);
			if (state.step === 'listen') Data.cancelLearn();
			seq += 1; // orphans any in-flight capture resolution
			scrim.classList.remove('is-open');
			// matches the 0.2s scrim fade-out before removal
			setTimeout(function () {
				scrim.remove();
				if (prev && prev.focus) prev.focus();
			}, 250);
		}

		function onKey(e) {
			if (e.key === 'Escape') close();
		}

		function arm() {
			var token = ++seq;
			Data.startLearn().then(function (res) {
				if (token !== seq || state.step !== 'listen') return;
				if (res.ok) {
					state.code = res.code;
					setStep('captured');
				} else {
					state.reason = res.reason;
					setStep('fail');
				}
			});
		}

		function focusStep() {
			var el = modal.querySelector('[data-autofocus]');
			if (el) el.focus();
		}

		function setStep(step) {
			state.step = step;
			modal.className = 'modal' + (step === 'fail' ? ' learn--fail' : '');
			if (step === 'listen') renderListen();
			else if (step === 'captured') renderCaptured();
			else if (step === 'fail') renderFail();
			else renderAssign();
			focusStep();
			if (step === 'listen') arm();
		}

		function renderListen() {
			modal.innerHTML = head('Listening for a signal', 1) +
				'<div class="learn-pulse"><div class="learn-pulse__ring"><div class="learn-pulse__core">' +
					UI.icon('i-signal', 30) + '</div></div></div>' +
				'<p class="learn-instr">Point the original remote at the device and press the button you want to capture a few times.</p>' +
				'<div class="modal__actions"><button class="btn btn--ghost" data-cancel data-autofocus>Cancel</button></div>';
			modal.setAttribute('aria-label', 'Listening for a signal');
			modal.querySelector('[data-cancel]').addEventListener('click', close);
		}

		function renderCaptured() {
			modal.innerHTML = head('Signal captured', 2) +
				'<div class="learn-read">' + bars(state.code.pulses) + readMeta(state.code) + '</div>' +
				'<div class="learn-note learn-note--good">' + UI.icon('i-check', 16) +
					'<span>Clean read, matched 3 of 3 repeats.</span></div>' +
				'<div class="modal__actions">' +
					'<button class="btn btn--ghost" data-retry>' + UI.icon('i-refresh', 15) + 'Re-capture</button>' +
					'<button class="btn btn--primary" data-accept data-autofocus>Use this code</button>' +
				'</div>';
			modal.setAttribute('aria-label', 'Signal captured');
			modal.querySelector('[data-retry]').addEventListener('click', function () { setStep('listen'); });
			modal.querySelector('[data-accept]').addEventListener('click', function () { setStep('assign'); });
		}

		function renderFail() {
			var f = FAIL[state.reason] || FAIL.noisy;
			var actions = f.retry
				? '<button class="btn btn--ghost" data-cancel>Cancel</button>' +
					'<button class="btn btn--primary" data-retry data-autofocus>' + UI.icon('i-refresh', 15) + 'Try again</button>'
				: '<button class="btn btn--ghost" data-cancel data-autofocus>Close</button>';
			modal.innerHTML = head(f.title, 1) +
				'<div class="learn-note learn-note--warn">' + UI.icon('i-signal', 16) +
					'<span>' + f.note + '</span></div>' +
				'<div class="modal__actions">' + actions + '</div>';
			modal.setAttribute('aria-label', f.title);
			modal.querySelector('[data-cancel]').addEventListener('click', close);
			var retry = modal.querySelector('[data-retry]');
			if (retry) retry.addEventListener('click', function () { setStep('listen'); });
		}

		function renderAssign() {
			// Device page: target is locked, no picker.
			var locked = opts.deviceId != null ? Data.findDevice(opts.config, opts.deviceId) : null;
			var deviceField = '';
			if (locked) {
				deviceField = '<div class="learn-field">' +
					'<div class="ctl__label">Device</div>' +
					'<div class="field">' + UI.icon(UI.serviceIcon(locked.service), 16) + UI.escapeHtml(locked.name) + '</div>' +
				'</div>';
			} else if (opts.config.devices.length > 0) {
				deviceField = '<div class="learn-field">' +
					'<label class="ctl__label" for="learn-device">Device</label>' +
					'<div data-dd-device></div>' +
				'</div>';
			}
			modal.innerHTML = head('Name &amp; assign', 3) +
				'<div class="learn-field">' +
					'<label class="ctl__label" for="learn-name">Command name</label>' +
					'<input class="input" id="learn-name" type="text" maxlength="' + NAME_MAX + '" placeholder="Power" data-autofocus>' +
					'<div class="field-error" data-name-err hidden></div>' +
				'</div>' +
				deviceField +
				'<div class="learn-field" data-new-name hidden>' +
					'<label class="ctl__label" for="learn-dev-name">Device name</label>' +
					'<input class="input" id="learn-dev-name" type="text" maxlength="' + NAME_MAX + '" placeholder="Living room TV">' +
					'<div class="field-error" data-dev-err hidden></div>' +
				'</div>' +
				'<div class="learn-field" data-new-service hidden>' +
					'<label class="ctl__label" for="learn-service">Type</label>' +
					'<div data-dd-service></div>' +
				'</div>' +
				'<div class="modal__actions">' +
					'<button class="btn btn--ghost" data-back>Back</button>' +
					'<button class="btn btn--primary" data-save>Save command</button>' +
				'</div>';
			modal.setAttribute('aria-label', 'Name & assign');

			function refreshNew() {
				var isNew = !locked && state.target === 'new';
				modal.querySelector('[data-new-name]').hidden = !isNew;
				modal.querySelector('[data-new-service]').hidden = !isNew;
			}
			var devHost = modal.querySelector('[data-dd-device]');
			if (devHost) {
				var items = opts.config.devices.map(function (d) {
					return { value: String(parseInt(d.id, 10) || 0), label: d.name, icon: UI.serviceIcon(d.service) };
				});
				items.push({ value: 'new', label: 'New device…', icon: 'i-plus' });
				buildDropdown(devHost, items, state.target, 'learn-device', function (v) {
					state.target = v;
					refreshNew();
				});
			}
			buildDropdown(modal.querySelector('[data-dd-service]'), SERVICES.map(function (s) {
				return { value: s, label: UI.serviceLabel(s), icon: UI.serviceIcon(s) };
			}), state.service, 'learn-service', function (v) { state.service = v; });
			refreshNew();
			modal.querySelector('[data-back]').addEventListener('click', function () { setStep('captured'); });
			modal.querySelector('[data-save]').addEventListener('click', save);
		}

		function save() {
			var nameEl = modal.querySelector('#learn-name');
			var nameErr = modal.querySelector('[data-name-err]');
			var name = nameEl.value.trim();
			var isNew = state.target === 'new';
			var target = isNew ? null : Data.findDevice(opts.config, state.target);
			var ok = true;

			if (!name) {
				showError(nameEl, nameErr, 'Command name cannot be empty.');
				ok = false;
			} else if (target && Object.prototype.hasOwnProperty.call(target.commands, name)) {
				showError(nameEl, nameErr, 'A command named “' + name + '” already exists on this device.');
				ok = false;
			} else {
				showError(nameEl, nameErr, null);
			}

			var devName = '';
			if (isNew) {
				var devEl = modal.querySelector('#learn-dev-name');
				var devErr = modal.querySelector('[data-dev-err]');
				devName = devEl.value.trim();
				if (!devName) {
					showError(devEl, devErr, 'Device name cannot be empty.');
					ok = false;
				} else if (opts.config.devices.length >= MAX_DEVICES) {
					showError(devEl, devErr, 'The device limit (' + MAX_DEVICES + ') is reached.');
					ok = false;
				} else {
					showError(devEl, devErr, null);
				}
			}
			if (!ok) return;

			modal.querySelector('[data-save]').disabled = true;
			var write;
			if (isNew) {
				var commands = {};
				commands[name] = state.code;
				write = Data.addDevice({
					name: devName,
					service: state.service,
					commands: commands
				}, opts.rev);
			} else {
				var cmds = {};
				Object.keys(target.commands).forEach(function (k) { cmds[k] = target.commands[k]; });
				cmds[name] = state.code;
				write = Data.saveDevice(target.id, { commands: cmds }, opts.rev);
			}
			write.then(function (res) {
				if (res.conflict) {
					UI.toast('This config changed in another tab. Reloading…');
					setTimeout(function () { location.reload(); }, 1200);
					return;
				}
				close();
				UI.toast('Saved');
				if (opts.onSaved) opts.onSaved(res);
			});
		}

		scrim.addEventListener('click', function (e) {
			if (e.target === scrim) close();
		});
		document.addEventListener('keydown', onKey);
		setStep('listen');
		// force a style pass so the .is-open entrance transition runs
		void scrim.offsetWidth;
		scrim.classList.add('is-open');
		focusStep();
	}

	return { open: open };
})();
