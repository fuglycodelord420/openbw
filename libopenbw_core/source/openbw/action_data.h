#ifndef OPENBW_ACTION_DATA_H
#define OPENBW_ACTION_DATA_H

#include "bwgame.h"

#include "simple/support/tuple_utils.hpp"
#include "simple/support/algorithm.hpp"
#include "simple/support/misc.hpp"
#include "simple/geom/vector.hpp"

#include <tuple>
#include <optional>
#include <variant>
#include <array>
#include <cassert>

#ifndef NDEBUG
#include <iostream>
#endif

namespace bwgame::action_data
{

	namespace sup = simple::support;
	namespace geom = simple::geom;

	using id_t = uint8_t;
	using unit_id_t = uint16_t;
	using unit_type_id_t = uint16_t;
	using tech_type_id_t = uint8_t;
	using upgrade_type_id_t = uint8_t;
	using order_id_t = uint8_t;
	using tile_vector = geom::vector<uint16_t, 2>;
	using position_vector = geom::vector<uint16_t, 2>;
	using vision_flags_t = uint16_t;
	using alliance_flags_t = uint32_t;
	using cheat_flags_t = uint32_t;
	using group_id_t = uint8_t;
	using group_subaction_t = uint8_t;
	using bool_t = uint8_t;
	using build_queue_slot_t = uint16_t;
	using player_leave_reason_t = uint8_t;
	using chat_message_t = geom::vector<uint8_t, 81>;
	using player_id_t = uint8_t;
	using upgrade_level_t = uint8_t;
	using resource_amount_t = int32_t;

	inline id_t get_primary_id(id_t id) { return id; }

	template <size_t N, typename O>
	id_t get_primary_id(const geom::vector<id_t,N,O>& id) { return id[0]; }

	template <typename T>
	struct tuple_capacity;

	template <typename... Elements>
	struct tuple_capacity<std::tuple<Elements...>>
	{
		constexpr static auto value = (sizeof(Elements) + ... + 0);
	};

	template <typename T>
	constexpr auto tuple_capacity_v = tuple_capacity<T>::value;

	template <typename T>
	using size_range = sup::range<T,
		sup::range_upper_bound<T, std::integral_constant<T,0>>>;

	template <typename Element, typename Size, Size Capacity>
	using param_array = typename sup::with_range<size_range, Size>::
		template index<std::array<Element,Capacity>>;

	using selection_array = param_array<unit_t*, size_t, 12>;
	using selection_id_array = param_array<unit_id_t, uint8_t, 12>;

	struct alliance_array : std::array<int,12> {};

	template <typename Writer, typename T>
	void put(Writer&& w, const T& value) { w.put(value); }

	template <typename Writer, typename T, size_t N, typename O>
	void put(Writer&& w, const geom::vector<T,N,O>& vector)
	{
		for(auto&& element : vector)
			put(w,element);
	}

	template <typename Writer>
	void put(Writer&& w, const selection_id_array& selection)
	{
		put(w, selection_id_array::size_type(
			selection.end() - selection.begin()));

		for(auto&& unit : selection)
			put(w, unit);
	}

	template <typename Reader, typename T>
	bool get(Reader&& r, state&, T& value)
	{
		if(r.left() < sizeof(T)) return false;
		value = r.template get<T>();
		return true;
	}

	template <typename Reader>
	bool get(Reader&& r, state& st, unit_t*& unit)
	{
		unit_id_t id;
		if(not get(r,st,id)) return false;
		unit = get_unit(st, unit_id(id));
		return true;
	}

	template <typename Reader>
	bool get(Reader&& r, state& st, const unit_type_t*& type)
	{
		unit_type_id_t id;
		if(not get(r,st,id)) return false;
		type = get_unit_type(st, UnitTypes(id));
		return true;
	}

	template <typename Reader>
	bool get(Reader&& r, state& st, const tech_type_t*& type)
	{
		tech_type_id_t id;
		if(not get(r,st,id)) return false;
		type = get_tech_type(st, TechTypes(id));
		return true;
	}

