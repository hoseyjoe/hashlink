package fontsys;

/**
	A glyph rendered by fontsys.FontSys.renderColor: an emoji in colour, RGBA with straight (not
	premultiplied) alpha.
**/
class ColorGlyph {
	/** Bitmap size in pixels. **/
	public var width:Int;

	public var height:Int;

	/** Bitmap origin: pixels right of the pen. **/
	public var left:Int;

	/** Bitmap origin: pixels above the baseline. **/
	public var top:Int;

	/** Horizontal advance in pixels. **/
	public var advance:Float;

	/** width * height * 4 bytes, R G B A, row-major. **/
	public var pixels:haxe.io.Bytes;

	public function new() {}
}
