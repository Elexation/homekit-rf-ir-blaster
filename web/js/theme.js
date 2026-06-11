(function () {
	try {
		var saved = localStorage.getItem('blaster-theme');
		if (saved === 'dark' || saved === 'light') {
			document.documentElement.setAttribute('data-theme', saved);
		}
	} catch (e) {}
})();

(function () {
	function current() {
		return document.documentElement.getAttribute('data-theme') === 'dark' ? 'dark' : 'light';
	}

	function setTheme(theme, persist) {
		document.documentElement.setAttribute('data-theme', theme);
		if (persist) {
			try { localStorage.setItem('blaster-theme', theme); } catch (e) {}
		}
		var label = theme === 'dark' ? 'Dark' : 'Light';
		var iconRef = theme === 'dark' ? '#i-moon' : '#i-sun';
		document.querySelectorAll('[data-theme-label]').forEach(function (el) {
			el.textContent = label;
		});
		document.querySelectorAll('[data-theme-icon]').forEach(function (el) {
			el.setAttribute('href', iconRef);
		});
	}

	function init() {
		setTheme(current(), false);
		document.querySelectorAll('[data-theme-toggle]').forEach(function (btn) {
			btn.addEventListener('click', function () {
				setTheme(current() === 'dark' ? 'light' : 'dark', true);
			});
		});
	}

	if (document.readyState === 'loading') {
		document.addEventListener('DOMContentLoaded', init);
	} else {
		init();
	}
})();
