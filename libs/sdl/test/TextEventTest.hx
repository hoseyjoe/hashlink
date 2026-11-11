import sdl.Event;

class TextEventTest {
	static var failures = [];

	static function check(name:String, cond:Bool) {
		if( !cond ) failures.push(name);
	}

	// push, then pump like hxd does, returning the first event of the wanted type
	static function pump(want:EventType):Event {
		var found:Event = null;
		sdl.Sdl.processEvents(function(e:Event) {
			if( found == null && e.type == want ) {
				found = new Event();
				found.type = e.type;
				found.keyCode = e.keyCode;
				found.text = e.text;
				found.editStart = e.editStart;
				found.editLength = e.editLength;
			}
			return false;
		});
		return found;
	}

	static function str(b:hl.Bytes):String {
		return b == null ? null : @:privateAccess String.fromUTF8(b);
	}

	static function main() {
		sdl.Sdl.init();
		var win = new sdl.Window("t", 100, 100, 0, 0, sdl.Window.SDL_WINDOW_HIDDEN);

		check("push commit", sdl.Sdl.debugPushTextEvent(false, "你好世界", 0, 0));
		var e = pump(TextInput);
		check("commit event", e != null);
		if( e != null ) check("commit text", str(e.text) == "你好世界");

		sdl.Sdl.debugPushTextEvent(false, "😀", 0, 0);
		e = pump(TextInput);
		check("emoji event", e != null);
		if( e != null ) {
			var s = str(e.text);
			check("emoji len", s.length == 2);
			check("emoji surrogate", s.charCodeAt(0) == 0xD83D);
		}

		sdl.Sdl.debugPushTextEvent(false, "a", 0, 0);
		e = pump(TextInput);
		check("a event", e != null);
		if( e != null ) check("a keyCode", e.keyCode == 0x61);

		sdl.Sdl.debugPushTextEvent(true, "nihao", 2, -1);
		e = pump(TextEditing);
		check("edit event", e != null);
		if( e != null ) {
			check("edit text", str(e.text) == "nihao");
			check("edit start", e.editStart == 2);
			check("edit length", e.editLength == -1);
		}

		sdl.Sdl.debugPushTextEvent(true, "", 0, 0);
		e = pump(TextEditing);
		check("empty edit event", e != null);
		if( e != null ) {
			check("empty edit text not null", e.text != null);
			check("empty edit text", str(e.text) == "");
		}

		win.stopTextInput();
		check("inactive after stop", win.isTextInputActive() == false);
		win.startTextInput();
		check("active after start", win.isTextInputActive() == true);
		win.setTextInputArea(10, 20, 300, 24, 40);

		if( failures.length == 0 ) {
			Sys.println("TextEventTest: all passed");
			Sys.exit(0);
		} else {
			for( f in failures ) Sys.println("FAIL: " + f);
			Sys.exit(1);
		}
	}
}
