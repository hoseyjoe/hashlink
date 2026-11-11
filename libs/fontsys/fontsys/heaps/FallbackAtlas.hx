package fontsys.heaps;

import fontsys.SdfGlyph;

private class Page {
	public var tex:h3d.mat.Texture;
	public var pixels:hxd.Pixels;
	public var dirty = false;
	/** Shelf packer state: current row's origin and height, next free x. **/
	public var shelfY = 0;
	public var shelfH = 0;
	public var cursorX = 0;

	public function new() {}
}

/**
	Texture pages holding SDF fallback glyphs. Pages are RGBA with RGB = 255 and the
	SDF in alpha (sampled with SDFChannel.Alpha). Glyphs are shelf-packed with a 1 px
	gutter; uploads are batched to one per dirty page per frame. A CPU copy of every
	page is kept so a lost graphics context can be restored. There is no eviction:
	once `maxPages` pages are full, `place` returns null.
**/
class FallbackAtlas {
	static inline var GUTTER = 1;

	/** Number of pages allocated so far. **/
	public var pageCount(default, null) = 0;

	/** Number of page uploads performed by `flush`. **/
	public var uploads(default, null) = 0;

	var pageSize:Int;
	var maxPages:Int;
	var pages:Array<Page> = [];
	var loopEvent:haxe.MainLoop.MainEvent;
	var loggedFull = false;
	var disposed = false;

	public function new(pageSize:Int = 1024, maxPages:Int = 2) {
		this.pageSize = pageSize;
		this.maxPages = maxPages;
		loopEvent = haxe.MainLoop.add(flush, -1);
		loopEvent.isBlocking = false;
	}

	/** A tile (dx = dy = 0) for `g`'s bitmap, or null if the atlas is full or disposed. **/
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
		var p = newPage();
		fit(p, w, h);
		return put(p, g);
	}

	/** Reserves the slot's position in the page's packer state; true if the glyph fits. **/
	function fit(p:Page, w:Int, h:Int):Bool {
		if (p.cursorX + w > pageSize) {
			// Close the shelf and start a new one below it.
			var ny = p.shelfY + p.shelfH + (p.shelfH > 0 ? GUTTER : 0);
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
		p.cursorX += w + GUTTER;
		if (h > p.shelfH)
			p.shelfH = h;
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
		p.dirty = true;
		return h2d.Tile.fromTexture(p.tex).sub(x, y, w, h);
	}

	function newPage():Page {
		var p = new Page();
		p.pixels = hxd.Pixels.alloc(pageSize, pageSize, RGBA);
		p.tex = new h3d.mat.Texture(pageSize, pageSize, [NoAlloc], RGBA);
		p.tex.filter = Linear;
		p.tex.realloc = function() {
			if (p.pixels != null)
				p.tex.uploadPixels(p.pixels);
		};
		pages.push(p);
		pageCount = pages.length;
		return p;
	}

	/** Uploads every page changed since the last flush. Runs once per frame via MainLoop. **/
	public function flush():Void {
		for (p in pages) {
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
		for (p in pages) {
			p.tex.realloc = null;
			p.tex.dispose();
			p.pixels = null;
		}
		pages = [];
	}
}
