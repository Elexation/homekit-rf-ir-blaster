// First-run setup. The one-time code is printed to serial at boot; the device
// re-checks the 8-char password rule as the authority.

(function () {
	'use strict';

	var Data = window.BlasterData;
	var MIN_PASSWORD = 8;

	var form = document.querySelector('[data-setup]');
	var nonceEl = document.getElementById('auth-nonce');
	var passEl = document.getElementById('auth-password');
	var confirmEl = document.getElementById('auth-confirm');
	var nonceErr = document.querySelector('[data-nonce-err]');
	var passErr = document.querySelector('[data-password-err]');
	var confirmErr = document.querySelector('[data-confirm-err]');
	var submitBtn = form.querySelector('[data-submit]');

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

	Data.getCsrf().then(function (token) {
		form.querySelector('[data-csrf]').value = token;
	});

	form.addEventListener('submit', function (e) {
		e.preventDefault();
		var ok = true;
		var nonce = nonceEl.value.trim();
		if (!nonce) {
			showError(nonceEl, nonceErr, 'Enter the one-time code.');
			ok = false;
		} else {
			showError(nonceEl, nonceErr, null);
		}
		if (passEl.value.length < MIN_PASSWORD) {
			showError(passEl, passErr, 'Use at least ' + MIN_PASSWORD + ' characters.');
			ok = false;
		} else {
			showError(passEl, passErr, null);
		}
		if (confirmEl.value !== passEl.value) {
			showError(confirmEl, confirmErr, 'Passwords don’t match.');
			ok = false;
		} else {
			showError(confirmEl, confirmErr, null);
		}
		if (!ok) return;

		submitBtn.disabled = true;
		Data.setup(passEl.value, nonce, form.querySelector('[data-csrf]').value).then(function (res) {
			if (res.ok) {
				location.href = 'dashboard.html';
				return;
			}
			submitBtn.disabled = false;
			showError(nonceEl, nonceErr, res.error === 'nonce'
				? 'That code doesn’t match. Re-check the web setup code from setup.'
				: 'Setup failed. Try again.');
		});
	});

	nonceEl.focus();
})();
