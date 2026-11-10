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

	public function new() {
		ctx = create();
		if (ctx == null)
			throw "fontsys: cannot initialize FreeType";
	}

	/**
		An installed font covering `code` for the BCP 47 language `lang`
		(e.g. "zh-Hans", "ja") and `style`, or null if none does.
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

	/** Frees the native context and every face; the object is unusable afterwards. **/
	public function dispose():Void {
		if (ctx != null) {
			destroy(ctx);
			ctx = null;
		}
		buf = null;
	}

	@:hlNative("fontsys", "hl_create") static function create():Ctx {
		return null;
	}

	@:hlNative("fontsys", "hl_destroy") static function destroy(ctx:Ctx):Void {}

	@:hlNative("fontsys", "hl_find") static function find_font(ctx:Ctx, code:Int, lang:hl.Bytes, weight:Int, italic:Bool, mono:Bool,
		index:hl.Ref<Int>):hl.Bytes {
		return null;
	}

	@:hlNative("fontsys", "hl_face_open") static function face_open(ctx:Ctx, path:hl.Bytes, index:Int):Int {
		return -1;
	}

	@:hlNative("fontsys", "hl_face_has") static function face_has(ctx:Ctx, face:Int, code:Int):Bool {
		return false;
	}

	@:hlNative("fontsys", "hl_render_sdf") static function render_sdf(ctx:Ctx, face:Int, code:Int, em:Int, spread:Int, buf:hl.Bytes,
		bufCap:Int, width:hl.Ref<Int>, height:hl.Ref<Int>, left:hl.Ref<Int>, top:hl.Ref<Int>, advance:hl.Ref<Float>):Int {
		return -2;
	}
}
