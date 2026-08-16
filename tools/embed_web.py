#!/usr/bin/env python3
"""Bundle web/ into src/web_assets.h as gzipped PROGMEM byte arrays plus a manifest.

Per page, the stylesheet run and body-script run are each concatenated into one
bundle and the tags rewritten in memory (on-disk web/ is never mutated), cutting
parallel fetches to fit the device's lwip socket cap (shared with HAP). theme.js
stays separate (runs in <head> before render). woff2 is stored raw; gzip uses
mtime 0 so output is stable.

Every non-HTML asset is referenced with a ?v=<content hash> stamp and marked
immutable, which is what lets the device serve it with a one-year Cache-Control
(new content produces a new URL, so a firmware update can never serve a stale
asset). HTML is never stamped and never cacheable: it carries the stamped refs.
"""

import gzip
import hashlib
import re
from pathlib import Path

# SCons execs pre-scripts without __file__; there the project root comes from env
try:
	Import("env")  # noqa: F821
	ROOT = Path(env["PROJECT_DIR"])  # noqa: F821
except NameError:
	ROOT = Path(__file__).resolve().parent.parent
WEB_DIR = ROOT / "web"
OUT_PATH = ROOT / "src" / "web_assets.h"

# Bundled under web/ for source-level licensing, never served.
SKIP_NAMES = {"OFL.txt"}

# extension -> (content type, gzip); an unmapped extension is a hard error.
TYPES = {
	".html": ("text/html; charset=utf-8", True),
	".css": ("text/css; charset=utf-8", True),
	".js": ("application/javascript; charset=utf-8", True),
	".svg": ("image/svg+xml", True),
	".woff2": ("font/woff2", False),
}

# Each page's stylesheet links and body scripts form one consecutive run;
# theme.js (<head>) is excluded so it stays standalone.
CSS_BLOCK = re.compile(r'(?:[ \t]*<link rel="stylesheet" href="styles/[^"]+">\r?\n)+')
JS_BLOCK = re.compile(r'(?:[ \t]*<script src="js/(?!theme\.js)[^"]+"></script>\r?\n)+')
CSS_REF = re.compile(r'href="(styles/[^"]+)"')
JS_REF = re.compile(r'src="(js/[^"]+)"')


def ident(url_path):
	parts = [p for chunk in url_path.split("/") for p in chunk.replace("-", ".").split(".") if p]
	return "k" + "".join(p[:1].upper() + p[1:] for p in parts)


def sha8(data):
	return hashlib.sha256(data).hexdigest()[:8]


def stamp_html_refs(text, stamps):
	"""Stamp a page's own refs, which are root-relative with no leading slash."""
	for url, mark in stamps.items():
		ref = url.lstrip("/")
		text = text.replace(f'"{ref}"', f'"{ref}{mark}"')
	return text


def stamp_css_refs(text, stamps):
	"""Stamp url() refs inside a bundle served from styles/, so they reach up one level."""
	for url, mark in stamps.items():
		ref = ".." + url
		text = text.replace(f"'{ref}'", f"'{ref}{mark}'").replace(f'"{ref}"', f'"{ref}{mark}"')
	return text


def byte_lines(data):
	lines = []
	for i in range(0, len(data), 16):
		lines.append("\t" + " ".join(f"0x{b:02X}," for b in data[i:i + 16]))
	return "\n".join(lines)


def make_bundle(text, block_re, ref_re, rel_url, tag_tmpl, sep, rewrite=None):
	"""Replace a tag run with one stamped bundle tag; return (text, consumed, asset)."""
	m = block_re.search(text)
	if not m:
		return text, [], None
	paths = [(WEB_DIR / r).resolve() for r in ref_re.findall(m.group(0))]
	body = sep.join(p.read_text(encoding="utf-8") for p in paths)
	if rewrite:
		body = rewrite(body)
	raw = body.encode("utf-8")
	tag = tag_tmpl.format(url=f"{rel_url}?v={sha8(raw)}")
	return text[:m.start()] + tag + "\n" + text[m.end():], paths, ("/" + rel_url, raw, True)


