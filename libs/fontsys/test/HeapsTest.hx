import fontsys.FontStyle;
import fontsys.SdfGlyph;
import fontsys.heaps.FallbackAtlas;
import fontsys.heaps.GlyphSource;
import fontsys.heaps.SystemFontFallback;
import fontsys.heaps.SystemGlyphSource;

/** Returns a fixed 40x44 glyph (left -8, top 30, advance 32) for U+4E2D, null otherwise; counts calls. **/
class FakeSource implements GlyphSource {
	public var calls = 0;
	public var lastLang:String;
	public var codes:Array<Int> = [];

	public function new() {}

	public function glyph(code:Int, lang:String, style:FontStyle, em:Int, spread:Int):Null<SdfGlyph> {
		calls++;
		lastLang = lang;
		codes.push(code);
		if (code != 0x4E2D)
			return null;
		var g = new SdfGlyph();
		g.width = 40;
		g.height = 44;
		g.left = -8;
		g.top = 30;
		g.advance = 32;
		g.pixels = haxe.io.Bytes.alloc(40 * 44);
		g.pixels.fill(0, 40 * 44, 200);
		return g;
	}
}

/** FallbackAtlas test; see heaps.hxml for the run command. **/
class HeapsTest extends hxd.App {
	static var failures = 0;
	var frame = 0;

	// State carried between frames by the one-frame-later upload case.
	var upAtlas:FallbackAtlas;

	// State carried between frames by the h2d.Text case (Review Focus 3).
	var text:h2d.Text;
	var textFont:h2d.Font;
	var textAtlas:FallbackAtlas;

	static function main() {
		new HeapsTest();
	}

	static function check(ok:Bool, msg:String) {
		if (!ok) {
			failures++;
			Sys.println("FAIL: " + msg);
		}
	}

	static function glyph(w:Int, h:Int, v:Int):SdfGlyph {
		var g = new SdfGlyph();
		g.width = w;
		g.height = h;
		g.left = 0;
		g.top = h;
		g.advance = w;
		g.pixels = haxe.io.Bytes.alloc(w * h);
		g.pixels.fill(0, w * h, v);
		return g;
	}

	static function overlap(a:h2d.Tile, b:h2d.Tile):Bool {
		// Tiles on different textures never overlap; on the same one keep a 1 px gutter.
		if (a.getTexture() != b.getTexture())
			return false;
		var ax = Std.int(a.x), ay = Std.int(a.y), bx = Std.int(b.x), by = Std.int(b.y);
		var aw = Std.int(a.width), ah = Std.int(a.height), bw = Std.int(b.width), bh = Std.int(b.height);
		return ax < bx + bw + 1 && bx < ax + aw + 1 && ay < by + bh + 1 && by < ay + ah + 1;
	}

	static function near(a:Float, b:Float, msg:String) {
		check(Math.abs(a - b) < 1e-6, '$msg (got $a, want $b)');
	}

	/**
		A stand-in for BitmapFont.toFont(): an SDF font (alpha channel) at size 48, baseline 38,
		on a 64x64 page with A and B as 30x30 sub-tiles whose bottom sits 12 px (the em 48 /
		spread 12 padding) below the baseline, and a distinct defaultChar.
	**/
	static function makeFont():h2d.Font {
		var tex = new h3d.mat.Texture(64, 64);
		var page = h2d.Tile.fromTexture(tex);
		var f = @:privateAccess new h2d.Font("stand-in", 48, SignedDistanceField(Alpha, 0.5, -1));
		@:privateAccess {
			f.baseLine = 38;
			f.lineHeight = 58;
			f.tile = page;
			f.glyphs.set("A".code, new h2d.Font.FontChar(page.sub(0, 0, 30, 30, 0, 20), 30));
			f.glyphs.set("B".code, new h2d.Font.FontChar(page.sub(32, 0, 30, 30, 0, 20), 30));
			f.defaultChar = new h2d.Font.FontChar(page.sub(0, 32, 20, 30, 0, 20), 20);
		}
		return f;
	}

	static function defaultChar(f:h2d.Font):h2d.Font.FontChar {
		return @:privateAccess f.defaultChar;
	}

