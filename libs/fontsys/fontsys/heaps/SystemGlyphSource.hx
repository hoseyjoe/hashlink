package fontsys.heaps;

import fontsys.ColorGlyph;
import fontsys.FontStyle;
import fontsys.FontSys;
import fontsys.SdfGlyph;

/**
	GlyphSource backed by installed OS fonts: FontSys.find -> openFace -> renderSdf (or, for colour
	glyphs, shapeCluster -> renderColor). Font lookups are cached by (code, lang, style), so the SDF
	and colour paths share one OS call; face handles are cached by (path, index); misses are cached
	so a character no installed font covers costs one OS lookup.
**/
class SystemGlyphSource implements GlyphSource {
	/** Asked for the emoji font when a sequence's first character comes from a text font ("1️⃣", "©️"). **/
	static inline var EMOJI_PROBE = 0x1F600;

	var sys:FontSys;
	var faces = new Map<String, Int>();
	var finds = new Map<String, Null<{path:String, index:Int}>>();
	var misses = new Map<String, Bool>();

	/** The font file the most recent successful `glyph` or `colorGlyph` came from (a test hook). **/
	public var lastFacePath(default, null):Null<String>;

	/** Uses `sys` if given, otherwise creates its own FontSys. **/
	public function new(?sys:FontSys) {
		this.sys = sys != null ? sys : new FontSys();
	}

	public function glyph(code:Int, lang:String, style:FontStyle, em:Int, spread:Int):Null<SdfGlyph> {
		if (style == null)
			style = FontStyle.REGULAR;
		var key = SystemFontFallback.styleKey(code, lang, style) + "|" + em + "|" + spread;
		if (misses.exists(key))
			return null;
		var g = lookup(code, lang, style, em, spread);
		if (g == null)
			misses.set(key, true);
		return g;
	}

	public function colorGlyph(codes:Array<Int>, lang:String, style:FontStyle, em:Int):Null<ColorGlyph> {
		if (codes == null || codes.length == 0)
			return null;
		if (style == null)
			style = FontStyle.REGULAR;
		var key = "c|" + codes.join(",") + "|" + SystemFontFallback.styleKey(0, lang, style) + "|" + em;
		if (misses.exists(key))
			return null;
		var g = lookupColor(codes, lang, style, em);
		if (g == null)
			misses.set(key, true);
		return g;
	}

	function lookupColor(codes:Array<Int>, lang:String, style:FontStyle, em:Int):Null<ColorGlyph> {
		var found = find(codes[0], lang, style);
		var face = found == null ? -1 : open(found);
		if ((face < 0 || !sys.faceIsColor(face)) && codes.length > 1) {
			// A text font has the first character (a digit, ©, ❤): the sequence is the emoji font's.
			found = find(EMOJI_PROBE, lang, style);
			face = found == null ? -1 : open(found);
		}
		if (face < 0 || !sys.faceIsColor(face))
			return null;
		var glyph = sys.shapeCluster(face, codes);
		if (glyph == 0)
			return null;
		var g = sys.renderColor(face, glyph, em);
		if (g != null)
			lastFacePath = found.path;
		return g;
	}

	function find(code:Int, lang:String, style:FontStyle):Null<{path:String, index:Int}> {
		var key = SystemFontFallback.styleKey(code, lang, style);
		if (finds.exists(key))
			return finds.get(key);
		var found = sys.find(code, lang, style);
		finds.set(key, found);
		return found;
	}

	function open(found:{path:String, index:Int}):Int {
		var faceKey = found.index + "|" + found.path;
		var face = faces.get(faceKey);
		if (face == null) {
			face = sys.openFace(found.path, found.index);
			faces.set(faceKey, face);
			// Cached, so this logs once per (path, index). -2: the face cache is full
			// (FontSys keeps at most 32 faces), so no new font file resolves from now on.
			if (face < 0)
				trace("fontsys: openFace(" + found.path + " #" + found.index + ") failed: " + face
					+ (face == -2 ? " (face cache full)" : ""));
		}
		return face;
	}

	function lookup(code:Int, lang:String, style:FontStyle, em:Int, spread:Int):Null<SdfGlyph> {
		var found = find(code, lang, style);
		if (found == null)
			return null;
		var face = open(found);
		if (face < 0)
			return null;
		var g = sys.renderSdf(face, code, em, spread);
		if (g != null)
			lastFacePath = found.path;
		return g;
	}
}
