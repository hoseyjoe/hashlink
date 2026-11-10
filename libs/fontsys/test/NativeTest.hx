import fontsys.FontStyle;
import fontsys.FontSys;

/**
	Plain HashLink test of the fontsys.hdll bindings (no Heaps).
	Exits 1 on the first failed check.
**/
class NativeTest {
	static function check(ok:Bool, what:String) {
		if (!ok) {
			Sys.println("NativeTest FAILED: " + what);
			Sys.exit(1);
		}
	}

	static function tempDir():String {
		for (v in ["TEMP", "TMP", "TMPDIR"]) {
			var d = Sys.getEnv(v);
			if (d != null && d != "")
				return d;
		}
		return "/tmp";
	}

	static function main() {
		var fs = new FontSys();

		// A Simplified Chinese glyph is covered by some installed font.
		var found = fs.find(0x4E2D, "zh-Hans", FontStyle.REGULAR);
		check(found != null, "find(U+4E2D, zh-Hans) found nothing");
		Sys.println('U+4E2D -> ${found.path} #${found.index}');

		var face = fs.openFace(found.path, found.index);
		check(face >= 0, "openFace(" + found.path + ") = " + face);
		check(fs.faceHas(face, 0x4E2D), "faceHas(U+4E2D) false");
		check(!fs.faceHas(face, 0x10FFFD), "faceHas(U+10FFFD) true");

		var g = fs.renderSdf(face, 0x4E2D, 32, 8);
		check(g != null, "renderSdf(U+4E2D, 32, 8) = null");
		check(g.width > 32, "width " + g.width + " <= 32");
		check(g.height > 32, "height " + g.height + " <= 32");
		check(g.pixels.length == g.width * g.height, "pixels.length " + g.pixels.length + " != " + g.width + "*" + g.height);
		check(g.advance > 20, "advance " + g.advance + " <= 20");

		// Larger than the initial native buffer: exercises the -1 retry.
		var big = fs.renderSdf(face, 0x4E2D, 128, 32);
		check(big != null, "renderSdf(U+4E2D, 128, 32) = null");
		check(big.width > 128 && big.pixels.length == big.width * big.height, "big glyph " + big.width + "x" + big.height);

		// A face that lacks the code point renders nothing.
		check(fs.renderSdf(face, 0x10FFFD, 32, 8) == null, "renderSdf(U+10FFFD) != null");

		// A private-use plane-16 code point no installed font covers.
		check(fs.find(0x10FFFD, "en", FontStyle.REGULAR) == null, "find(U+10FFFD) found a font");

		// Non-ASCII path: the String -> UTF-8 conversion must round-trip.
		var src = "C:/Windows/Fonts/arial.ttf";
		var srcIndex = 0;
		if (!sys.FileSystem.exists(src)) {
			src = found.path;
			srcIndex = found.index;
		}
		var ext = src.substr(src.lastIndexOf("."));
		var copy = tempDir() + "/fontsys_\u6D4B\u8BD5" + ext;
		sys.io.File.copy(src, copy);
		var copyFace = fs.openFace(copy, srcIndex);
		var copyHasA = copyFace >= 0 && fs.faceHas(copyFace, "A".code);
		fs.dispose();
		sys.FileSystem.deleteFile(copy);
		check(copyFace >= 0, "openFace(" + copy + ") = " + copyFace);
		check(copyHasA, "copied font lacks 'A'");

		Sys.println("NativeTest: all passed");
	}
}