	/** SystemFontFallback with a fake source: metrics, scaling, caches, surrogates, language. **/
	static function fallbackFakeTests() {
		var atlas = new FallbackAtlas(256, 1);
		var src = new FakeSource();
		var font = makeFont();
		var fb = new SystemFontFallback(font, FontStyle.REGULAR, atlas, src);
		check(fb.source == src, 'source is the one passed in');
		check(fb.language == "en", 'language defaults to "en" (got ${fb.language})');
		font.fallback = fb;

		// Size 48: k = 48/32 = 1.5.
		var c = font.getChar(0x4E2D);
		check(c != defaultChar(font), 'U+4E2D resolved through the fallback');
		near(c.t.width, 60, 'tile width at 48');
		near(c.t.height, 66, 'tile height at 48');
		near(c.t.dx, -8 * 1.5, 'dx at 48');
		near(c.t.dy, 38 - 30 * 1.5, 'dy at 48');
		near(c.width, 32 * 1.5, 'advance at 48');
		check(c.t.getTexture() != font.tile.getTexture(), 'fallback glyph is on the atlas page');
		check(src.calls == 1, 'source called once for the first lookup (got ${src.calls})');
		check(font.getChar(0x4E2D) == c, 'second lookup returns the cached FontChar');
		check(src.calls == 1, 'same code twice in one font: source called once (got ${src.calls})');

		// A second font sharing the fallback, resized to 24 before the lookup: half of everything.
		var font24 = makeFont();
		font24.fallback = fb;
		font24.resizeTo(24);
		var h = font24.getChar(0x4E2D);
		check(h != defaultChar(font24), 'U+4E2D resolved in the 24 px font');
		near(h.t.width, 30, 'tile width at 24');
		near(h.t.height, 33, 'tile height at 24');
		near(h.t.dx, -8 * 1.5 / 2, 'dx at 24');
		near(h.t.dy, (38 - 30 * 1.5) / 2, 'dy at 24');
		near(h.width, 32 * 1.5 / 2, 'advance at 24');
		check(src.calls == 1, 'second font: source still called once (got ${src.calls})');
		check(h.t.getTexture() == c.t.getTexture() && h.t.x == c.t.x && h.t.y == c.t.y, 'both fonts share one atlas slot');

		// A font resized to 24 after resolving at 48 agrees.
		var clone = font.clone();
		clone.resizeTo(24);
		var r = clone.getChar(0x4E2D);
		near(r.t.width, 30, 'resized clone tile width');
		near(r.t.dx, -6, 'resized clone dx');
		near(r.t.dy, -3.5, 'resized clone dy');
		near(r.width, 24, 'resized clone advance');
		check(src.calls == 1, 'clone: source still called once (got ${src.calls})');

		// Unknown code point: defaultChar, and the miss is cached.
		for (i in 0...3)
			check(font.getChar(0x10FFFD) == defaultChar(font), 'U+10FFFD gives defaultChar (try $i)');
		check(src.calls == 2, 'unknown code asked 3 times: source called once (got ${src.calls - 1})');

		// Review Focus 4: lone surrogates and out-of-range codes never reach the source.
		check(font.getChar(0xD83D) == defaultChar(font), 'U+D83D gives defaultChar');
		check(font.getChar(0xDFFF) == defaultChar(font), 'U+DFFF gives defaultChar');
		check(font.getChar(0x110000) == defaultChar(font), '0x110000 gives defaultChar');
		check(src.calls == 2, 'surrogates and out-of-range codes never reach the source (got ${src.calls})');

		// Language: only new lookups use it.
		fb.language = "ja";
		font.getChar(0x4E2E);
		check(src.lastLang == "ja", 'source receives "ja" (got ${src.lastLang})');
		var before = src.calls;
		check(font.getChar(0x4E2D) == c, 'cached U+4E2D unchanged after the language change');
		check(src.calls == before, 'cached U+4E2D not asked again');
		check(src.codes.indexOf(0x4E2D) == src.codes.lastIndexOf(0x4E2D), 'U+4E2D asked exactly once overall');

		// Atlas full: resolves to defaultChar and is not rendered again.
		var full = new FallbackAtlas(32, 1); // 40x44 never fits
		var src2 = new FakeSource();
		var f2 = makeFont();
		f2.fallback = new SystemFontFallback(f2, FontStyle.REGULAR, full, src2);
		for (i in 0...3)
			check(f2.getChar(0x4E2D) == defaultChar(f2), 'glyph that does not fit gives defaultChar (try $i)');
		check(src2.calls == 1, 'glyph that does not fit is rendered once (got ${src2.calls})');
		full.dispose();
		atlas.dispose();
	}

