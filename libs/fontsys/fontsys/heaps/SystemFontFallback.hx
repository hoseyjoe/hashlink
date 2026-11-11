package fontsys.heaps;

import fontsys.FontStyle;
import h2d.Font;

/** A glyph rendered once at the authoring size and placed in the atlas. **/
private class AuthoredGlyph {
	public var tile:h2d.Tile;
	public var left:Int;
	public var top:Int;
	public var advance:Float;
	/** Authored at COLOR_EM on a colour page (an emoji), not at EM as an SDF. **/
	public var isColor:Bool;

	public function new(tile, left, top, advance, isColor = false) {
		this.tile = tile;
		this.left = left;
		this.top = top;
		this.advance = advance;
		this.isColor = isColor;
	}
}

/** Authored glyphs (and misses) for one (source, atlas) pair, shared by every fallback using that pair. **/
private class GlyphCache {
	public var source:GlyphSource;
	public var atlas:FallbackAtlas;

	/** styleKey -> glyph, or null for a miss (no font, render failure, or atlas full). **/
	public var glyphs = new Map<String, Null<AuthoredGlyph>>();

	public function new(source, atlas) {
		this.source = source;
		this.atlas = atlas;
	}
}

/**
	h2d.Font fallback that draws characters missing from a Heaps font from installed OS fonts,
	rendered on demand as SDF glyphs (em 32, spread 8 — the same 0.25 spread-to-em ratio as the
	apps' own SDF fonts) into a FallbackAtlas.

	Each (code, language, style) is rendered and placed once per (source, atlas) pair, however many
	fonts or sizes use it; Font.getChar caches the per-font scaled FontChar in that font's glyphs.
	Misses (no font covers it, or the atlas is full) are cached too, so they cost one lookup.

	Emoji come in colour when the OS font for them has colour glyphs: rendered once at COLOR_EM
	onto the atlas's colour pages, returned as FontChar.isColor glyphs (h2d.Text draws them
	untinted). As a ClusterFallback it also draws whole emoji sequences (skin tones, ZWJ families,
	flags, keycaps) as the one glyph HarfBuzz joins them into.
**/
class SystemFontFallback implements FontFallback implements ClusterFallback {
	public static inline var EM = 32;
	public static inline var SPREAD = 8;
	/** Pixels per em colour glyphs are rendered at; scaled to the font size like SDF glyphs. **/
	public static inline var COLOR_EM = 64;

	static var sharedSource:SystemGlyphSource;
	static var caches:Array<GlyphCache> = [];

	/**
		BCP 47 language tag picking Han forms ("zh-Hans", "zh-Hant", "ja", "ko", ...).
		"" (or null) means the OS's own language (on Android, the device locale; see
		fontsys.h). Changing it affects only lookups not made yet.
	**/
	public var language:String = "en";

	public var source(default, null):GlyphSource;

	var base:Font;
	var style:FontStyle;
	var cache:GlyphCache;

	/**
		@param base The unresized font (BitmapFont.toFont()); supplies the size and baseline that
			fallback glyphs are aligned to.
		@param style Weight / italic / monospace asked of the OS. Read by value at each lookup.
		@param atlas Where rendered glyphs are placed.
		@param source Glyph provider; defaults to one SystemGlyphSource shared by all fallbacks.
	**/
	public function new(base:Font, style:FontStyle, atlas:FallbackAtlas, ?source:GlyphSource) {
		this.base = base;
		this.style = style != null ? style : FontStyle.REGULAR;
		if (source == null) {
			if (sharedSource == null)
				sharedSource = new SystemGlyphSource();
			source = sharedSource;
		}
		this.source = source;
		cache = cacheFor(source, atlas);
	}

	static function cacheFor(source:GlyphSource, atlas:FallbackAtlas):GlyphCache {
		// Drop caches whose atlas is gone (their tiles point at freed pages).
		caches = [for (c in caches) if (!@:privateAccess c.atlas.disposed) c];
		for (c in caches)
			if (c.source == source && c.atlas == atlas)
				return c;
		var c = new GlyphCache(source, atlas);
		caches.push(c);
		return c;
	}

	/** Cache key for a glyph request; FontStyle is keyed by value, not identity. **/
	public static function styleKey(code:Int, lang:String, style:FontStyle):String {
		return code + "|" + (lang == null ? "" : lang) + "|" + style.weight + (style.italic ? "i" : "") + (style.mono ? "m" : "");
	}

	public function resolveGlyph(font:Font, code:Int):Null<FontChar> {
		// Lone surrogates (HL strings are UTF-16) and out-of-range values are never characters.
		if (code < 0 || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF))
			return null;
		var g = authoredColor([code]);
		if (g == null)
			g = authored(code);
		return g == null ? null : fontChar(font, g);
	}

	/**
		An emoji sequence (`emoji`) as one colour glyph, or for a text-style cluster (ending in
		U+FE0E) its first character as a plain SDF glyph; null when it cannot be drawn as one.
	**/
	public function resolveCluster(font:Font, cluster:String, emoji:Bool):Null<FontChar> {
		var codes = [for (c in new haxe.iterators.StringIteratorUnicode(cluster)) c];
		if (codes.length == 0)
			return null;
		var g = emoji ? authoredColor(codes) : authored(codes[0]);
		return g == null ? null : fontChar(font, g);
	}

	/** `g` scaled to `font`'s size and sat on its baseline. **/
	function fontChar(font:Font, g:AuthoredGlyph):FontChar {
		var size = Math.abs(font.size);
		var k = size / (g.isColor ? COLOR_EM : EM);
		var dx = g.left * k;
		var dy = base.baseLine * size / Math.abs(base.size) - g.top * k;
		var t = g.tile.sub(0, 0, g.tile.width, g.tile.height, dx, dy);
		t.scaleToSize(g.tile.width * k, g.tile.height * k);
		var c = new FontChar(t, g.advance * k);
		c.isColor = g.isColor;
		return c;
	}

	function authoredColor(codes:Array<Int>):Null<AuthoredGlyph> {
		var key = "c|" + codes.join(",") + "|" + styleKey(0, language, style);
		if (cache.glyphs.exists(key))
			return cache.glyphs.get(key);
		var g:AuthoredGlyph = null;
		var cg = source.colorGlyph(codes, language, style, COLOR_EM);
		if (cg != null) {
			var tile = cache.atlas.placeColor(cg);
			if (tile != null)
				g = new AuthoredGlyph(tile, cg.left, cg.top, cg.advance, true);
		}
		cache.glyphs.set(key, g);
		return g;
	}

	function authored(code:Int):Null<AuthoredGlyph> {
		var key = styleKey(code, language, style);
		if (cache.glyphs.exists(key))
			return cache.glyphs.get(key);
		var g:AuthoredGlyph = null;
		var sdf = source.glyph(code, language, style, EM, SPREAD);
		if (sdf != null) {
			var tile = cache.atlas.place(sdf);
			if (tile != null)
				g = new AuthoredGlyph(tile, sdf.left, sdf.top, sdf.advance);
		}
		cache.glyphs.set(key, g);
		return g;
	}
}
