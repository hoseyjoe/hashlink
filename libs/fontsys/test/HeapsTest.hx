import fontsys.SdfGlyph;
import fontsys.heaps.FallbackAtlas;

/** FallbackAtlas test; see heaps.hxml for the run command. **/
class HeapsTest extends hxd.App {
	static var failures = 0;
	var frame = 0;

	// State carried between frames by the one-frame-later upload case.
	var upAtlas:FallbackAtlas;

	static function main() {
		new HeapsTest();
	}

	static function check(ok:Bool, msg:String) {
		if (!ok) {
			failures++;
			Sys.println("FAIL: " + msg);
		}
	}

	static function glyph(w:Int, h:Int, v:Int):SdfGlyph {
		var g = new SdfGlyph();
		g.width = w;
		g.height = h;
		g.left = 0;
		g.top = h;
		g.advance = w;
		g.pixels = haxe.io.Bytes.alloc(w * h);
		g.pixels.fill(0, w * h, v);
		return g;
	}

	static function overlap(a:h2d.Tile, b:h2d.Tile):Bool {
		// Tiles on different textures never overlap; on the same one keep a 1 px gutter.
		if (a.getTexture() != b.getTexture())
			return false;
		var ax = Std.int(a.x), ay = Std.int(a.y), bx = Std.int(b.x), by = Std.int(b.y);
		var aw = Std.int(a.width), ah = Std.int(a.height), bw = Std.int(b.width), bh = Std.int(b.height);
		return ax < bx + bw + 1 && bx < ax + aw + 1 && ay < by + bh + 1 && by < ay + ah + 1;
	}

	override function init() {
		hxd.System.allowTimeout = false;

		// Upload batching: placing in one frame uploads nothing yet; one flush later, exactly once.
		upAtlas = new FallbackAtlas(1024, 2);
		for (i in 0...50)
			check(upAtlas.place(glyph(40, 40, 200)) != null, 'batch glyph $i placed');
		check(upAtlas.uploads == 0, 'no upload straight after place (got ${upAtlas.uploads})');

		// No overlap, with gutter.
		var a = new FallbackAtlas(1024, 2);
		var tiles = [for (i in 0...30) a.place(glyph(20 + i % 7, 18 + i % 5, 90))];
		var bad = 0;
		for (i in 0...tiles.length) {
			check(tiles[i] != null, 'tile $i placed');
			check(tiles[i].dx == 0 && tiles[i].dy == 0, 'tile $i has dx/dy 0');
			for (j in i + 1...tiles.length)
				if (overlap(tiles[i], tiles[j]))
					bad++;
		}
		check(bad == 0, 'tiles overlap or lack gutter ($bad pairs)');
		a.dispose();

		// Page cap, and the single "full" log.
		var logs:Array<String> = [];
		var oldTrace = haxe.Log.trace;
		haxe.Log.trace = function(v:Dynamic, ?p) logs.push(Std.string(v));
		var c = new FallbackAtlas(256, 2);
		var placed = [];
		for (i in 0...8)
			placed.push(c.place(glyph(100, 100, 77)));
		check(c.pageCount == 2, 'two pages after 8 glyphs (got ${c.pageCount})');
		check(placed[3] != null && placed[3].getTexture() == placed[0].getTexture(), '4th glyph still on page 1');
		check(placed[4].getTexture() != placed[0].getTexture(), '5th glyph on page 2');
		check(c.place(glyph(100, 100, 77)) == null, '9th glyph returns null');
		check(c.place(glyph(100, 100, 77)) == null, '10th glyph returns null');
		check(c.place(glyph(100, 100, 77)) == null, '11th glyph returns null');
		haxe.Log.trace = oldTrace;
		var fullLogs = [for (l in logs) if (l.indexOf("FallbackAtlas: full (2 pages)") >= 0) l];
		check(fullLogs.length == 1, 'full logged exactly once (got ${fullLogs.length}: $logs)');
		check(c.place(glyph(300, 10, 1)) == null, 'glyph bigger than a page returns null');

		// Pixel readback and context loss.
		var d = new FallbackAtlas(256, 2);
		var t = d.place(glyph(30, 30, 173));
		d.flush();
		check(d.uploads == 1, 'explicit flush uploads one page (got ${d.uploads})');
		d.flush();
		check(d.uploads == 1, 'clean flush uploads nothing');
		var tex = t.getTexture();
		var cx = Std.int(t.x) + 15, cy = Std.int(t.y) + 15;
		var px = tex.capturePixels().getPixel(cx, cy);
		check((px >>> 24) == 173, 'alpha at glyph centre is 173 (got ${px >>> 24})');
		check(((px >> 16) & 0xFF) == 255 && ((px >> 8) & 0xFF) == 255 && (px & 0xFF) == 255, 'RGB is 255 (got ${StringTools.hex(px & 0xFFFFFF, 6)})');

		// Context loss: dispose the GPU texture; the next use runs realloc.
		tex.dispose();
		var px2 = tex.capturePixels().getPixel(cx, cy);
		check((px2 >>> 24) == 173, 'alpha after context loss is 173 (got ${px2 >>> 24})');
		check(((px2 >> 16) & 0xFF) == 255, 'RGB after context loss is 255');

		// dispose: later place returns null.
		d.dispose();
		check(d.place(glyph(10, 10, 1)) == null, 'place after dispose returns null');
		c.dispose();
	}

	override function update(dt:Float) {
		frame++;
		if (frame == 2) {
			check(upAtlas.uploads == 1, 'one upload a frame later (got ${upAtlas.uploads})');
			check(upAtlas.pageCount == 1, 'batch used one page');
			upAtlas.dispose();
			if (failures == 0)
				Sys.println("HeapsTest: all passed");
			else
				Sys.println('HeapsTest: $failures failure(s)');
			Sys.exit(failures == 0 ? 0 : 1);
		}
	}
}
