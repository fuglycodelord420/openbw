#ifndef OPENBW_UI_USER_INPUT_HANDLER_H
#define OPENBW_UI_USER_INPUT_HANDLER_H

#include "common.h"
#include "../bwgame.h"
#include "../actions.h"
#include "ui_utils.h"

#include "simple/graphical/window.h"
#include "simple/interactive.hpp"

#include <cassert>
#include <vector>

namespace bwgame
{
	namespace sprt = simple::support;

	template <typename T = int>
	inline xy_t<T> to_xy(int2 v) { return {T(v.x()), T(v.y())}; }
	inline rect to_rect(sprt::range<int2> v)
	{
		return
		{
			to_xy(v.lower()),
			to_xy(v.upper())
		};
	}

	inline int2 to_int2(xy v) { return {v.x,v.y}; }
	inline sprt::range<int2> to_range2(rect r)
	{
		return
		{
			to_int2(r.from),
			to_int2(r.to)
		};
	}

	struct user_input_handler
	{
		simple::graphical::window& wnd;
		const int owner;
		action_functions& actions;
		const simple::geom::segment<int2>& view;

		const unit_type_t* building;
		bool build_mode;
		int2 building_tile_position;

		std::vector<unit_t*> selection_buffer;

		user_input_handler
		(
			simple::graphical::window&,
			int owner,
			action_functions&,
			const simple::geom::segment<int2>& view
		);

		void select(sprt::range<int2> region, bool double_clicked);


		void default_action(int2) const;

		void handle_event(const simple::interactive::event&);
		void key_up(const simple::interactive::key_released&);

		bool left_click(const simple::interactive::mouse_down&);

		void draw(pixel_writer_rgba);

	};

} // namespace bwgame

#endif /* end of include guard */
