#ifndef __COLORS_H__
#define __COLORS_H__

#define COLOR_ENUM_SZ 10

// Currently, we support 10 colors, which means that this tool only can color up to 10 
// different loops. For number of loops > 10, just use the default color. Also, it is easy
// to extend the Color_enum. 
// We follow the color rule from the url: http://www.graphviz.org/doc/info/colors.html#brewer
enum ColorEnum {
	RED,
	GREEN,
	BLUE,
	CYAN,
	GOLD,
	HOTPINK,
	NAVY,
	ORANGE,
	OLIVEDRAB,
	MAGENTA
};

#endif
