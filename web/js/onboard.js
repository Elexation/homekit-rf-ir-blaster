// First-boot setup wizard over the open AP: Wi-Fi -> pairing code -> done (QR + codes).
// Wi-Fi is verified live (device STA-connects while the page polls); advances on success.

(function () {
	'use strict';

	var SUBS = {
		wifi: 'Let’s get your device online.',
		code: 'Pick the code you’ll use to pair in Home.',
		done: 'Save these, then reconnect.'
	};
	var TRIVIAL = {
		'00000000': 1, '11111111': 1, '22222222': 1, '33333333': 1, '44444444': 1,
		'55555555': 1, '66666666': 1, '77777777': 1, '88888888': 1, '99999999': 1,
		'12345678': 1, '87654321': 1
	};

	var steps = {};
	document.querySelectorAll('[data-step]').forEach(function (el) {
		steps[el.getAttribute('data-step')] = el;
	});
	var dots = document.querySelectorAll('[data-dot]');
	var subEl = document.querySelector('[data-sub]');

	var ssidEl = document.getElementById('ob-ssid');
	var passEl = document.getElementById('ob-pass');
	var codeEl = document.getElementById('ob-code');
	var ssidErr = document.querySelector('[data-ssid-err]');
	var passErr = document.querySelector('[data-pass-err]');
	var codeErr = document.querySelector('[data-code-err]');
	var finishBtn = document.querySelector('[data-finish]');
	var regenBtn = document.querySelector('[data-regen]');
	var rebootBtn = document.querySelector('[data-reboot]');
	var rebootingEl = document.querySelector('[data-rebooting]');
	var wifiBtn = document.querySelector('[data-wifi-btn]');
	var testingEl = document.querySelector('[data-testing]');

	var state = { ssid: '', password: '' };

	function show(name, idx) {
		Object.keys(steps).forEach(function (k) { steps[k].hidden = (k !== name); });
		dots.forEach(function (d, i) { d.classList.toggle('is-active', i <= idx); });
		if (subEl && SUBS[name]) subEl.textContent = SUBS[name];
	}

	function err(el, errEl, msg) {
		if (msg) {
			el.classList.add('is-invalid');
			errEl.textContent = msg;
			errEl.hidden = false;
		} else {
			el.classList.remove('is-invalid');
			errEl.hidden = true;
		}
	}

	function post(url, fields) {
		var opts = { method: 'POST' };
		if (fields) {
			opts.headers = { 'Content-Type': 'application/x-www-form-urlencoded' };
			opts.body = Object.keys(fields).map(function (k) {
				return encodeURIComponent(k) + '=' + encodeURIComponent(fields[k]);
			}).join('&');
		}
		return fetch(url, opts).then(function (r) { return r.json(); });
	}

	function fmtCode(c) {
		return /^\d{8}$/.test(c) ? c.slice(0, 3) + '-' + c.slice(3, 5) + '-' + c.slice(5) : c;
	}

	// Build the QR as an SVG with attribute-only styling so it stays CSP-clean.
	function renderQr(text) {
		var box = document.querySelector('[data-qr]');
		box.textContent = '';
		var qr = qrcode(0, 'M');
		qr.addData(text);
		qr.make();
		var n = qr.getModuleCount();
		var pad = 2;  // quiet zone in modules
		var dim = n + pad * 2;
		var ns = 'http://www.w3.org/2000/svg';
		var svg = document.createElementNS(ns, 'svg');
		svg.setAttribute('viewBox', '0 0 ' + dim + ' ' + dim);
		svg.setAttribute('class', 'ob-qr__svg');
		var bg = document.createElementNS(ns, 'rect');
		bg.setAttribute('x', '0');
		bg.setAttribute('y', '0');
		bg.setAttribute('width', String(dim));
		bg.setAttribute('height', String(dim));
		bg.setAttribute('class', 'ob-qr__bg');
		svg.appendChild(bg);
		for (var r = 0; r < n; r++) {
			for (var c = 0; c < n; c++) {
				if (!qr.isDark(r, c)) continue;
				var m = document.createElementNS(ns, 'rect');
				m.setAttribute('x', String(c + pad));
				m.setAttribute('y', String(r + pad));
				m.setAttribute('width', '1');
				m.setAttribute('height', '1');
				m.setAttribute('class', 'ob-qr__m');
				svg.appendChild(m);
			}
		}
		box.appendChild(svg);
	}

	// Fill the SSID datalist from the device's boot scan; typing still works for hidden/late SSIDs.
	function loadNetworks() {
		var dl = document.getElementById('ob-networks');
		if (!dl) return;
		fetch('/api/onboard/networks').then(function (r) { return r.json(); }).then(function (d) {
			if (!d || !Array.isArray(d.networks)) return;
			d.networks.forEach(function (name) {
				var o = document.createElement('option');
				o.value = name;  // createElement, not innerHTML: SSIDs are untrusted broadcast
				dl.appendChild(o);
			});
		}).catch(function () { /* no suggestions; the field still accepts typing */ });
	}

	function suggestCode() {
		fetch('/api/onboard/state').then(function (r) { return r.json(); }).then(function (d) {
			if (d && d.code) codeEl.value = d.code;
		}).catch(function () { /* leave the field for manual entry */ });
	}

	function setTesting(on) {
		wifiBtn.disabled = on;
		ssidEl.disabled = on;
		passEl.disabled = on;
		testingEl.hidden = !on;
	}

	// The connect test drops STA, so the AP/page blips; poll through fetch failures until a result.
	function pollVerify(deadline) {
		fetch('/api/onboard/verify-status').then(function (r) { return r.json(); }).then(function (d) {
			if (d && d.state === 'ok') {
				setTesting(false);
				suggestCode();
				show('code', 1);
				codeEl.focus();
			} else if (d && d.state === 'fail') {
				setTesting(false);
				err(passEl, passErr, 'Couldn’t connect. Check the network and password.');
			} else if (Date.now() > deadline) {
				setTesting(false);
				err(passEl, passErr, 'Timed out. Move closer to the router and try again.');
			} else {
				setTimeout(function () { pollVerify(deadline); }, 900);
			}
		}).catch(function () {
			if (Date.now() > deadline) {
				setTesting(false);
				err(passEl, passErr, 'Lost the setup network. Rejoin it and try again.');
			} else {
				setTimeout(function () { pollVerify(deadline); }, 1300);
			}
		});
	}

	steps.wifi.addEventListener('submit', function (e) {
		e.preventDefault();
		var ssid = ssidEl.value.trim();
		var pass = passEl.value;
		var ok = true;
		if (!ssid) { err(ssidEl, ssidErr, 'Enter your Wi-Fi network name.'); ok = false; }
		else err(ssidEl, ssidErr, null);
		if (pass && (pass.length < 8 || pass.length > 63)) {
			err(passEl, passErr, '8 to 63 characters, or blank for an open network.');
			ok = false;
		} else err(passEl, passErr, null);
		if (!ok) return;
		state.ssid = ssid;
		state.password = pass;
		setTesting(true);
		post('/api/onboard/verify', { ssid: ssid, password: pass }).then(function (res) {
			if (res && res.ok) { pollVerify(Date.now() + 40000); return; }
			setTesting(false);
			if (res && res.error === 'password') err(passEl, passErr, 'Wi-Fi password looks invalid.');
			else err(ssidEl, ssidErr, 'Wi-Fi name looks invalid.');
		}).catch(function () {
			pollVerify(Date.now() + 40000);  // the test may have already dropped us; poll anyway
		});
	});

	codeEl.addEventListener('input', function () {
		codeEl.value = codeEl.value.replace(/\D/g, '').slice(0, 8);
	});
	regenBtn.addEventListener('click', suggestCode);

	// Finish is idempotent; the AP can blip during SRP keygen, so retry through drops.
	function attemptFinish(code, triesLeft) {
		post('/api/onboard/finish', { ssid: state.ssid, password: state.password, code: code })
			.then(function (res) {
				finishBtn.disabled = false;
				if (res && res.ok) {
					if (res.qr) renderQr(res.qr);
					document.querySelector('[data-show-code]').textContent = fmtCode(res.code || code);
					document.querySelector('[data-show-nonce]').textContent = res.nonce || '';
					document.querySelector('[data-show-ip]').textContent =
						res.ip ? 'https://' + res.ip : 'find it on your router';
					show('done', 2);
					return;
				}
				var m = 'Setup failed. Try again.';
				if (res && res.error === 'code') m = 'That code is too simple. Try another.';
				else if (res && res.error === 'ssid') m = 'Wi-Fi name looks invalid.';
				else if (res && res.error === 'password') m = 'Wi-Fi password looks invalid.';
				err(codeEl, codeErr, m);
			})
			.catch(function () {
				if (triesLeft > 0) {
					setTimeout(function () { attemptFinish(code, triesLeft - 1); }, 1500);
					return;
				}
				finishBtn.disabled = false;
				err(codeEl, codeErr, 'Couldn’t reach the device. Try again.');
			});
	}

	steps.code.addEventListener('submit', function (e) {
		e.preventDefault();
		var code = codeEl.value;
		if (!/^\d{8}$/.test(code)) { err(codeEl, codeErr, 'Enter exactly 8 digits.'); return; }
		if (TRIVIAL[code]) { err(codeEl, codeErr, 'That code is too simple. Try another.'); return; }
		err(codeEl, codeErr, null);
		finishBtn.disabled = true;
		attemptFinish(code, 3);
	});

	rebootBtn.addEventListener('click', function () {
		rebootBtn.disabled = true;
		rebootingEl.hidden = false;
		post('/api/onboard/reboot');  // device restarts; this response may not arrive
	});

	show('wifi', 0);
	loadNetworks();
	ssidEl.focus();
})();