	/** Review Focus 5: a 1-page, 256 px atlas and 100 CJK code points through the real source. **/
	static function fallbackCapTest() {
		var atlas = new FallbackAtlas(256, 1);
		var font = makeFont();
		var fb = new SystemFontFallback(font, FontStyle.REGULAR, atlas);
		fb.language = "zh-Hans";
		font.fallback = fb;
		var placed = 0, defaults = 0;
		var oldTrace = haxe.Log.trace;
		haxe.Log.trace = function(v:Dynamic, ?p) {};
		try {
			for (i in 0...100) {
				var c = font.getChar(0x4E00 + i);
				if (c == defaultChar(font))
					defaults++;
				else {
					placed++;
					check(c.t.getTexture() != font.tile.getTexture(), 'U+${StringTools.hex(0x4E00 + i)} on the atlas');
				}
			}
		} catch (e:Dynamic) {
			check(false, 'exception while filling a capped atlas: $e');
		}
		haxe.Log.trace = oldTrace;
		check(placed > 0, 'some of the 100 glyphs fit (placed $placed)');
		check(defaults > 0, 'the rest give defaultChar (defaults $defaults)');
		check(placed + defaults == 100, 'every code point resolved ($placed + $defaults)');
		check(atlas.pageCount == 1, 'capped atlas has 1 page (got ${atlas.pageCount})');
		Sys.println('cap test: $placed placed, $defaults defaultChar');
		atlas.dispose();
	}

	function textChecks() {
		var a = textFont.getChar("A".code);
		var b = textFont.getChar("B".code);
		var zh = textFont.getChar(0x4E2D);
		check(zh != defaultChar(textFont), 'U+4E2D resolved from an OS font');
		var src = cast(cast(textFont.fallback, SystemFontFallback).source, SystemGlyphSource);
		Sys.println('U+4E2D from: ${src.lastFacePath}');

		// Texture runs recorded by the TileGroup's BatchDrawState: head .. tail inclusive.
		var st = @:privateAccess text.glyphs.content.state;
		var texs:Array<h3d.mat.Texture> = [];
		@:privateAccess {
			var e = st.head;
			while (e != null) {
				texs.push(e.texture);
				if (e == st.tail)
					break;
				e = e.next;
			}
		}
		check(texs.length == 3, '"A中B" has 3 state entries (got ${texs.length})');
		if (texs.length == 3) {
			check(texs[0] == a.t.getTexture(), 'entry 1 is the font page');
			check(texs[1] == zh.t.getTexture(), 'entry 2 is the atlas page');
			check(texs[2] == b.t.getTexture(), 'entry 3 is the font page');
			check(texs[0] != texs[1], 'atlas page differs from the font page');
		}

		var ab = text.calcTextWidth("AB");
		check(text.textWidth > ab, 'width of "A中B" (${text.textWidth}) > width of "AB" ($ab)');
		var aBottom = a.t.dy + a.t.height, zhBottom = zh.t.dy + zh.t.height;
		check(Math.abs(zhBottom - aBottom) <= 0.2 * 48, 'U+4E2D bottom $zhBottom within 0.2 em of A bottom $aBottom');
		Sys.println('A bottom $aBottom, U+4E2D bottom $zhBottom (dy ${zh.t.dy}, h ${zh.t.height}, w ${zh.t.width}), advance ${zh.width}');

		// An emoji (a surrogate pair, then U+FE0F) is one glyph, and on Windows a real one: Segoe UI
		// Emoji's monochrome outline. Elsewhere it may be the box (Android's emoji font is bitmaps).
		var et = new h2d.Text(textFont, s2d);
		et.text = "A\u{1F600}\u{FE0F}B";
		@:privateAccess et.updateSize();
		var count = @:privateAccess et.glyphs.count();
		check(count == 3, '"A😀️B" draws 3 glyphs (got $count)');
		var grin = textFont.getChar(0x1F600);
		if (grin != defaultChar(textFont))
			Sys.println('U+1F600 from: ${src.lastFacePath} (w ${grin.t.width}, h ${grin.t.height}, advance ${grin.width})');
		if (Sys.systemName() == "Windows")
			check(grin != defaultChar(textFont), 'U+1F600 resolved from an OS font');
		et.remove();
		textAtlas.dispose();
	}

