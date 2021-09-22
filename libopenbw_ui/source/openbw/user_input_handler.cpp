#include "user_input_handler.h"

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
	view(view),
	building(nullptr),
	build_mode(false),
	building_tile_position()
{
	assert(owner >= 0 && owner < 8);
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
			{
				build_mode = false;
				building = nullptr;
				default_action(view.position + mouse.data.position);
			}
		},
		[](auto&&){}

	}, e);
}

void user_input_handler::key_up(const simple::interactive::key_released& key)
{
	using simple::interactive::scancode;

	switch(key.data.scancode)
	{
		case scancode::s:
			actions.execute_action(owner, std::tuple(
				bwgame::action_data::train{},
				actions.get_unit_type(UnitTypes::Terran_SCV)
			));
			actions.execute_action(owner, std::tuple(
				bwgame::action_data::train{},
				actions.get_unit_type(UnitTypes::Terran_Marine)
			));
			if(build_mode)
				building = actions.get_unit_type(UnitTypes::Terran_Supply_Depot);
		break;

		case scancode::p:
			actions.execute_action(owner, std::tuple(
				bwgame::action_data::train{},
				actions.get_unit_type(UnitTypes::Protoss_Probe)
			));
			if(build_mode)
				building = actions.get_unit_type(UnitTypes::Protoss_Pylon);
		break;

		case scancode::d:
			actions.execute_action(owner, std::tuple(
				bwgame::action_data::train{},
				actions.get_unit_type(UnitTypes::Protoss_Dragoon)
			));
		break;

		case scancode::g:
			if(build_mode)
				building = actions.get_unit_type(UnitTypes::Protoss_Gateway);
		break;

		case scancode::m:
			actions.execute_action(owner, std::tuple(
				bwgame::action_data::train{},
				actions.get_unit_type(UnitTypes::Terran_Medic)
			));
			if(build_mode)
				building = actions.get_unit_type(UnitTypes::Terran_Missile_Turret);
		break;

		case scancode::f:
			actions.execute_action(owner, std::tuple(
				bwgame::action_data::train{},
				actions.get_unit_type(UnitTypes::Terran_Firebat)
			));
		break;

		case scancode::a:
			if(build_mode)
			{
				unit_t* u = actions.get_single_selected_unit(owner);
				switch(u->unit_type->id)
				{
					case UnitTypes::Terran_SCV:
						building = actions.get_unit_type(UnitTypes::Terran_Academy);
					break;

					case UnitTypes::Protoss_Probe:
						building = actions.get_unit_type(UnitTypes::Protoss_Assimilator);
					break;

					default:;
				}
			}
		break;

		case scancode::u:
			if(build_mode)
				building = actions.get_unit_type(UnitTypes::Terran_Bunker);
		break;

		case scancode::e:
			if(build_mode)
				building = actions.get_unit_type(UnitTypes::Terran_Engineering_Bay);
		break;

		case scancode::c:
			if(build_mode)
			{
				unit_t* u = actions.get_single_selected_unit(owner);
				switch(u->unit_type->id)
				{
					case UnitTypes::Terran_SCV:
						building = actions.get_unit_type(UnitTypes::Terran_Command_Center);
					break;

					case UnitTypes::Protoss_Probe:
						building = actions.get_unit_type(UnitTypes::Protoss_Cybernetics_Core);
					break;

					default:;
				}
			}
		break;

		case scancode::r:
			if(build_mode)
				building = actions.get_unit_type(UnitTypes::Terran_Refinery);
		break;

		case scancode::b:
		{
			if(build_mode)
				building = actions.get_unit_type(UnitTypes::Terran_Barracks);
			else
			{
				unit_t* u = actions.get_single_selected_unit(owner);
				if(u && u->owner == owner) switch(u->unit_type->id)
				{
					case UnitTypes::Terran_SCV:
					case UnitTypes::Zerg_Drone:
					case UnitTypes::Protoss_Probe:
						build_mode = true;
					break;

					default:;
				}
			}
		}
		break;

		default:;

	}
}

bool user_input_handler::left_click(const simple::interactive::mouse_down& mouse)
{
	if(building)
	{
		unit_t* u = actions.get_single_selected_unit(owner);
		actions.execute_action(owner, std::tuple(
			bwgame::action_data::build{},
			actions.get_order_type(
				u->unit_type->id == UnitTypes::Terran_SCV ? Orders::PlaceBuilding :
				u->unit_type->id == UnitTypes::Zerg_Drone ? Orders::PlaceBuilding :
				u->unit_type->id == UnitTypes::Protoss_Probe ? Orders::PlaceProtossBuilding :
				Orders::None
			),
			action_data::tile_vector(building_tile_position),
			building
		));
		build_mode = false;
		building = nullptr;
		return true;
	}

	return false;
}

void user_input_handler::default_action(int2 position) const
{
	unit_t* target = actions.select_get_unit_at(to_xy(position));
	actions.execute_action(owner, std::tuple(
		bwgame::action_data::default_order{},
		action_data::position_vector(position),
		target, nullptr, false
	));
}

