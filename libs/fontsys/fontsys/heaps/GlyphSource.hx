package fontsys.heaps;

import fontsys.ColorGlyph;
import fontsys.FontStyle;
import fontsys.SdfGlyph;

/** Where SystemFontFallback gets the glyphs it places in its atlas. **/
interface GlyphSource {
	/**
		`code` rendered as an SDF at `em` pixels per em with a `spread`-pixel distance range,
		for the BCP 47 language `lang` and `style`, or null if nothing can draw it.
	**/
	function glyph(code:Int, lang:String, style:FontStyle, em:Int, spread:Int):Null<SdfGlyph>;

	/**
		The code points `codes` (one character, or an emoji sequence) rendered in colour as one
		glyph at `em` pixels per em, from an OS colour (emoji) font, or null if no colour font
		draws them as one glyph.
	**/
	function colorGlyph(codes:Array<Int>, lang:String, style:FontStyle, em:Int):Null<ColorGlyph>;
}
