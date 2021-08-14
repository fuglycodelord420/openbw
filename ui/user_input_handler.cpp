#include "user_input_handler.h"

#include <SDL2/SDL.h>


using namespace bwgame;

user_input_handler::user_input_handler
(
	simple::graphical::window& wnd,
	int owner,
	action_functions& actions,
	const simple::geom::segment<int2>& view
) :
	wnd(wnd),
	owner(owner),
	actions(actions),
	view(view)
{
	assert(owner > 0 && owner < 9);
}

void user_input_handler::handle_event(const simple::interactive::event& e)
{
	using namespace simple::interactive;
	std::visit(simple::support::overloaded
	{
		[this](const key_released& key)
		{
			key_up(key);
		},
		[this](const mouse_up& mouse)
		{
			if(mouse.data.button == mouse_button::right)
				default_action(view.position + mouse.data.position);
		},
		[](auto&&){}

	}, e);
}

void user_input_handler::key_up(const simple::interactive::key_released& key)
{
	if(key.data.scancode == simple::interactive::scancode::s)
	{
		actions.action_train(owner, actions.get_unit_type(UnitTypes::Terran_SCV));
	}
}

void user_input_handler::default_action(int2 position) const
{
	unit_t* target = actions.select_get_unit_at(to_xy(position));
	actions.action_default_order(owner, to_xy(position), target, nullptr, false);
}

void user_input_handler::select(sprt::range<int2> region, bool double_clicked)
{

	using simple::interactive::pressed;
	using simple::interactive::scancode;
	bool shift = pressed(scancode::lshift) || pressed(scancode::rshift);
	bool ctrl = pressed(scancode::lctrl) || pressed(scancode::rctrl);

	auto corner = region.lower(); // TODO: sort by distance to this
	region.fix();

	auto small_box = not(region.upper() - region.lower() > int2::one(4));

	auto already_selected = actions.selected_units(owner);
	auto first_selected = actions.get_first_selected_unit(owner);
	int available_slots = 12 -
	(shift ? std::distance(already_selected.begin(), already_selected.end()) : 0);
	if(small_box)
	{
		unit_t* u = actions.select_get_unit_at(to_xy(view.position + region.lower()));

		if(!u)
		{
			// just clicked on empty space, nothing to do
			return;

			// this is not what broodwar does
			// actions_proxy->action_deselect(*owner_id, already_selected);
		}

		if(u->owner == owner)
		{
			selection_buffer.push_back(u);
			if (double_clicked || ctrl)
			{
				for (unit_t* u2 : actions.find_units(to_rect(view)))
				{
					if (u2 == u) continue;
					if (u2->owner != u->owner) continue;
					if (u->unit_type != u2->unit_type) continue;
					selection_buffer.push_back(u2);
				}
			}
			else if(shift)
			{
				if(std::find(already_selected.begin(), already_selected.end(), u) != already_selected.end())
				{
					actions.action_deselect(owner, u);
					// shift-deselected one selected unit, we are done
					return;
				}
			}
		}
		else
		{
			// select only one enemy unit and we are done
			actions.action_select(owner, u);
			return;
		}
	}
	else
	{
		a_vector<unit_t*> new_units;
		bool any_non_neutrals = false;
		for (unit_t* u : actions.find_units(to_rect(region + view.position)))
		{
			if (actions.unit_can_be_selected(u))
				selection_buffer.push_back(u);
		}
		auto non_neutral_end = std::partition (
			selection_buffer.begin(), selection_buffer.end(),
			[](auto& unit) { return unit &&
			(unit->owner != 11); }
		);
		std::partition ( selection_buffer.begin(), non_neutral_end,
			[this](auto& unit) { return unit &&
			(unit->owner == owner); }
		);
	}

	if(shift)
		actions.action_shift_select(owner, selection_buffer);
	else
		actions.action_select(owner, selection_buffer);

	selection_buffer.clear();
}

void user_input_handler::draw(pixel_writer pixels) const
{
	// draw building placement box
}

