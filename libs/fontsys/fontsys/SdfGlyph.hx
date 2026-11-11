package fontsys;

/**
	A glyph rendered by fontsys.FontSys.renderSdf: a single-channel signed
	distance field, 128 = edge, larger = inside, spread padding included.
**/
class SdfGlyph {
	/** Bitmap size in pixels. **/
	public var width:Int;

	public var height:Int;

	/** Bitmap origin: pixels right of the pen. **/
	public var left:Int;

	/** Bitmap origin: pixels above the baseline. **/
	public var top:Int;

	/** Horizontal advance in pixels. **/
	public var advance:Float;

	/** width * height bytes, row-major. **/
	public var pixels:haxe.io.Bytes;

	public function new() {}
}
