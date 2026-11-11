package fontsys.heaps;

import fontsys.FontStyle;
import fontsys.FontSys;
import fontsys.SdfGlyph;

/**
	GlyphSource backed by installed OS fonts: FontSys.find -> openFace -> renderSdf.
	Face handles are cached by (path, index); misses are cached by (code, lang, style, em, spread)
	so a character no installed font covers costs one OS lookup.
**/
class SystemGlyphSource implements GlyphSource {
	var sys:FontSys;
	var faces = new Map<String, Int>();
	var misses = new Map<String, Bool>();

	/** The font file the most recent successful `glyph` came from (a test hook). **/
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

	function lookup(code:Int, lang:String, style:FontStyle, em:Int, spread:Int):Null<SdfGlyph> {
		var found = sys.find(code, lang, style);
		if (found == null)
			return null;
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
		if (face < 0)
			return null;
		var g = sys.renderSdf(face, code, em, spread);
		if (g != null)
			lastFacePath = found.path;
		return g;
	}
}
