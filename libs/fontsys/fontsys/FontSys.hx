package fontsys;

private typedef Ctx = hl.Abstract<"fontsys_hl_ctx">;

/**
	System font lookup and SDF glyph rendering (fontsys.hdll, FreeType).
	Faces are cached by (path, index) and live until dispose().
**/
class FontSys {
	/** Initial native render buffer; grown when a glyph needs more. **/
	static inline var INITIAL_BUFFER = 64 * 64;

	var ctx:Ctx;
	var buf:hl.Bytes;
	var bufCap = 0;

	/**
		Throws if FreeType cannot start, or if fontsys.hdll is missing: the
		natives are bound as an optional library ("?fontsys"), so a missing
		hdll does not stop the program at load time; the first native call
		throws a catchable error instead.
	**/
	public function new() {
		ctx = create();
		if (ctx == null)
			throw "fontsys: cannot initialize FreeType";
	}

	/**
		An installed font covering `code` for the BCP 47 language `lang`
		(e.g. "zh-Hans", "ja") and `style`, or null if none does.
		An empty or null `lang` means the OS's own language (see fontsys.h).
	**/
	public function find(code:Int, lang:String, style:FontStyle):Null<{path:String, index:Int}> {
		if (style == null)
			style = FontStyle.REGULAR;
		var index = 0;
		var p = find_font(ctx, code, lang == null ? null : @:privateAccess lang.toUtf8(), style.weight, style.italic, style.mono, index);
		if (p == null)
			return null;
		return {path: @:privateAccess String.fromUTF8(p), index: index};
	}

	/** A face id (>= 0) for face `index` of the font file at `path`, or < 0 on failure. **/
	public function openFace(path:String, index:Int):Int {
		if (path == null)
			return -1;
		return face_open(ctx, @:privateAccess path.toUtf8(), index);
	}

	/** Whether the face maps `code` to a glyph. **/
	public function faceHas(face:Int, code:Int):Bool {
		return face_has(ctx, face, code);
	}

	/**
		Renders `code` from `face` as an SDF at `em` pixels per em with a
		`spread`-pixel distance range, or null if the face lacks it or
		rendering fails.
	**/
	public function renderSdf(face:Int, code:Int, em:Int, spread:Int):Null<SdfGlyph> {
		if (buf == null) {
			bufCap = INITIAL_BUFFER;
			buf = new hl.Bytes(bufCap);
		}
		var width = 0, height = 0, left = 0, top = 0, advance = 0.;
		var r = render_sdf(ctx, face, code, em, spread, buf, bufCap, width, height, left, top, advance);
		if (r == -1) {
			// Buffer too small: width/height are set, grow and render again.
			var need = width * height;
			while (bufCap < need)
				bufCap <<= 1;
			buf = new hl.Bytes(bufCap);
			r = render_sdf(ctx, face, code, em, spread, buf, bufCap, width, height, left, top, advance);
		}
		if (r != 1)
			return null;
		var g = new SdfGlyph();
		g.width = width;
		g.height = height;
		g.left = left;
		g.top = top;
		g.advance = advance;
		var n = width * height;
		g.pixels = haxe.io.Bytes.alloc(n);
		if (n > 0)
			@:privateAccess g.pixels.b.blit(0, buf, 0, n);
		return g;
	}

	/** Whether the face has colour glyphs (an emoji font: COLR layers or colour bitmaps). **/
	public function faceIsColor(face:Int):Bool {
		return face_is_color(ctx, face);
	}

	/**
		The glyph id HarfBuzz shapes the code points `codes` to in `face` when that is exactly one
		glyph (an emoji sequence the font joins), else 0.
	**/
	public function shapeCluster(face:Int, codes:Array<Int>):Int {
		if (codes == null || codes.length == 0)
			return 0;
		var b = new hl.Bytes(codes.length * 4);
		for (i in 0...codes.length)
			b.setI32(i * 4, codes[i]);
		var glyph = 0;
		return shape_cluster(ctx, face, b, codes.length, glyph) == 1 ? glyph : 0;
	}

	/**
		Renders the glyph id `glyph` of `face` in colour with one em = `em` pixels, or null if it has
		no colour image (an outline glyph, or a bitmap font this build cannot decode).
	**/
	public function renderColor(face:Int, glyph:Int, em:Int):Null<ColorGlyph> {
		if (buf == null) {
			bufCap = INITIAL_BUFFER;
			buf = new hl.Bytes(bufCap);
		}
		var width = 0, height = 0, left = 0, top = 0, advance = 0.;
		var r = render_color(ctx, face, glyph, em, buf, bufCap, width, height, left, top, advance);
		if (r == -1) {
			var need = width * height * 4;
			while (bufCap < need)
				bufCap <<= 1;
			buf = new hl.Bytes(bufCap);
			r = render_color(ctx, face, glyph, em, buf, bufCap, width, height, left, top, advance);
		}
		if (r != 1)
			return null;
		var g = new ColorGlyph();
		g.width = width;
		g.height = height;
		g.left = left;
		g.top = top;
		g.advance = advance;
		var n = width * height * 4;
		g.pixels = haxe.io.Bytes.alloc(n);
		if (n > 0)
			@:privateAccess g.pixels.b.blit(0, buf, 0, n);
		return g;
	}

	/** Frees the native context and every face; the object is unusable afterwards. **/
	public function dispose():Void {
		if (ctx != null) {
			destroy(ctx);
			ctx = null;
		}
		buf = null;
	}

	@:hlNative("?fontsys", "hl_create") static function create():Ctx {
		return null;
	}

	@:hlNative("?fontsys", "hl_destroy") static function destroy(ctx:Ctx):Void {}

	@:hlNative("?fontsys", "hl_find") static function find_font(ctx:Ctx, code:Int, lang:hl.Bytes, weight:Int, italic:Bool, mono:Bool,
		index:hl.Ref<Int>):hl.Bytes {
		return null;
	}

	@:hlNative("?fontsys", "hl_face_open") static function face_open(ctx:Ctx, path:hl.Bytes, index:Int):Int {
		return -1;
	}

	@:hlNative("?fontsys", "hl_face_has") static function face_has(ctx:Ctx, face:Int, code:Int):Bool {
		return false;
	}

	@:hlNative("?fontsys", "hl_face_is_color") static function face_is_color(ctx:Ctx, face:Int):Bool {
		return false;
	}

	@:hlNative("?fontsys", "hl_shape_cluster") static function shape_cluster(ctx:Ctx, face:Int, cps:hl.Bytes, n:Int, glyph:hl.Ref<Int>):Int {
		return -2;
	}

	@:hlNative("?fontsys", "hl_render_color") static function render_color(ctx:Ctx, face:Int, glyph:Int, em:Int, buf:hl.Bytes, bufCap:Int,
		width:hl.Ref<Int>, height:hl.Ref<Int>, left:hl.Ref<Int>, top:hl.Ref<Int>, advance:hl.Ref<Float>):Int {
		return -2;
	}

	@:hlNative("?fontsys", "hl_render_sdf") static function render_sdf(ctx:Ctx, face:Int, code:Int, em:Int, spread:Int, buf:hl.Bytes,
		bufCap:Int, width:hl.Ref<Int>, height:hl.Ref<Int>, left:hl.Ref<Int>, top:hl.Ref<Int>, advance:hl.Ref<Float>):Int {
		return -2;
	}
}