	override function init() {
		hxd.System.allowTimeout = false;

		fallbackFakeTests();
		fallbackCapTest();

		// Review Focus 3: h2d.Text mixing page / atlas / page glyphs, real source.
		textAtlas = new FallbackAtlas(1024, 2);
		textFont = makeFont();
		var tfb = new SystemFontFallback(textFont, FontStyle.REGULAR, textAtlas);
		tfb.language = "zh-Hans";
		textFont.fallback = tfb;
		text = new h2d.Text(textFont, s2d);
		text.text = "A中B";

		// Upload batching: placing in one frame uploads nothing yet; one flush later, exactly once.
		upAtlas = new FallbackAtlas(1024, 2);
		for (i in 0...50)
			check(upAtlas.place(glyph(40, 40, 200)) != null, 'batch glyph $i placed');
		check(upAtlas.uploads == 0, 'no upload straight after place (got ${upAtlas.uploads})');

		// No overlap, with gutter.
		var a = new FallbackAtlas(1024, 2);
		var tiles = [for (i in 0...30) a.place(glyph(20 + i % 7, 18 + i % 5, 90))];
		var bad = 0;
		for (i in 0...tiles.length) {
			check(tiles[i] != null, 'tile $i placed');
			check(tiles[i].dx == 0 && tiles[i].dy == 0, 'tile $i has dx/dy 0');
			for (j in i + 1...tiles.length)
				if (overlap(tiles[i], tiles[j]))
					bad++;
		}
		check(bad == 0, 'tiles overlap or lack gutter ($bad pairs)');
		a.dispose();

		// Page cap, and the single "full" log.
		var logs:Array<String> = [];
		var oldTrace = haxe.Log.trace;
		haxe.Log.trace = function(v:Dynamic, ?p) logs.push(Std.string(v));
		var c = new FallbackAtlas(256, 2);
		var placed = [];
		for (i in 0...8)
			placed.push(c.place(glyph(100, 100, 77)));
		check(c.pageCount == 2, 'two pages after 8 glyphs (got ${c.pageCount})');
		check(placed[3] != null && placed[3].getTexture() == placed[0].getTexture(), '4th glyph still on page 1');
		check(placed[4].getTexture() != placed[0].getTexture(), '5th glyph on page 2');
		check(c.place(glyph(100, 100, 77)) == null, '9th glyph returns null');
		check(c.place(glyph(100, 100, 77)) == null, '10th glyph returns null');
		check(c.place(glyph(100, 100, 77)) == null, '11th glyph returns null');
		haxe.Log.trace = oldTrace;
		var fullLogs = [for (l in logs) if (l.indexOf("FallbackAtlas: full (2 pages)") >= 0) l];
		check(fullLogs.length == 1, 'full logged exactly once (got ${fullLogs.length}: $logs)');
		check(c.place(glyph(300, 10, 1)) == null, 'glyph bigger than a page returns null');

		// Pixel readback and context loss.
		var d = new FallbackAtlas(256, 2);
		var t = d.place(glyph(30, 30, 173));
		d.flush();
		check(d.uploads == 1, 'explicit flush uploads one page (got ${d.uploads})');
		d.flush();
		check(d.uploads == 1, 'clean flush uploads nothing');
		var tex = t.getTexture();
		var cx = Std.int(t.x) + 15, cy = Std.int(t.y) + 15;
		var px = tex.capturePixels().getPixel(cx, cy);
		check((px >>> 24) == 173, 'alpha at glyph centre is 173 (got ${px >>> 24})');
		check(((px >> 16) & 0xFF) == 255 && ((px >> 8) & 0xFF) == 255 && (px & 0xFF) == 255, 'RGB is 255 (got ${StringTools.hex(px & 0xFFFFFF, 6)})');

		// Context loss: dispose the GPU texture; the next use runs realloc.
		tex.dispose();
		var px2 = tex.capturePixels().getPixel(cx, cy);
		check((px2 >>> 24) == 173, 'alpha after context loss is 173 (got ${px2 >>> 24})');
		check(((px2 >> 16) & 0xFF) == 255, 'RGB after context loss is 255');

		// Placing on a page that is already on the GPU is visible at once (no flush, no frame).
		var e = new FallbackAtlas(256, 2);
		var e1 = e.place(glyph(30, 30, 100));
		e.flush();
		var e2 = e.place(glyph(30, 30, 222));
		var ep = e1.getTexture().capturePixels().getPixel(Std.int(e2.x) + 15, Std.int(e2.y) + 15);
		check((ep >>> 24) == 222, 'glyph placed on a live page is readable at once (got ${ep >>> 24})');
		var ep1 = e1.getTexture().capturePixels().getPixel(Std.int(e1.x) + 15, Std.int(e1.y) + 15);
		check((ep1 >>> 24) == 100, 'earlier glyph survives the re-upload (got ${ep1 >>> 24})');
		var z = e.place(glyph(0, 0, 0));
		check(z != null && z.width == 0 && z.height == 0, 'zero-size glyph gets a 0x0 tile');
		e.dispose();

		// dispose: later place returns null.
		d.dispose();
		check(d.place(glyph(10, 10, 1)) == null, 'place after dispose returns null');
		c.dispose();
	}

	override function update(dt:Float) {
		frame++;
		if (frame == 2) {
			check(upAtlas.uploads == 1, 'one upload a frame later (got ${upAtlas.uploads})');
			check(upAtlas.pageCount == 1, 'batch used one page');
			upAtlas.dispose();
			textChecks();
			if (failures == 0)
				Sys.println("HeapsTest: all passed");
			else
				Sys.println('HeapsTest: $failures failure(s)');
			Sys.exit(failures == 0 ? 0 : 1);
		}
	}
}