	template <typename Reader>
	bool get(Reader&& r, state& st, const upgrade_type_t*& type)
	{
		upgrade_type_id_t id;
		if(not get(r,st,id)) return false;
		type = get_upgrade_type(st, UpgradeTypes(id));
		return true;
	}

	template <typename Reader>
	bool get(Reader&& r, state& st, const order_type_t*& order)
	{
		order_id_t id;
		if(not get(r,st,id)) return false;
		order = get_order_type(st, Orders(id));
		return true;
	}

	template <typename Reader>
	bool get(Reader&& r, state& st, alliance_array& alliances)
	{
		alliance_flags_t flags;
		if(not get(r,st,flags)) return false;

		// thanks std for not providing compile time array size -_-
		assert(std::numeric_limits<alliance_flags_t>::digits/2 >= alliances.size());
		static_assert(std::is_unsigned_v<alliance_flags_t>);
		for (size_t i = 0; i != alliances.size(); ++i) {
			alliances[i] = flags & 0b11;
			flags >>= 2;
		}
		return true;
	}

	template <typename Reader, typename T, size_t N, typename O>
	bool get(Reader&& r, state& st, geom::vector<T,N,O>& vector)
	{
		for(auto&& element : vector)
			if(not get(r,st,element))
				return false;
		return true;
	}

	template <typename Reader>
	bool get(Reader&& r, state& st, bool& value)
	{
		bool_t raw_value;
		if(not get(r,st,raw_value)) return false;
		value = raw_value != 0;
		return true;
	}

	template <typename Reader>
	bool get(Reader&& r, state& st, std::string& message)
	{
		chat_message_t raw_message;
		if(not get(r,st,raw_message)) return false;
		for (auto& c : raw_message) {
			if (c == 0) break;
			if (c >= 32) message += (char)c;
		}
		return true;
	}

	template <typename Reader>
	bool get(Reader&& r, state& st, selection_array& selection)
	{
		selection_id_array::size_type raw_size;

		if(not get(r,st,raw_size)) return false;
		selection = selection_array({}, {raw_size});
		for (auto& unit : selection)
		{
			if(not get(r, st, unit)) return false;
		}
		return true;
	}

	struct
	{

		template <typename T>
		auto operator()(const T& param)
		{
			return param;
		}

		unit_id_t operator()(unit_t* unit)
		{
			return get_unit_id(unit).raw_value;
		}

		unit_type_id_t operator()(const unit_type_t* type)
		{
			auto id = type ? type->id : UnitTypes::None;
			return static_cast<unit_type_id_t>(id);
		}

		tech_type_id_t operator()(const tech_type_t* type)
		{
			assert(type);
			return static_cast<tech_type_id_t>(type->id);
		}

		upgrade_type_id_t operator()(const upgrade_type_t* type)
		{
			assert(type);
			return static_cast<upgrade_type_id_t>(type->id);
		}

		order_id_t operator()(const order_type_t* order)
		{
			assert(order);
			return static_cast<order_id_t>(order->id);
		}

		alliance_flags_t operator()(const alliance_array& alliances)
		{
			alliance_flags_t flags{};

			// thanks std for not providing compile time array size -_-
			assert(std::numeric_limits<alliance_flags_t>::digits/2 >= alliances.size());
			static_assert(std::is_unsigned_v<alliance_flags_t>);
			for (size_t i = 0; i != alliances.size(); ++i) {
				auto flag = static_cast<alliance_flags_t>(alliances[i]);
				flags |= flag << (i*2);
			}
			return flags;
		}

		bool_t operator()(bool param)
		{
			return static_cast<bool_t>(param);
		}

		chat_message_t operator()(const std::string& message)
		{
			chat_message_t result{};
			auto fitting_message = sup::map_range(sup::make_range(message), result);
			std::copy(fitting_message.begin(), fitting_message.end(), result.begin());
			return result;
		}

		selection_id_array operator()(const selection_array& selection)
		{
			selection_id_array result{{}, {
				selection_id_array::size_type
					(selection.end() - selection.begin())
			}};
			std::transform(selection.begin(), selection.end(),
				result.begin(), *this);
			return result;
		}

	} to_data_param;

