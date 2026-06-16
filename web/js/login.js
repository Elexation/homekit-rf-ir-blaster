// Sign-in page.

(function () {
	'use strict';

	var Data = window.BlasterData;

	var form = document.querySelector('[data-login]');
	var passEl = document.getElementById('auth-password');
	var errEl = document.querySelector('[data-password-err]');
	var submitBtn = form.querySelector('[data-submit]');

	function showError(msg) {
		if (msg) {
			passEl.classList.add('is-invalid');
			errEl.textContent = msg;
			errEl.hidden = false;
		} else {
			passEl.classList.remove('is-invalid');
			errEl.hidden = true;
		}
	}

	function lockedMsg(retryMs) {
		var min = Math.max(1, Math.ceil((retryMs || 0) / 60000));
		return 'Too many attempts. Try again in ' + min + (min === 1 ? ' minute.' : ' minutes.');
	}

	Data.getCsrf().then(function (token) {
		form.querySelector('[data-csrf]').value = token;
	});

	form.addEventListener('submit', function (e) {
		e.preventDefault();
		if (!passEl.value) {
			showError('Enter the password.');
			return;
		}
		showError(null);
		submitBtn.disabled = true;
		Data.login(passEl.value, form.querySelector('[data-csrf]').value).then(function (res) {
			if (res.ok) {
				location.href = 'dashboard.html';
				return;
			}
			submitBtn.disabled = false;
			showError(res.error === 'locked' ? lockedMsg(res.retryMs) : 'Wrong password.');
		});
	});

	passEl.focus();
})();
