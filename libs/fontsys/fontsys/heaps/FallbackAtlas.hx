package fontsys.heaps;

import fontsys.ColorGlyph;
import fontsys.SdfGlyph;

private class Page {
	public var tex:h3d.mat.Texture;
	public var pixels:hxd.Pixels;
	public var dirty = false;
	/** Pixels left free right of and below each glyph. **/
	public var gutter:Int;
	/** Shelf packer state: current row's origin and height, next free x. **/
	public var shelfY = 0;
	public var shelfH = 0;
	public var cursorX = 0;

	public function new(gutter) {
		this.gutter = gutter;
	}
}

/**
	Texture pages holding SDF fallback glyphs. Pages are RGBA with RGB = 255 and the
	SDF in alpha (sampled with SDFChannel.Alpha). Glyphs are shelf-packed with a 1 px
	gutter; uploads are batched to one per dirty page per frame. A CPU copy of every
	page is kept so a lost graphics context can be restored. There is no eviction:
	once `maxPages` pages are full, `place` returns null.

	Colour glyphs (emoji, `placeColor`) go on separate colour pages: RGBA with straight alpha, as
	rendered, capped at `maxColorPages` and packed, uploaded and restored the same way. They are
	authored at 64 px and drawn at text sizes, often 4-5x smaller, so colour pages are mipmapped,
	glyphs sit COLOR_GUTTER px apart (mip levels then mix no neighbours) and transparent texels near
	a glyph take the colour beside them, alpha staying 0, so filtering does not darken edges.
**/
class FallbackAtlas {
	static inline var GUTTER = 1;
	/** Mip level 2 (4x smaller) averages 4x4 blocks: a 4 px gap keeps neighbours apart. **/
	static inline var COLOR_GUTTER = 4;

	/** Number of SDF pages allocated so far. **/
	public var pageCount(default, null) = 0;

	/** Number of colour pages allocated so far. **/
	public var colorPageCount(default, null) = 0;

	/** Number of page uploads performed by `flush`. **/
	public var uploads(default, null) = 0;

	var pageSize:Int;
	var maxPages:Int;
	var maxColorPages:Int;
	var pages:Array<Page> = [];
	var colorPages:Array<Page> = [];
	var loopEvent:haxe.MainLoop.MainEvent;
	var loggedFull = false;
	var loggedColorFull = false;
	var disposed = false;

	public function new(pageSize:Int = 1024, maxPages:Int = 2, maxColorPages:Int = 2) {
		this.pageSize = pageSize;
		this.maxPages = maxPages;
		this.maxColorPages = maxColorPages;
		loopEvent = haxe.MainLoop.add(flush, -1);
		loopEvent.isBlocking = false;
	}

	/**
		A tile (dx = dy = 0) on a colour page for `g`'s RGBA bitmap, or null if the colour pages are
		full, the atlas is disposed or the glyph is larger than a page. Logs once when full.
	**/
	public function placeColor(g:ColorGlyph):Null<h2d.Tile> {
		if (disposed)
			return null;
		var w = g.width, h = g.height;
		if (w > pageSize || h > pageSize)
			return null;
		var p = null;
		for (q in colorPages)
			if (fit(q, w, h)) {
				p = q;
				break;
			}
		if (p == null) {
			if (colorPages.length >= maxColorPages) {
				if (!loggedColorFull) {
					loggedColorFull = true;
					trace('FallbackAtlas: colour pages full ($maxColorPages pages)');
				}
				return null;
			}
			p = newPage(colorPages, true);
			colorPageCount = colorPages.length;
			fit(p, w, h);
		}
		var x = p.cursorX, y = p.shelfY;
		reserve(p, w, h);
		var dst = p.pixels.bytes;
		for (row in 0...h)
			dst.blit(((y + row) * pageSize + x) * 4, g.pixels, row * w * 4, w * 4);
		bleed(p, x - COLOR_GUTTER, y - COLOR_GUTTER, w + 2 * COLOR_GUTTER, h + 2 * COLOR_GUTTER);
		return changed(p, x, y, w, h);
	}

	/**
		A tile (dx = dy = 0) for `g`'s bitmap, or null if the atlas is full or disposed
		or the glyph is larger than a page. A zero-size glyph (width or height 0) gets a
		0x0 tile on the current page; it writes no pixels and takes no packer space
		beyond the 1 px gutter.
	**/
	public function place(g:SdfGlyph):Null<h2d.Tile> {
		if (disposed)
			return null;
		var w = g.width, h = g.height;
		if (w > pageSize || h > pageSize)
			return null;
		for (p in pages) {
			if (fit(p, w, h))
				return put(p, g);
		}
		if (pages.length >= maxPages) {
			if (!loggedFull) {
				loggedFull = true;
				trace('FallbackAtlas: full ($maxPages pages)');
			}
			return null;
		}
		var p = newPage(pages, false);
		pageCount = pages.length;
		fit(p, w, h);
		return put(p, g);
	}

	/** Takes the w x h slot at the page's cursor (after `fit` said it fits). **/
	function reserve(p:Page, w:Int, h:Int) {
		p.cursorX += w + p.gutter;
		if (h > p.shelfH)
			p.shelfH = h;
	}

