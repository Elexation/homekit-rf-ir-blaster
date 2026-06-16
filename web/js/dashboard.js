// Tiles are manage-only: control lives in HomeKit, so a tile opens the device
// page rather than firing a code.

(function () {
	'use strict';

	var UI = window.BlasterUI;

	var state = { config: null, rev: 0, status: null };

	function fillHeader(cfg, status) {
		var meta = document.querySelector('[data-meta]');
		if (meta) {
			var parts = [];
			if (cfg.settings.canonicalDomain) parts.push(cfg.settings.canonicalDomain);
			parts.push(status.ip, 'Wi-Fi ' + status.wifi);
			meta.textContent = parts.join(' · ');
		}

		var hk = document.querySelector('[data-homekit]');
		if (hk) hk.innerHTML = UI.icon('i-home', 15) + 'HomeKit · ' + UI.escapeHtml(status.pairing);
	}

	function renderStats(cfg) {
		var devices = cfg.devices.length, commands = 0, ir = 0, rf = 0;
		cfg.devices.forEach(function (d) {
			UI.commandList(d).forEach(function (c) {
				commands++;
				if (c.kind === 'ir') ir++;
				else if (c.kind === 'rf') rf++;
			});
		});
		var stats = [
			{ n: devices, label: devices === 1 ? 'Device' : 'Devices' },
			{ n: commands, label: 'Commands' },
			{ n: ir, label: 'IR codes' },
			{ n: rf, label: 'RF codes' }
		];
		document.querySelector('[data-stats]').innerHTML = stats.map(function (s) {
			return '<div class="card stat"><div class="stat__num">' + s.n +
				'</div><div class="stat__label">' + s.label + '</div></div>';
		}).join('');
	}

	function deviceTile(d) {
		var learned = UI.learnedCount(d);
		var count = UI.commandCount(d);
		var foot = learned > 0
			? UI.deviceProtos(d).map(function (p) {
				return '<span class="proto-chip">' + UI.escapeHtml(p) + '</span>';
			}).join('')
			: '<span class="pill pill--quiet">Not learned</span>';
		return '<a class="card device-card" href="device.html?id=' + encodeURIComponent(d.id) + '">' +
			'<div class="device-card__row">' +
				'<div class="badge' + (learned > 0 ? ' is-active' : '') + '">' +
					UI.icon(UI.serviceIcon(d.service), 19) + '</div>' +
				'<div class="device-card__grow">' +
					'<div class="device-card__name">' + UI.escapeHtml(d.name) + '</div>' +
					'<div class="device-card__proto">' + UI.escapeHtml(UI.serviceLabel(d.service)) + '</div>' +
				'</div>' +
			'</div>' +
			'<div class="device-card__foot">' +
				'<span class="device-card__count">' + count + (count === 1 ? ' command' : ' commands') + '</span>' +
				'<span class="device-card__chips">' + foot + '</span>' +
			'</div>' +
		'</a>';
	}

	function homeCard(status) {
		return '<div class="card home-card">' +
			'<h2 class="section-title">Apple Home</h2>' +
			'<div class="home-card__pair"><span class="pill pill--good"><span class="pill__dot"></span>' +
				UI.escapeHtml(status.pairing) + '</span></div>' +
			'<div class="home-card__instr">Open the Home app, tap <strong>Add Accessory</strong>, and enter this setup code.</div>' +
			'<div class="home-card__code">' + UI.escapeHtml(status.setupCode) + '</div>' +
		'</div>';
	}

	function learnCta() {
		return '<button class="card learn-cta" data-action="learn">' +
			'<span class="learn-cta__badge">' + UI.icon('i-plus', 19) + '</span>' +
			'<span class="learn-cta__grow">' +
				'<span class="learn-cta__title">Learn a code</span>' +
				'<span class="learn-cta__sub">Point a remote at the device</span>' +
			'</span>' +
		'</button>';
	}

	function renderMain(cfg, status) {
		var main = document.querySelector('[data-main]');
		if (cfg.devices.length === 0) {
			main.innerHTML =
				'<div class="card"><div class="empty">' +
					'<div class="empty__badge">' + UI.icon('i-signal', 34) + '</div>' +
					'<div><div class="empty__title">No devices yet</div>' +
					'<div class="empty__text">Point any remote at the device and press a button. ' +
						'The code is captured and turned into a device you control from the Home app.</div></div>' +
					'<button class="btn btn--primary empty__cta" data-action="learn">' +
						UI.icon('i-plus', 17) + 'Learn your first code</button>' +
				'</div></div>' +
				'<aside class="dash__side">' + homeCard(status) + '</aside>';
		} else {
			main.innerHTML =
				'<div><h2 class="section-title">Devices</h2>' +
					'<div class="device-grid">' + cfg.devices.map(deviceTile).join('') + '</div></div>' +
				'<aside class="dash__side">' + learnCta() + homeCard(status) + '</aside>';
		}
	}

	function wireLearn() {
		document.body.addEventListener('click', function (e) {
			var t = e.target.closest ? e.target.closest('[data-action="learn"]') : null;
			if (!t) return;
			window.BlasterLearn.open({
				config: state.config,
				rev: state.rev,
				onSaved: function (res) {
					state.config = res.config;
					state.rev = res.rev;
					renderStats(state.config);
					renderMain(state.config, state.status);
				}
			});
		});
	}

	function wireLogout() {
		document.querySelector('[data-logout]').addEventListener('click', function () {
			window.BlasterData.logout().then(function () { location.href = 'login.html'; });
		});
	}

	Promise.all([window.BlasterData.getConfig(), window.BlasterData.getStatus()])
		.then(function (r) {
			state.config = r[0].config;
			state.rev = r[0].rev;
			state.status = r[1];
			fillHeader(state.config, state.status);
			renderStats(state.config);
			renderMain(state.config, state.status);
			wireLearn();
			wireLogout();
		});
})();