void user_input_handler::select(range2 region, bool double_clicked)
{

	using simple::interactive::pressed;
	using simple::interactive::scancode;
	bool shift = pressed(scancode::lshift) || pressed(scancode::rshift);
	bool ctrl = pressed(scancode::lctrl) || pressed(scancode::rctrl);

	auto corner = view.position + region.lower();
	region.fix();

	auto small_box = not(region.upper() - region.lower() > int2::one(4));

	auto already_selected = actions.selected_units(owner);

	if(small_box)
	{
		unit_t* u = actions.select_get_unit_at(to_xy(view.position + region.lower()));

		if(!u || !is_visible(*u))
		{

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
					actions.execute_action(owner, std::tuple{
						bwgame::action_data::deselect{},
						bwgame::action_data::selection_array{{u}, {1}}
					});
					// shift-deselected one selected unit, we are done
					return;
				}
			}
		}
		else
		{
			// select only one enemy unit and we are done
			actions.execute_action(owner, std::tuple{
				bwgame::action_data::select{},
				bwgame::action_data::selection_array{{u}, {1}}
			});
			return;
		}
	}
	else
	{
		a_vector<unit_t*> new_units;
		bool any_non_neutrals = false;
		for (unit_t* u : actions.find_units(to_rect(region + view.position)))
		{
			if (actions.unit_can_be_selected(u) && is_visible(*u))
				selection_buffer.push_back(u);
		}

		constexpr auto neutral_owner = 11;
		std::sort
		(
			selection_buffer.begin(), selection_buffer.end(),
			[this, &corner](auto& a, auto& b)
			{
				// first partition by owners
				if(a->owner != b->owner)
				{
				    // we go to the beginning
					if(a->owner == owner) return true;
					if(b->owner == owner) return false;

					// neutrals go to the end
					if(a->owner == neutral_owner) return false;
					if(b->owner == neutral_owner) return true;

					// others go in some order
					return a->owner < b->owner;
				}

				// then partition further by multi select
				bool multi_a = actions.unit_can_be_multi_selected(a);
				bool multi_b = actions.unit_can_be_multi_selected(b);
				if(multi_a != multi_b)
				{
					// multi selectable go to the beginning
					if(multi_a) return true;
					return false;
				}

				// finally sort by distance to corner
				auto a_distance = magnitude(to_int2(a->position) - corner);
				auto b_distance = magnitude(to_int2(b->position) - corner);

				return a_distance < b_distance;
			}
		);
	}

	size_t available_slots = 12 -
	(shift ? std::distance(already_selected.begin(), already_selected.end()) : 0);

	auto size = std::min(selection_buffer.size(), available_slots);

	std::copy_n(selection_buffer.begin(), size, selection.begin());

	if(shift)
		actions.execute_action(owner, std::tuple{
			bwgame::action_data::shift_select{},
			bwgame::action_data::selection_array{selection, {size}}
		});
	else
		actions.execute_action(owner, std::tuple{
			bwgame::action_data::select{},
			bwgame::action_data::selection_array{selection, {size}}
		});

	selection_buffer.clear();

}

template <typename T>
auto div_ceil(T divident, T divisor)
{
	return (divident + divisor - 1) / divisor;
}

template <typename T>
auto div_round(T divident, T divisor)
{
	return (divident + divisor/2) / divisor;
}

void user_input_handler::draw(pixel_writer_rgba pixels)
{
	// deselect units out of vision range
	{
		std::array<unit_t*, 12> selected_not_visible{};
		size_t i = 0;
		for(auto&& u : actions.selected_units(owner))
			if(!is_visible(*u))
				selected_not_visible[i++] = u;
		actions.execute_action(owner, std::tuple{
			bwgame::action_data::deselect{},
			bwgame::action_data::selection_array{{selected_not_visible}, {i}}
		});
	}

	// draw building placement box
	{
		constexpr auto tile_size = int2::one(32);

		if(building)
		{
			auto building_tiles = div_ceil(to_int2(building->placement_size), tile_size);

			auto mouse = simple::interactive::last_mouse_state();

			auto building_size = tile_size * building_tiles;

			auto map_position = view.position + mouse.position;
			map_position -= building_size / 2;

			map_position = div_round(map_position, tile_size);
			building_tile_position = map_position;
			map_position *= tile_size;

			map_position -= view.position;

			unit_t* u = actions.get_single_selected_unit(owner);
			bool can_place = actions.can_place_building(
				u, owner, building,
				to_xy(building_tile_position * tile_size + building_size/2),
				false, false);
			line_rectangle_rgba(pixels,
				simple::geom::segment{building_size, map_position},
				can_place ? rgba_color::green : rgba_color::red);
		}
	}

}

// TODO: account for allied vision
bool user_input_handler::is_visible(const sprite_t& sprite) const
{
	return sprite.visibility_flags & (1u << owner);
}

bool user_input_handler::is_visible(const unit_t& unit) const
{
	return is_visible(*(unit.sprite));
}

bool user_input_handler::is_visible(const tile_t& tile) const
{
	// NOTE: for some reason tile visibility flags are inverted
	return not (tile.visible & (1u << owner));
}
