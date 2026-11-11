package fontsys.heaps;

import fontsys.FontStyle;
import fontsys.SdfGlyph;

/** Where SystemFontFallback gets the SDF glyphs it places in its atlas. **/
interface GlyphSource {
	/**
		`code` rendered as an SDF at `em` pixels per em with a `spread`-pixel distance range,
		for the BCP 47 language `lang` and `style`, or null if nothing can draw it.
	**/
	function glyph(code:Int, lang:String, style:FontStyle, em:Int, spread:Int):Null<SdfGlyph>;
}
