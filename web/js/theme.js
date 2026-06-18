(function () {
	var saved = null;
	try { saved = localStorage.getItem('blaster-theme'); } catch (e) {}
	document.documentElement.setAttribute('data-theme', saved === 'light' ? 'light' : 'dark');
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

	var fadeTimer = null;

	function init() {
		setTheme(current(), false);
		document.querySelectorAll('[data-theme-toggle]').forEach(function (btn) {
			btn.addEventListener('click', function () {
				var root = document.documentElement;
				// held just past base.css's 0.35s palette transition
				root.classList.add('theme-fade');
				clearTimeout(fadeTimer);
				fadeTimer = setTimeout(function () { root.classList.remove('theme-fade'); }, 400);
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

// scrollbars hide when idle; capture phase because scroll events do not bubble
(function () {
	var scrollTimer = null;
	document.addEventListener('scroll', function () {
		var root = document.documentElement;
		root.classList.add('is-scrolling');
		clearTimeout(scrollTimer);
		scrollTimer = setTimeout(function () { root.classList.remove('is-scrolling'); }, 1000);
	}, { capture: true, passive: true });
})();
