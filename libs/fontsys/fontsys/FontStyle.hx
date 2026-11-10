package fontsys;

/** What fontsys.FontSys.find asks the OS for, besides coverage and language. **/
class FontStyle {
	/** 100 (thin) .. 900 (black); 400 regular, 700 bold. **/
	public var weight:Int;

	public var italic:Bool;

	/** Prefer a fixed-width font. **/
	public var mono:Bool;

	public function new(weight = 400, italic = false, mono = false) {
		this.weight = weight;
		this.italic = italic;
		this.mono = mono;
	}

	public static final REGULAR = new FontStyle();
	public static final BOLD = new FontStyle(700);
	public static final ITALIC = new FontStyle(400, true);
	public static final BOLD_ITALIC = new FontStyle(700, true);
	public static final MONO = new FontStyle(400, false, true);
}