	template <typename Writer, typename Params>
	void write(Writer&& writer, const Params& params)
	{
		using namespace simple::support;
		if constexpr(std::tuple_size_v<Params> == 0)
			return;
		else
		{
			put(writer, tuple_car(params));
			write(writer, tuple_tie_cdr(params));
		}
	}

	template <typename Reader, typename Params>
	size_t read(Reader&& reader, state& st, Params&& params)
	{
		using namespace simple::support;
		using params_t = std::remove_reference_t<Params>;
		if constexpr(std::tuple_size_v<params_t> == 0)
			return 0;
		else
		{
			if(not get(reader, st, tuple_car(params))) return std::tuple_size_v<params_t>;
			return read(reader, st, tuple_tie_cdr(params));
		}
		return false;
	};

	template <typename Action>
	struct action_interface : Action
	{
		using typename Action::params;
		using typename Action::func_params;

		using writer_type = data_writer<tuple_capacity_v<params> + sizeof(id_t)>;

		template <typename A = Action, std::enable_if_t<
			// TODO; check for existance of func_params instead of this, to eliminate the duplication when they are the same
			not std::is_same_v<
				typename A::func_params,
				typename A::params
			>
		>* = nullptr>
		static writer_type write(const func_params& p)
		{
			return write(sup::transform(to_data_param, p));
		};

		static writer_type write(const params& p)
		{
			writer_type w;
			put(w, Action::id);
			action_data::write(w, p);
			return w;
		};

		template <typename Reader>
		static std::optional<func_params> read(Reader&& r, state& st)
		{
			auto id = Action::id;
			if(not get(r, st, id)) return std::nullopt;
			if(id != Action::id) return std::nullopt;

			func_params result;
			auto failed_count = action_data::read(r,st,result);
			if(failed_count == 0)
			{
				return std::optional(result);
			}
			else
			{
#ifndef NDEBUG
				std::cerr << "parsing action data failed at parameter " << std::tuple_size_v<func_params> - failed_count << '\n';
#endif
				return std::nullopt;
			}
		}

	};

	template <id_t ID>
	struct select_guts
	{
		constexpr static id_t id = ID;
		enum index { units };
		using params = std::tuple<selection_id_array>;
		using func_params = std::tuple<selection_array>;
	};

	struct build_guts
	{
		constexpr static id_t id = 12;

		enum index
		{
			order,
			tile,
			unit_type
		};

		using params = std::tuple
		<
			order_id_t,
			tile_vector,
			unit_type_id_t
		>;

		using func_params = std::tuple
		<
			const order_type_t*,
			tile_vector,
			const unit_type_t*
		>;
	};

	struct set_shared_vision_guts
	{
		constexpr static id_t id = 13;
		enum index { flags };
		using params = std::tuple<vision_flags_t>;
		using func_params = params;
	};

	struct set_alliances_guts
	{
		constexpr static id_t id = 14;
		enum index { flags };
		using params = std::tuple<alliance_flags_t>;
		using func_params = std::tuple<alliance_array>;
	};

	struct cheat_guts
	{
		constexpr static id_t id = 18;
		enum index { flags };
		using params = std::tuple < cheat_flags_t >;
		using func_params = params;
	};

	struct control_group_guts
	{
		constexpr static id_t id = 19;

		enum index
		{
			subaction,
			group_id
		};

		using params = std::tuple
		<
			group_subaction_t,
			group_id_t
		>;

		using func_params = params;
	};

	struct default_order_guts
	{
		constexpr static id_t id = 20;

		enum index
		{
			position,
			target,
			target_type,
			queue
		};

		using params = std::tuple
		<
			position_vector,
			unit_id_t,
			unit_type_id_t,
			bool_t
		>;

		using func_params = std::tuple
		<
			position_vector,
			unit_t*,
			const unit_type_t*,
			bool
		>;
	};

	struct order_guts
	{
		constexpr static id_t id = 21;

		enum index
		{
			position,
			target,
			target_type,
			order,
			queue
		};

		using params = std::tuple
		<
			position_vector,
			unit_id_t,
			unit_type_id_t,
			order_id_t,
			bool_t
		>;

