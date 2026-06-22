// Settings page; validation mirrors the config layer, and saving the web-access form restarts.

(function () {
	'use strict';

	var UI = window.BlasterUI;
	var Data = window.BlasterData;

	var RESERVED_PORTS = { 1201: 'HomeKit', 3232: 'updates', 5353: 'mDNS' };
	var MAX_DOMAIN_LEN = 253;
	var BOOLS = ['https', 'trustedProxy'];

	// Hostname: dot-separated labels, each 1-63 of [a-z0-9-], no leading/trailing hyphen.
	var HOST_RE = /^(?=.{1,253}$)([a-zA-Z0-9](?:[a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?)(\.[a-zA-Z0-9](?:[a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?)*$/;

	// Import bounds, mirroring the config layer so a bad file is refused before sending.
	var MAX_BYTES = Data.MAX_CONFIG_BYTES;
	var MAX_DEVICES = 32;
	var MAX_PULSES = 512;
	var MAX_REPEAT = 10;
	var MAX_DELAY = 5000;
	var SERVICES = { Switch: 1, WindowCovering: 1, Outlet: 1, LightBulb: 1, Fan: 1, Television: 1 };

	var state = { rev: 0, config: null, pending: null };

	function setToggle(btn, on) {
		btn.classList.toggle('is-on', on);
		btn.setAttribute('aria-checked', on ? 'true' : 'false');
	}

	function toggleState(name) {
		var btn = document.querySelector('[data-toggle="' + name + '"]');
		return btn.getAttribute('aria-checked') === 'true';
	}

	function populate(cfg, status) {
		var domain = cfg.settings.canonicalDomain || '';
		document.querySelector('[data-meta]').textContent = domain || status.ip;
		BOOLS.forEach(function (k) {
			setToggle(document.querySelector('[data-toggle="' + k + '"]'), !!cfg.settings[k]);
		});
		document.getElementById('set-port').value = cfg.settings.listenPort;

		setToggle(document.querySelector('[data-toggle="useDomain"]'), !!domain);
		document.querySelector('[data-domain-field]').hidden = !domain;
		document.getElementById('set-domain').value = domain;

		// absent defaults to on; only explicit false turns it off
		setToggle(document.querySelector('[data-toggle="ledEnabled"]'), cfg.settings.ledEnabled !== false);

		['ip', 'setupCode', 'pairing'].forEach(function (f) {
			document.querySelector('[data-field="' + f + '"]').textContent = status[f];
		});
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

	function validate() {
		var portEl = document.getElementById('set-port');
		var domainEl = document.getElementById('set-domain');
		var portErr = document.querySelector('[data-port-err]');
		var domainErr = document.querySelector('[data-domain-err]');
		var ok = true;

		var port = parseInt(portEl.value, 10);
		if (!(port >= 1 && port <= 65535)) {
			showError(portEl, portErr, 'Port must be between 1 and 65535.');
			ok = false;
		} else if (RESERVED_PORTS[port]) {
			showError(portEl, portErr, 'Port ' + port + ' is reserved for ' + RESERVED_PORTS[port] + '.');
			ok = false;
		} else {
			showError(portEl, portErr, null);
		}

		var domain = '';
		if (toggleState('useDomain')) {
			domain = domainEl.value.trim();
			if (!domain) {
				showError(domainEl, domainErr, 'Enter a domain, or turn this off.');
				ok = false;
			} else if (domain.length > MAX_DOMAIN_LEN) {
				showError(domainEl, domainErr, 'Domain is too long (max ' + MAX_DOMAIN_LEN + ').');
				ok = false;
			} else if (!HOST_RE.test(domain)) {
				showError(domainEl, domainErr, 'Enter a valid hostname, e.g. blaster.local.');
				ok = false;
			} else {
				showError(domainEl, domainErr, null);
			}
		} else {
			showError(domainEl, domainErr, null);
		}

		return ok ? { port: port, domain: domain } : null;
	}

	function save() {
		var v = validate();
		if (!v) return;
		UI.confirmDialog({
			title: 'Save and restart?',
			confirmLabel: 'Save & restart'
		}).then(function (ok) {
			if (!ok) return;
			var settings = {
				https: toggleState('https'),
				listenPort: v.port,
				trustedProxy: toggleState('trustedProxy'),
				canonicalDomain: v.domain,
				ledEnabled: toggleState('ledEnabled')  // carry it so a restart save doesn't reset it
			};
			Data.saveSettings(settings, state.rev).then(function (res) {
				if (res.conflict) {
					UI.toast('Settings changed in another tab. Reloading…');
					setTimeout(function () { location.reload(); }, 1200);
					return;
				}
				state.rev = res.rev;
				state.config.settings = settings;
				UI.toast('Saved. The device restarts to apply.');
			});
		});
	}

	// Built from last-saved settings, not the form, so it doesn't pick up unsaved network edits.
	function saveLed(on) {
		var s = state.config.settings;
		var settings = {
			https: s.https,
			listenPort: s.listenPort,
			trustedProxy: s.trustedProxy,
			canonicalDomain: s.canonicalDomain,
			ledEnabled: on
		};
		Data.saveSettings(settings, state.rev).then(function (res) {
			if (res.conflict) {
				UI.toast('Settings changed in another tab. Reloading…');
				setTimeout(function () { location.reload(); }, 1200);
				return;
			}
			state.rev = res.rev;
			state.config.settings = settings;
			UI.toast(on ? 'Status light on.' : 'Status light off.');
		});
	}

	// Export is compact JSON (matches the firmware) so it round-trips under the byte ceiling.

	function exportFile() {
		var json = JSON.stringify(state.config);
		var blob = new Blob([json], { type: 'application/json' });
		var url = URL.createObjectURL(blob);
		var a = document.createElement('a');
		a.href = url;
		a.download = 'remote-config.json';
		document.body.appendChild(a);
		a.click();
		document.body.removeChild(a);
		URL.revokeObjectURL(url);
		UI.toast('Configuration exported.');
	}

	function validateImport(text, bytes) {
		var issues = [];
		if (bytes > MAX_BYTES) {
			return { ok: false, issues: ['File is too large (' + bytes + ' bytes; the limit is ' + MAX_BYTES + ').'] };
		}
		var cfg;
		try { cfg = JSON.parse(text); } catch (e) {
			return { ok: false, issues: ['File is not valid JSON.'] };
		}
		if (!cfg || typeof cfg !== 'object' || Array.isArray(cfg)) {
			return { ok: false, issues: ['The top level of the file must be a configuration object.'] };
		}

		var devices = cfg.devices;
		if (devices !== undefined && !Array.isArray(devices)) {
			issues.push('"devices" must be a list.');
			devices = [];
		}
		devices = devices || [];
		if (devices.length > MAX_DEVICES) {
			issues.push('Too many devices (' + devices.length + '; the limit is ' + MAX_DEVICES + ').');
		}

		var seenIds = {}, maxId = 1;
		devices.forEach(function (d) {
			var label = UI.escapeHtml(d && d.name ? d.name : ('id ' + (d && d.id)));
			if (!d || typeof d !== 'object') { issues.push('A device entry is not an object.'); return; }
			if (typeof d.id !== 'number' || !Number.isInteger(d.id) || d.id < 1) {
				issues.push('Device "' + label + '" has an invalid id (must be a whole number).');
			} else if (d.id === 1) {
				issues.push('Device "' + label + '" uses id 1, which is reserved for the bridge.');
			} else {
				if (seenIds[d.id]) issues.push('Two devices share id ' + d.id + '.');
				seenIds[d.id] = 1;
				if (d.id > maxId) maxId = d.id;
			}
			if (!SERVICES[d.service]) {
				issues.push('Device "' + label + '" has an unknown type "' + UI.escapeHtml(d.service) + '".');
			}
			var cmds = d.commands || {};
			Object.keys(cmds).forEach(function (name) {
				var c = cmds[name], cl = UI.escapeHtml(name);
				var learned = c && c.kind && c.kind !== 'none';
				var pulses = (c && c.pulses) || [];
				if (learned && pulses.length === 0) issues.push('Command "' + cl + '" on "' + label + '" is marked learned but has no pulses.');
				if (pulses.length % 2 !== 0) issues.push('Command "' + cl + '" on "' + label + '" has an odd pulse count.');
				if (pulses.length > MAX_PULSES) issues.push('Command "' + cl + '" on "' + label + '" has too many pulses (limit ' + MAX_PULSES + ').');
				if (c && c.kind === 'rf' && c.freqMHz !== 315 && c.freqMHz !== 433) {
					issues.push('Command "' + cl + '" on "' + label + '" has an invalid RF frequency.');
				}
				if (c && c.repeatCount !== undefined &&
					(typeof c.repeatCount !== 'number' || !Number.isInteger(c.repeatCount) ||
					c.repeatCount < 1 || c.repeatCount > MAX_REPEAT)) {
					issues.push('Command "' + cl + '" on "' + label + '" has an invalid repeat count.');
				}
				if (c && c.repeatDelayMs !== undefined &&
					(typeof c.repeatDelayMs !== 'number' || !Number.isInteger(c.repeatDelayMs) ||
					c.repeatDelayMs < 0 || c.repeatDelayMs > MAX_DELAY)) {
					issues.push('Command "' + cl + '" on "' + label + '" has an invalid repeat delay.');
				}
			});
		});

		if (typeof cfg.nextDeviceId === 'number' && cfg.nextDeviceId <= maxId) {
			issues.push('"nextDeviceId" must be greater than every device id.');
		}

		var s = cfg.settings;
		if (s && typeof s === 'object') {
			if (s.listenPort !== undefined) {
				var p = s.listenPort;
				if (typeof p !== 'number' || p < 1 || p > 65535) issues.push('Listen port is out of range.');
				else if (RESERVED_PORTS[p]) issues.push('Listen port ' + p + ' is reserved for ' + RESERVED_PORTS[p] + '.');
			}
			if (typeof s.canonicalDomain === 'string' && s.canonicalDomain) {
				if (s.canonicalDomain.length > MAX_DOMAIN_LEN) issues.push('Domain is too long.');
				else if (!HOST_RE.test(s.canonicalDomain)) issues.push('Domain is not a valid hostname.');
			}
		}

		if (issues.length) return { ok: false, issues: issues };
		return { ok: true, config: cfg };
	}

	function renderResult(res) {
		var box = document.querySelector('[data-import-result]');
		if (!res.ok) {
			state.pending = null;
			box.innerHTML = '<div class="bk-result__title is-bad">' + UI.icon('i-trash', 16) + 'Can’t import this file</div>' +
				'<ul class="bk-issues">' + res.issues.map(function (m) { return '<li>' + m + '</li>'; }).join('') + '</ul>';
			return;
		}
		state.pending = res.config;
		box.innerHTML = '<div class="bk-lead">Overwrites all current devices, codes, and settings.</div>' +
			'<button class="btn btn--primary" data-do-import>' + UI.icon('i-upload', 16) + 'Restore &amp; replace</button>';
		box.querySelector('[data-do-import]').addEventListener('click', doImport);
	}

	function doImport() {
		if (!state.pending) return;
		Data.replaceConfig(state.pending, state.rev).then(function (res) {
			if (res.conflict) {
				UI.toast('Config changed in another tab. Reloading…');
				setTimeout(function () { location.reload(); }, 1200);
				return;
			}
			UI.toast('Configuration imported.');
			setTimeout(function () { location.href = 'dashboard.html'; }, 900);
		});
	}

	function onFile(e) {
		var file = e.target.files && e.target.files[0];
		if (!file) return;
		document.querySelector('[data-file-label]').textContent = file.name;
		var reader = new FileReader();
		reader.onload = function () { renderResult(validateImport(String(reader.result), file.size)); };
		reader.onerror = function () { renderResult({ ok: false, issues: ['Could not read the file.'] }); };
		reader.readAsText(file);
	}

	function wire() {
		document.querySelectorAll('[data-toggle]').forEach(function (btn) {
			btn.addEventListener('click', function () {
				var on = btn.getAttribute('aria-checked') !== 'true';
				setToggle(btn, on);
				var name = btn.getAttribute('data-toggle');
				if (name === 'useDomain') {
					document.querySelector('[data-domain-field]').hidden = !on;
					if (on) document.getElementById('set-domain').focus();
				} else if (name === 'ledEnabled') {
					saveLed(on);
				}
			});
		});
		document.querySelector('[data-save]').addEventListener('click', save);
		document.querySelector('[data-export]').addEventListener('click', exportFile);
		document.getElementById('bk-input').addEventListener('change', onFile);
		document.querySelector('[data-factory]').addEventListener('click', function () {
			UI.confirmDialog({
				title: 'Factory reset?',
				message: 'Erases all devices, codes, and HomeKit pairing. This cannot be undone.',
				confirmLabel: 'Erase everything',
				danger: true
			}).then(function (ok) {
				if (!ok) return;
				Data.factoryReset().then(function () {
					UI.toast('Factory reset complete.');
					setTimeout(function () { location.href = 'dashboard.html'; }, 900);
				});
			});
		});
	}

	Promise.all([Data.getConfig(), Data.getStatus()]).then(function (r) {
		state.rev = r[0].rev;
		state.config = r[0].config;
		populate(r[0].config, r[1]);
		wire();
	});
})();
