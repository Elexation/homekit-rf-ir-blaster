// Web UI data seam: granular writes merge into the cached config and POST the whole document.

window.BlasterData = (function () {
	'use strict';

	// Mirror the config layer so the import UI can reject before sending.
	var MAX_CONFIG_BYTES = 16384;
	var SCHEMA_VERSION = 1;

	// Merge base before the first server read.
	var DEFAULTS = {
		schemaVersion: SCHEMA_VERSION,
		settings: { https: true, listenPort: 443, trustedProxy: false, canonicalDomain: '', ledEnabled: true },
		nextDeviceId: 2,
		devices: []
	};

	// Cached so granular writes merge into the full doc. csrfToken cached per
	// page load (session-bound); a 403 clears it and retries once.
	var cachedConfig = null;
	var cachedRev = 0;
	var csrfToken = null;
	var learnPolling = false;  // gate for the in-flight learn poll loop; cleared on capture/cancel

	function clone(o) { return JSON.parse(JSON.stringify(o)); }
	function setCache(cfg, rev) { cachedConfig = clone(cfg); cachedRev = rev; }
	function base() { return clone(cachedConfig || DEFAULTS); }

	// Session lapsed: bounce to login, return a never-resolving promise so the
	// .then chain stops here.
	function toLogin() { location.href = 'login.html'; return new Promise(function () {}); }

	function getJson(url) {
		return fetch(url, { credentials: 'same-origin', headers: { Accept: 'application/json' } })
			.then(function (r) {
				if (r.status === 401) return toLogin();
				return r.json();
			});
	}

	function csrf() {
		if (csrfToken) return Promise.resolve(csrfToken);
		return fetch('/api/csrf', { credentials: 'same-origin' })
			.then(function (r) { return r.status === 401 ? toLogin() : r.text(); })
			.then(function (t) { csrfToken = t; return t; });
	}

	// Whole-config write. Resolves {conflict,rev[,config]}. On a backstop reject
	// (client already validated) keep the cached config rather than corrupt state.
	function writeConfig(cfg, baseRev, retried) {
		return csrf().then(function (tok) {
			return fetch('/api/config?rev=' + encodeURIComponent(baseRev), {
				method: 'POST',
				credentials: 'same-origin',
				headers: { 'Content-Type': 'application/json', 'X-CSRF-Token': tok },
				body: JSON.stringify(cfg)
			});
		}).then(function (r) {
			if (r.status === 401) return toLogin();
			if (r.status === 403 && !retried) { csrfToken = null; return writeConfig(cfg, baseRev, true); }
			return r.json().then(function (res) {
				if (res && res.conflict) return res;
				if (res && res.config) { setCache(res.config, res.rev); return res; }
				if (window.console) console.warn('config write rejected:', res && res.error);
				return { conflict: false, rev: cachedRev, config: clone(cachedConfig) };
			});
		});
	}

	function writeMerged(baseRev, mutate) {
		var cfg = base();
		var extra = mutate(cfg);
		return writeConfig(cfg, baseRev).then(function (res) {
			if (extra && res && !res.conflict) {
				Object.keys(extra).forEach(function (k) { res[k] = extra[k]; });
			}
			return res;
		});
	}

	function postCsrf(url, retried) {
		return csrf().then(function (tok) {
			return fetch(url, { method: 'POST', credentials: 'same-origin', headers: { 'X-CSRF-Token': tok } });
		}).then(function (r) {
			if (r.status === 401) return toLogin();
			if (r.status === 403 && !retried) { csrfToken = null; return postCsrf(url, true); }
			return r.json();
		});
	}

	function postForm(url, fields) {
		var body = Object.keys(fields).map(function (k) {
			return encodeURIComponent(k) + '=' + encodeURIComponent(fields[k]);
		}).join('&');
		return fetch(url, {
			method: 'POST',
			credentials: 'same-origin',
			headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
			body: body
		}).then(function (r) { return r.json(); });
	}

	return {
		MAX_CONFIG_BYTES: MAX_CONFIG_BYTES,
		SCHEMA_VERSION: SCHEMA_VERSION,

		getConfig: function () {
			return getJson('/api/config').then(function (r) {
				setCache(r.config, r.rev);
				return { config: clone(r.config), rev: r.rev };
			});
		},

		getStatus: function () { return getJson('/api/status'); },

		findDevice: function (cfg, id) {
			var n = parseInt(id, 10);
			for (var i = 0; i < cfg.devices.length; i++) {
				if (cfg.devices[i].id === n) return cfg.devices[i];
			}
			return null;
		},

		saveSettings: function (settings, baseRev) {
			return writeMerged(baseRev, function (cfg) { cfg.settings = settings; });
		},

		saveDevice: function (id, patch, baseRev) {
			var n = parseInt(id, 10);
			return writeMerged(baseRev, function (cfg) {
				for (var i = 0; i < cfg.devices.length; i++) {
					if (cfg.devices[i].id !== n) continue;
					if (typeof patch.name === 'string') cfg.devices[i].name = patch.name;
					if (patch.commands) cfg.devices[i].commands = patch.commands;
				}
			});
		},

		// id comes from nextDeviceId, which only grows, so deleted ids are never reused.
		addDevice: function (dev, baseRev) {
			var id = null;
			return writeMerged(baseRev, function (cfg) {
				id = cfg.nextDeviceId;
				cfg.nextDeviceId += 1;
				cfg.devices.push({
					id: id, service: dev.service, name: dev.name,
					commands: dev.commands || {}
				});
				return { id: id };
			});
		},

		deleteDevice: function (id, baseRev) {
			var n = parseInt(id, 10);
			return writeMerged(baseRev, function (cfg) {
				cfg.devices = cfg.devices.filter(function (d) { return d.id !== n; });
			});
		},

		// Import path: the page validates for messaging, the device validates as authority.
		replaceConfig: function (cfg, baseRev) {
			return writeConfig(clone(cfg), baseRev);
		},

		factoryReset: function () { return postCsrf('/api/factory-reset'); },

		// getCsrf returns the form token (cookie is HttpOnly); login/setup/logout post it back.
		getCsrf: function () { return csrf(); },

		login: function (password, csrfTok) {
			return postForm('/api/login', { password: password, csrf: csrfTok });
		},

		setup: function (password, nonce, csrfTok) {
			return postForm('/api/setup', { password: password, nonce: nonce, csrf: csrfTok });
		},

		logout: function () {
			return csrf().then(function (tok) { return postForm('/api/logout', { csrf: tok }); });
		},

		// Arm the receiver, then poll for the outcome (the httpd task can't block for the
		// 30 s window). Resolves {ok:true,code} on capture, {ok:false,reason} on failure.
		startLearn: function () {
			learnPolling = true;
			return getJson('/api/learn/start').then(function (res) {
				if (res && res.ok === false) { learnPolling = false; return res; }
				return new Promise(function (resolve) {
					function poll() {
						if (!learnPolling) return;  // cancelled; the modal has already moved on
						fetch('/api/learn/poll', { credentials: 'same-origin', headers: { Accept: 'application/json' } })
							.then(function (r) { return r.status === 401 ? toLogin() : r.json(); })
							.then(function (p) {
								if (!learnPolling) return;
								if (p && (p.ok === true || p.ok === false)) { learnPolling = false; resolve(p); }
								else setTimeout(poll, 400);  // still listening
							})
							.catch(function () { if (learnPolling) setTimeout(poll, 400); });  // transient; keep trying
					}
					poll();
				});
			});
		},

		cancelLearn: function () {
			learnPolling = false;  // stop the poll loop
			return fetch('/api/learn/cancel', { method: 'POST', credentials: 'same-origin' })
				.then(function (r) { return r.status === 401 ? toLogin() : r.json(); });
		}
	};
})();