		using func_params = std::tuple
		<
			position_vector,
			unit_t*,
			const unit_type_t*,
			const order_type_t*,
			bool
		>;
	};

	template <id_t ID>
	struct implicit_guts
	{
		constexpr static id_t id = ID;
		using params = std::tuple<>;
		using func_params = params;
	};

	template <id_t ID>
	struct queue_guts
	{
		constexpr static id_t id = ID;
		enum index { queue };
		using params = std::tuple<bool_t>;
		using func_params = std::tuple<bool>;
	};

	template <id_t ID>
	struct train_guts
	{
		constexpr static id_t id = ID;
		enum index { unit_type };
		using params = std::tuple<unit_type_id_t>;
		using func_params = std::tuple<const unit_type_t*>;
	};

	struct cancel_build_queue_guts
	{
		constexpr static id_t id = 32;
		enum index { slot };
		using params = std::tuple<build_queue_slot_t>;
		using func_params = params;
	};

	template <id_t ID>
	struct unused_byte_guts
	{
		constexpr static id_t id = ID;
		using params = std::tuple<uint8_t>;
		using func_params = params;
	};

	struct unload_guts
	{
		constexpr static id_t id = 41;
		enum index { unit };
		using params = std::tuple<unit_id_t>;
		using func_params = std::tuple<unit_t*>;
	};

	template <id_t ID>
	struct position_guts
	{
		constexpr static id_t id = ID;
		enum index { position };
		using params = std::tuple<position_vector>;
		using func_params = params;
	};

	struct research_guts
	{
		constexpr static id_t id = 48;
		enum index { tech };
		using params = std::tuple<tech_type_id_t>;
		using func_params = std::tuple<const tech_type_t*>;
	};

	struct upgrade_guts
	{
		constexpr static id_t id = 50;
		enum index { upgrade };
		using params = std::tuple<upgrade_type_id_t>;
		using func_params = std::tuple<const upgrade_type_t*>;
	};

	struct player_leave_guts
	{
		constexpr static id_t id = 87;
		enum index { reason };
		using params = std::tuple<player_leave_reason_t>;
		using func_params = params;
	};

	struct chat_guts
	{
		constexpr static id_t id = 92;
		enum index { message };
		using params = std::tuple<chat_message_t>;
		using func_params = std::tuple<std::string>;
	};

	template <id_t subtype>
	struct ext_cheat_unit_guts
	{
		constexpr static auto id = geom::vector<id_t,3>(210, 0, subtype);
		enum index { target, value };
		using params = std::tuple<unit_id_t, int32_t>;
		using func_params = std::tuple<unit_t*, int32_t>;
	};

	struct ext_cheat_upgrade_guts
	{
		constexpr static auto id = geom::vector<id_t,3>(210, 1, 0);
		enum index
		{
			player,
			upgrade,
			level
		};

		using params = std::tuple
		<
			player_id_t,
			upgrade_type_id_t,
			upgrade_level_t
		>;

		using func_params = std::tuple
		<
			player_id_t,
			const upgrade_type_t*,
			upgrade_level_t
		>;
	};

	struct ext_cheat_tech_guts
	{
		constexpr static auto id = geom::vector<id_t,3>(210, 1, 1);
		enum index
		{
			player,
			tech,
			researched
		};

		using params = std::tuple
		<
			player_id_t,
			tech_type_id_t,
			bool_t
		>;

		using func_params = std::tuple
		<
			player_id_t,
			const tech_type_t*,
			bool
		>;
	};

	template <id_t subtype>
	struct ext_cheat_resource_guts
	{
		constexpr static auto id = geom::vector<id_t,3>(210, 1, subtype);
		enum index { player, amount, };
		using params = std::tuple<player_id_t, resource_amount_t>;
		using func_params = params;
	};

