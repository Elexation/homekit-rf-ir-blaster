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
				carrierHz: c.carrierHz, rolling: c.rolling, pulses: c.pulses
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

	return {
		serviceIcon: serviceIcon,
		serviceLabel: serviceLabel,
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
		confirmDialog: confirmDialog
	};
})();
