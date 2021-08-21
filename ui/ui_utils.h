#ifndef OPENBW_UI_UTILS_H
#define OPENBW_UI_UTILS_H

#include "common.h"
#include "simple/graphical/pixels.h"
#include "simple/graphical/algorithm/fill.h"
#include "simple/graphical/surface.h"

namespace bwgame
{

	using namespace simple::graphical::color_literals;

	struct rgba_color
	{
		static constexpr auto green = 0x10fc18ff_rgba;
		static constexpr auto red = 0xfc1018ff_rgba;
	};

	using simple::graphical::int2;
	using simple::graphical::pixel_byte;
	using simple::graphical::rgba_pixel;
	using simple::graphical::surface;
	using range2 = simple::support::range<int2>;
	using pixel_writer = simple::graphical::pixel_writer<pixel_byte>;
	using pixel_writer_rgba = simple::graphical::pixel_writer<rgba_pixel, pixel_byte>;

	template <typename Pixels, typename Color>
	inline void line_range(Pixels pixels, range2 range, int line_width, Color color)
	{
		for(auto dim = int2::dimensions; dim --> 0;)
		{
			auto lower_edge	= range;
			auto upper_edge	= range;

			lower_edge.upper()[dim] = lower_edge.lower()[dim] + line_width;
			upper_edge.lower()[dim] = upper_edge.upper()[dim] - line_width;

			fill(Pixels(pixels, lower_edge), color);
			fill(Pixels(pixels, upper_edge), color);
		}
	}

	inline void fill_rectangle(pixel_writer pixels, range2 rect, pixel_byte color)
	{
		if(!rect.valid()) return;
		rect.intersect({int2::zero(), pixels.size()});
		fill(pixel_writer(pixels, rect), color);
	}

	inline void line_rectangle(pixel_writer pixels, range2 rect, pixel_byte color)
	{
		if(!rect.valid()) return;
		rect.intersect({int2::zero(), pixels.size()});
		line_range(pixels, rect, 1, color);
	}

	inline void line_rectangle_rgba(pixel_writer_rgba pixels, range2 rect, rgba_pixel color)
	{
		if(!rect.valid()) return;
		rect.intersect({int2::zero(), pixels.size()});
		line_range(pixels, rect, 1, color);
	}

	inline void outline_rectangle(pixel_writer pixels, range2 rect, int line_width, pixel_byte color)
	{
		rect.lower() -= line_width;
		rect.upper() += line_width;
		if(!rect.valid()) return;
		rect.intersect({int2::zero(), pixels.size()});
		line_range(pixels, rect, 1, color);
	}

	inline surface resize(const surface& s, int2 size)
	{
		simple::graphical::surface ret(size, s.format());
		ret.blend(s.blend());
		ret.alpha(s.alpha());
		ret.color(s.color());
		return std::move(ret);
	}

} // namespace bwgame

#endif /* end of include guard */