def collect():
	"""Return an ordered list of (url, raw_bytes, immutable) for every served asset.

	Emission order is a dependency order: the font is stamped before the CSS bundles that
	reference it, and the bundles before the HTML that references them.
	"""
	items = []
	consumed = set()
	pages = []
	stamps = {}  # url -> "?v=hash", for every asset another asset links to

	for path in sorted(p for p in WEB_DIR.rglob("*") if p.is_file()):
		if path.name in SKIP_NAMES or path.suffix.lower() in (".html", ".css", ".js"):
			continue
		url = "/" + path.relative_to(WEB_DIR).as_posix()
		raw = path.read_bytes()
		stamps[url] = f"?v={sha8(raw)}"
		items.append((url, raw, True))

	for html in sorted(WEB_DIR.glob("*.html")):
		text = html.read_text(encoding="utf-8")
		stem = html.stem
		text, css_paths, css_bundle = make_bundle(
			text, CSS_BLOCK, CSS_REF, f"styles/{stem}.bundle.css",
			'<link rel="stylesheet" href="{url}">', "\n",
			lambda body: stamp_css_refs(body, stamps))
		text, js_paths, js_bundle = make_bundle(
			text, JS_BLOCK, JS_REF, f"js/{stem}.bundle.js",
			'<script src="{url}"></script>', "\n;\n")
		if css_bundle is None or js_bundle is None:
			raise SystemExit(f"{html.name}: expected a stylesheet run and a body-script run to bundle")
		pages.append(("/" + html.relative_to(WEB_DIR).as_posix(), text))
		consumed.update(css_paths)
		consumed.update(js_paths)
		items.append(css_bundle)
		items.append(js_bundle)

	# Code that survived bundling (theme.js runs in <head>, so it is never in a body run).
	for path in sorted(p for p in WEB_DIR.rglob("*") if p.is_file()):
		if path.name in SKIP_NAMES or path.resolve() in consumed:
			continue
		if path.suffix.lower() not in (".css", ".js"):
			continue
		url = "/" + path.relative_to(WEB_DIR).as_posix()
		raw = path.read_bytes()
		stamps[url] = f"?v={sha8(raw)}"
		items.append((url, raw, True))

	for url, text in pages:
		items.append((url, stamp_html_refs(text, stamps).encode("utf-8"), False))
	return items


def build():
	items = collect()
	if not items:
		raise SystemExit(f"no assets found under {WEB_DIR}")
	assets = []
	for url, raw, immutable in items:
		ext = "." + url.rsplit(".", 1)[-1].lower()
		if ext not in TYPES:
			raise SystemExit(f"no content type mapped for {url}; add it to TYPES in tools/embed_web.py")
		ctype, do_gzip = TYPES[ext]
		stored = gzip.compress(raw, 9, mtime=0) if do_gzip else raw
		assets.append((url, ctype, do_gzip, len(raw), stored, immutable))
	assets.sort(key=lambda a: a[0])  # deterministic manifest order

	out = []
	out.append("// Generated by tools/embed_web.py; do not edit.")
	out.append("#pragma once")
	out.append("")
	out.append("#include <cstddef>")
	out.append("#include <cstdint>")
	out.append("")
	out.append("#ifndef PROGMEM")
	out.append("#define PROGMEM")
	out.append("#endif")
	out.append("")
	out.append("namespace web_assets {")
	out.append("")
	out.append("struct Asset {")
	out.append("\tconst char*    path;")
	out.append("\tconst char*    contentType;")
	out.append("\tconst uint8_t* data;")
	out.append("\tsize_t         length;")
	out.append("\tbool           gzipped;    // serve with Content-Encoding: gzip")
	out.append("\tbool           immutable;  // ?v=-stamped URL: safe to cache for a year")
	out.append("};")
	out.append("")
	for url, _, _, _, stored, _ in assets:
		out.append(f"static const uint8_t {ident(url)}[] PROGMEM = {{")
		out.append(byte_lines(stored))
		out.append("};")
		out.append("")
	out.append("static const Asset kAssets[] = {")
	for url, ctype, do_gzip, _, _, immutable in assets:
		gz = "true" if do_gzip else "false"
		im = "true" if immutable else "false"
		out.append(f'\t{{"{url}", "{ctype}", {ident(url)}, sizeof({ident(url)}), {gz}, {im}}},')
	out.append("};")
	out.append("static const size_t kAssetCount = sizeof(kAssets) / sizeof(kAssets[0]);")
	out.append("")
	out.append("}  // namespace web_assets")
	out.append("")
	return "\n".join(out), assets


def main():
	content, assets = build()
	total_raw = sum(a[3] for a in assets)
	total_stored = sum(len(a[4]) for a in assets)
	for url, _, do_gzip, raw_len, stored, immutable in assets:
		note = "gzip" if do_gzip else "raw"
		print(f"{url:32} {raw_len:7} -> {len(stored):7} ({note}{', cached' if immutable else ''})")
	print(f"{'total':32} {total_raw:7} -> {total_stored:7}")
	if OUT_PATH.exists() and OUT_PATH.read_text(encoding="utf-8") == content:
		print(f"{OUT_PATH} unchanged")
	else:
		OUT_PATH.write_text(content, encoding="utf-8", newline="\n")
		print(f"wrote {OUT_PATH}")


# no __main__ guard: PlatformIO execs pre-scripts, so this must run unconditionally;
# errors raise SystemExit, which fails the build in both contexts
main()