	/** Marks the page for upload after its CPU copy changed, and returns the slot's tile. **/
	function changed(p:Page, x:Int, y:Int, w:Int, h:Int):h2d.Tile {
		p.dirty = true;
		// A page already on the GPU would otherwise show the old pixels until the next
		// flush, but h2d.Text lays out and draws after flush has run this frame. Free the
		// GPU copy (realloc stays set) so the next bind re-uploads the whole CPU copy.
		// Once disposed it stays so until re-uploaded: many placements cost one upload.
		if (@:privateAccess p.tex.t != null)
			p.tex.dispose();
		return h2d.Tile.fromTexture(p.tex).sub(x, y, w, h);
	}

	/** Reserves the slot's position in the page's packer state; true if the glyph fits. **/
	function fit(p:Page, w:Int, h:Int):Bool {
		if (p.cursorX + w > pageSize) {
			// Close the shelf and start a new one below it.
			var ny = p.shelfY + p.shelfH + (p.shelfH > 0 ? p.gutter : 0);
			if (ny + h > pageSize)
				return false;
			p.shelfY = ny;
			p.shelfH = 0;
			p.cursorX = 0;
		} else if (p.shelfY + Math.max(p.shelfH, h) > pageSize) {
			return false;
		}
		return true;
	}

	function put(p:Page, g:SdfGlyph):h2d.Tile {
		var w = g.width, h = g.height;
		var x = p.cursorX, y = p.shelfY;
		reserve(p, w, h);
		// CPU copy: RGB = 255, A = SDF value. Pixels are RGBA so bytes are 255,255,255,v.
		var dst = p.pixels.bytes;
		var src = g.pixels;
		for (row in 0...h) {
			var si = row * w;
			var di = ((y + row) * pageSize + x) * 4;
			for (col in 0...w) {
				dst.set(di, 255);
				dst.set(di + 1, 255);
				dst.set(di + 2, 255);
				dst.set(di + 3, src.get(si + col));
				di += 4;
			}
		}
		return changed(p, x, y, w, h);
	}

	/**
		Gives each transparent texel in the rectangle (clipped to the page) the average colour of
		its non-transparent neighbours, COLOR_GUTTER rings deep; alpha is left as it is.
	**/
	function bleed(p:Page, x0:Int, y0:Int, w:Int, h:Int) {
		var x1 = x0 + w < pageSize ? x0 + w : pageSize, y1 = y0 + h < pageSize ? y0 + h : pageSize;
		if (x0 < 0) x0 = 0;
		if (y0 < 0) y0 = 0;
		w = x1 - x0;
		h = y1 - y0;
		if (w <= 0 || h <= 0)
			return;
		var b = p.pixels.bytes;
		// known: has a colour to give (opaque-ish, or filled by an earlier ring).
		var known = haxe.io.Bytes.alloc(w * h);
		for (y in 0...h)
			for (x in 0...w)
				if (b.get(((y0 + y) * pageSize + x0 + x) * 4 + 3) != 0)
					known.set(y * w + x, 1);
		for (ring in 0...COLOR_GUTTER) {
			var filled = [];
			for (y in 0...h)
				for (x in 0...w) {
					if (known.get(y * w + x) != 0)
						continue;
					var r = 0, g = 0, bl = 0, n = 0;
					for (dy in -1...2)
						for (dx in -1...2) {
							var nx = x + dx, ny = y + dy;
							if (nx < 0 || ny < 0 || nx >= w || ny >= h || known.get(ny * w + nx) == 0)
								continue;
							var i = ((y0 + ny) * pageSize + x0 + nx) * 4;
							r += b.get(i);
							g += b.get(i + 1);
							bl += b.get(i + 2);
							n++;
						}
					if (n == 0)
						continue;
					var i = ((y0 + y) * pageSize + x0 + x) * 4;
					b.set(i, Std.int(r / n));
					b.set(i + 1, Std.int(g / n));
					b.set(i + 2, Std.int(bl / n));
					filled.push(y * w + x);
				}
			if (filled.length == 0)
				break;
			for (k in filled)
				known.set(k, 1);
		}
	}

	function newPage(list:Array<Page>, color:Bool):Page {
		var p = new Page(color ? COLOR_GUTTER : GUTTER);
		p.pixels = hxd.Pixels.alloc(pageSize, pageSize, RGBA);
		p.tex = new h3d.mat.Texture(pageSize, pageSize, color ? [NoAlloc, MipMapped] : [NoAlloc], RGBA);
		p.tex.filter = Linear;
		if (color)
			p.tex.mipMap = Linear;
		p.tex.realloc = function() {
			if (p.pixels == null)
				return;
			p.tex.uploadPixels(p.pixels);
			p.dirty = false;
			uploads++;
		};
		list.push(p);
		return p;
	}

	/** Uploads every page changed since the last flush. Runs once per frame via MainLoop. **/
	public function flush():Void {
		for (list in [pages, colorPages])
			for (p in list) {
				if (!p.dirty)
					continue;
				p.dirty = false;
				p.tex.uploadPixels(p.pixels);
				uploads++;
			}
	}

	public function dispose():Void {
		if (disposed)
			return;
		disposed = true;
		if (loopEvent != null) {
			loopEvent.stop();
			loopEvent = null;
		}
		for (p in pages.concat(colorPages)) {
			p.tex.realloc = null;
			p.tex.dispose();
			p.pixels = null;
		}
		pages = [];
		colorPages = [];
	}
}