	using select = action_interface<select_guts<9>>;
	using shift_select = action_interface<select_guts<10>>;
	using deselect = action_interface<select_guts<11>>;
	using build = action_interface<build_guts>;
	using set_shared_vision = action_interface<set_shared_vision_guts>;
	using set_alliances = action_interface<set_alliances_guts>;
	using cheat = action_interface<cheat_guts>;
	using control_group = action_interface<control_group_guts>;
	using default_order = action_interface<default_order_guts>;
	using order = action_interface<order_guts>;
	using cancel_building_unit = action_interface<implicit_guts<24>>;
	using cancel_morph = action_interface<implicit_guts<25>>;
	using stop = action_interface<queue_guts<26>>;
	using carrier_stop = action_interface<implicit_guts<27>>;
	using reaver_stop = action_interface<implicit_guts<28>>;
	using return_cargo = action_interface<queue_guts<30>>;
	using train = action_interface<train_guts<31>>;
	using morph = action_interface<train_guts<35>>;
	using morph_building = action_interface<train_guts<53>>;
	using cancel_build_queue = action_interface<cancel_build_queue_guts>;
	using cloak = action_interface<unused_byte_guts<33>>;
	using decloak = action_interface<unused_byte_guts<34>>;
	using unsiege = action_interface<queue_guts<37>>;
	using siege = action_interface<queue_guts<38>>;
	using train_fighter = action_interface<implicit_guts<39>>;
	using unload_all = action_interface<queue_guts<40>>;
	using unload = action_interface<unload_guts>;
	using morph_archon = action_interface<implicit_guts<42>>;
	using morph_dark_archon = action_interface<implicit_guts<90>>;
	using hold_position = action_interface<queue_guts<43>>;
	using burrow = action_interface<queue_guts<44>>;
	using unburrow = action_interface<unused_byte_guts<45>>;
	using cancel_nuke = action_interface<implicit_guts<46>>;
	using liftoff = action_interface<position_guts<47>>;
	using research = action_interface<research_guts>;
	using cancel_research = action_interface<implicit_guts<49>>;
	using upgrade = action_interface<upgrade_guts>;
	using cancel_upgrade = action_interface<implicit_guts<51>>;
	using cancel_addon = action_interface<implicit_guts<52>>;
	using stim_pack = action_interface<implicit_guts<54>>;
	using player_leave = action_interface<player_leave_guts>;
	using ping_minimap = action_interface<position_guts<88>>;
	using chat = action_interface<chat_guts>;
	using ext_cheat_hp = action_interface<ext_cheat_unit_guts<0>>;
	using ext_cheat_shield = action_interface<ext_cheat_unit_guts<1>>;
	using ext_cheat_energy = action_interface<ext_cheat_unit_guts<2>>;
	using ext_cheat_upgrade = action_interface<ext_cheat_upgrade_guts>;
	using ext_cheat_tech = action_interface<ext_cheat_tech_guts>;
	using ext_cheat_minerals = action_interface<ext_cheat_resource_guts<2>>;
	using ext_cheat_gas = action_interface<ext_cheat_resource_guts<3>>;

	using all_types = std::tuple
	<
		select, shift_select, deselect,
		build,
		set_shared_vision, set_alliances,
		cheat,
		control_group,
		default_order, order,
		cancel_building_unit, cancel_morph,
		stop, carrier_stop, reaver_stop,
		return_cargo,
		train, morph, morph_building,
		cancel_build_queue,
		cloak, decloak,
		unsiege, siege,
		train_fighter,
		unload_all, unload,
		morph_archon, morph_dark_archon,
		hold_position,
		burrow, unburrow,
		cancel_nuke,
		liftoff,
		research, cancel_research,
		upgrade, cancel_upgrade,
		cancel_addon,
		stim_pack,
		player_leave,
		ping_minimap,
		chat,
		ext_cheat_hp, ext_cheat_shield, ext_cheat_energy,
		ext_cheat_upgrade, ext_cheat_tech,
		ext_cheat_minerals, ext_cheat_gas
	>;

	template <typename T>
	using make_alternative_t = sup::prepend_t<typename T::func_params, T>;

	template <typename T>
	struct tuple_to_variant;
	template <typename... Ts>
	struct tuple_to_variant<std::tuple<Ts...>>
	{using type = std::variant<Ts...>;};
	template <typename T>
	using tuple_to_variant_t = typename tuple_to_variant<T>::type;

	using variant = tuple_to_variant_t<sup::transform_t<all_types, make_alternative_t>>;

} // namespace bwgame

#endif /* end of include guard */
