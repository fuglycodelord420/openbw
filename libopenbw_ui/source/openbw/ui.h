#include "common.h"
#include "openbw/bwgame.h"
#include "openbw/replay.h"

#include "native_sound.h"
#include "ui_utils.h"
#include "user_input_handler.h"

#include "simple/graphical/software_window.h"
#include "simple/graphical/algorithm/blit.h"
#include "simple/graphical/algorithm/fill.h"

#include "simple/graphical/initializer.h"
#include "simple/interactive/initializer.h"

#include <optional>

namespace bwgame {

using namespace simple::graphical::color_literals;

struct vr4_entry {
	using bitmap_t = std::conditional<is_native_fast_int<uint64_t>::value, uint64_t, uint32_t>::type;
	std::array<bitmap_t, 64 / sizeof(bitmap_t)> bitmap;
	std::array<bitmap_t, 64 / sizeof(bitmap_t)> inverted_bitmap;
};
struct vx4_entry {
	std::array<uint16_t, 16> images;
};

struct pcx_image {
	size_t width;
	size_t height;
	a_vector<uint8_t> data;
};

struct tileset_image_data {
	a_vector<uint8_t> wpe;
	a_vector<vr4_entry> vr4;
	a_vector<vx4_entry> vx4;
	pcx_image dark_pcx;
	std::array<pcx_image, 7> light_pcx;
	grp_t creep_grp;
	int resource_minimap_color;
	std::array<uint8_t, 256> cloak_fade_selector;
};

struct image_data {
	std::array<std::array<uint8_t, 8>, 16> player_unit_colors;
	std::array<uint8_t, 16> player_minimap_colors;
	std::array<uint8_t, 24> selection_colors;
	std::array<uint8_t, 24> hp_bar_colors;
	std::array<int, 0x100> creep_edge_frame_index{};
};

template<typename data_T>
pcx_image load_pcx_data(const data_T& data) {
	data_loading::data_reader_le r(data.data(), data.data() + data.size());
	auto base_r = r;
	auto id = r.get<uint8_t>();
	if (id != 0x0a) error("pcx: invalid identifier %#x", id);
	r.get<uint8_t>(); // version
	auto encoding = r.get<uint8_t>(); // encoding
	auto bpp = r.get<uint8_t>(); // bpp
	auto offset_x = r.get<uint16_t>();
	auto offset_y = r.get<uint16_t>();
	auto last_x = r.get<uint16_t>();
	auto last_y = r.get<uint16_t>();

	if (encoding != 1) error("pcx: invalid encoding %#x", encoding);
	if (bpp != 8) error("pcx: bpp is %d, expected 8", bpp);

	if (offset_x != 0 || offset_y != 0) error("pcx: offset %d %d, expected 0 0", offset_x, offset_y);

	r.skip(2 + 2 + 48 + 1);

	auto bit_planes = r.get<uint8_t>();
	auto bytes_per_line = r.get<uint16_t>();

	size_t width = last_x + 1;
	size_t height = last_y + 1;

	pcx_image pcx;
	pcx.width = width;
	pcx.height = height;

	pcx.data.resize(width * height);

	r = base_r;
	r.skip(128);

	auto padding = bytes_per_line * bit_planes - width;
	if (padding != 0) error("pcx: padding not supported");

	uint8_t* dst = pcx.data.data();
	uint8_t* dst_end = pcx.data.data() + pcx.data.size();

	while (dst != dst_end) {
		auto v = r.get<uint8_t>();
		if ((v & 0xc0) == 0xc0) {
			v &= 0x3f;
			auto c = r.get<uint8_t>();
			for (; v; --v) {
				if (dst == dst_end) error("pcx: failed to decode");
				*dst++ = c;
			}
		} else {
			*dst = v;
			++dst;
		}
	}

	return pcx;
}

// static inline std::unique_ptr<native_window_drawing::surface> flip_image(native_window_drawing::surface* src) {
// 	auto tmp = native_window_drawing::create_rgba_surface(src->w, src->h);
// 	src->blit(&*tmp, 0, 0);
// 	void* ptr = tmp->lock();
// 	uint32_t* pixels = (uint32_t*)ptr;
// 	for (size_t y = 0; y != (size_t)tmp->h; ++y) {
// 		for (size_t x = 0; x != (size_t)tmp->w / 2; ++x) {
// 			std::swap(pixels[x], pixels[tmp->w - 1 - x]);
// 		}
// 		pixels += tmp->pitch / 4;
// 	}
// 	tmp->unlock();
// 	return tmp;
// }

template<typename load_data_file_F>
void load_image_data(image_data& img, load_data_file_F&& load_data_file) {

	std::array<int, 0x100> creep_edge_neighbors_index{};
	std::array<int, 128> creep_edge_neighbors_index_n{};

	for (size_t i = 0; i != 0x100; ++i) {
		int v = 0;
		if (i & 2) v |= 0x10;
		if (i & 8) v |= 0x24;
		if (i & 0x10) v |= 9;
		if (i & 0x40) v |= 2;
		if ((i & 0xc0) == 0xc0) v |= 1;
		if ((i & 0x60) == 0x60) v |= 4;
		if ((i & 3) == 3) v |= 0x20;
		if ((i & 6) == 6) v |= 8;
		if ((v & 0x21) == 0x21) v |= 0x40;
		if ((v & 0xc) == 0xc) v |= 0x40;
		creep_edge_neighbors_index[i] = v;
	}

	int n = 0;
	for (int i = 0; i != 128; ++i) {
		auto it = std::find(creep_edge_neighbors_index.begin(), creep_edge_neighbors_index.end(), i);
		if (it == creep_edge_neighbors_index.end()) continue;
		creep_edge_neighbors_index_n[i] = n;
		++n;
	}

	for (size_t i = 0; i != 0x100; ++i) {
		img.creep_edge_frame_index[i] = creep_edge_neighbors_index_n[creep_edge_neighbors_index[i]];
	}

	a_vector<uint8_t> tmp_data;
	auto load_pcx_file = [&](a_string filename) {
		load_data_file(tmp_data, std::move(filename));
		return load_pcx_data(tmp_data);
	};

	auto tunit_pcx = load_pcx_file("game/tunit.pcx");
	if (tunit_pcx.width != 128 || tunit_pcx.height != 1) error("tunit.pcx dimensions are %dx%d (128x1 required)", tunit_pcx.width, tunit_pcx.height);
	for (size_t i = 0; i != 16; ++i) {
		for (size_t i2 = 0; i2 != 8; ++i2) {
			img.player_unit_colors[i][i2] = tunit_pcx.data[i * 8 + i2];
		}
	}
	auto tminimap_pcx = load_pcx_file("game/tminimap.pcx");
	if (tminimap_pcx.width != 16 || tminimap_pcx.height != 1) error("tminimap.pcx dimensions are %dx%d (16x1 required)", tminimap_pcx.width, tminimap_pcx.height);
	for (size_t i = 0; i != 16; ++i) {
		img.player_minimap_colors[i] = tminimap_pcx.data[i];
	}
	auto tselect_pcx = load_pcx_file("game/tselect.pcx");
	if (tselect_pcx.width != 24 || tselect_pcx.height != 1) error("tselect.pcx dimensions are %dx%d (24x1 required)", tselect_pcx.width, tselect_pcx.height);
	for (size_t i = 0; i != 24; ++i) {
		img.selection_colors[i] = tselect_pcx.data[i];
	}
	auto thpbar_pcx = load_pcx_file("game/thpbar.pcx");
	if (thpbar_pcx.width != 19 || thpbar_pcx.height != 1) error("thpbar.pcx dimensions are %dx%d (19x1 required)", thpbar_pcx.width, thpbar_pcx.height);
	for (size_t i = 0; i != 19; ++i) {
		img.hp_bar_colors[i] = thpbar_pcx.data[i];
	}

}

template<typename load_data_file_F>
void load_tileset_image_data(tileset_image_data& img, size_t tileset_index, load_data_file_F&& load_data_file) {
	using namespace data_loading;

	std::array<const char*, 8> tileset_names = {
		"badlands", "platform", "install", "AshWorld", "Jungle", "Desert", "Ice", "Twilight"
	};

	a_vector<uint8_t> vr4_data;
	a_vector<uint8_t> vx4_data;

	const char* tileset_name = tileset_names.at(tileset_index);

	load_data_file(vr4_data, format("Tileset/%s.vr4", tileset_name));
	load_data_file(vx4_data, format("Tileset/%s.vx4", tileset_name));
	load_data_file(img.wpe, format("Tileset/%s.wpe", tileset_name));

	a_vector<uint8_t> grp_data;
	load_data_file(grp_data, format("Tileset/%s.grp", tileset_name));
	img.creep_grp = read_grp(data_loading::data_reader_le(grp_data.data(), grp_data.data() + grp_data.size()));

	data_reader<true, false> vr4_r(vr4_data.data(), nullptr);
	img.vr4.resize(vr4_data.size() / 64);
	for (size_t i = 0; i != img.vr4.size(); ++i) {
		for (size_t i2 = 0; i2 != 8; ++i2) {
			auto r2 = vr4_r;
			auto v = vr4_r.get<uint64_t, true>();
			auto iv = r2.get<uint64_t, false>();
			size_t n = 8 / sizeof(vr4_entry::bitmap_t);
			for (size_t i3 = 0; i3 != n; ++i3) {
				img.vr4[i].bitmap[i2 * n + i3] = (vr4_entry::bitmap_t)v;
				img.vr4[i].inverted_bitmap[i2 * n + i3] = (vr4_entry::bitmap_t)iv;
				v >>= n == 1 ? 0 : 8 * sizeof(vr4_entry::bitmap_t);
				iv >>= n == 1 ? 0 : 8 * sizeof(vr4_entry::bitmap_t);
			}
		}
	}
	data_reader<true, false> vx4_r(vx4_data.data(), vx4_data.data() + vx4_data.size());
	img.vx4.resize(vx4_data.size() / 32);
	for (size_t i = 0; i != img.vx4.size(); ++i) {
		for (size_t i2 = 0; i2 != 16; ++i2) {
			img.vx4[i].images[i2] = vx4_r.get<uint16_t>();
		}
	}

	a_vector<uint8_t> tmp_data;
	auto load_pcx_file = [&](a_string filename) {
		load_data_file(tmp_data, std::move(filename));
		return load_pcx_data(tmp_data);
	};

	img.dark_pcx = load_pcx_file(format("Tileset/%s/dark.pcx", tileset_name));
	if (img.dark_pcx.width != 256 || img.dark_pcx.height != 32) error("invalid dark.pcx");
	for (size_t x = 0; x != 256; ++x) {
		img.dark_pcx.data[256 * 31 + x] = (uint8_t)x;
	}

	std::array<const char*, 7> light_names = {"ofire", "gfire", "bfire", "bexpl", "trans50", "red", "green"};
	for (size_t i = 0; i != 7; ++i) {
		img.light_pcx[i] = load_pcx_file(format("Tileset/%s/%s.pcx", tileset_name, light_names[i]));
	}

	if (img.wpe.size() != 256 * 4) error("wpe size invalid (%d)", img.wpe.size());

	auto get_nearest_color = [&](int r, int g, int b) {
		size_t best_index = -1;
		int best_score{};
		for (size_t i = 0; i != 256; ++i) {
			int dr = r - img.wpe[4 * i + 0];
			int dg = g - img.wpe[4 * i + 1];
			int db = b - img.wpe[4 * i + 2];
			int score = dr * dr + dg * dg + db * db;
			if (best_index == (size_t)-1 || score < best_score) {
				best_index = i;
				best_score = score;
			}
		}
		return best_index;
	};
	img.resource_minimap_color = get_nearest_color(0, 255, 255);

	for (size_t i = 0; i != 256; ++i) {
		int r = img.wpe[4 * i + 0];
		int g = img.wpe[4 * i + 1];
		int b = img.wpe[4 * i + 2];
		int strength = (r * 28 + g * 77 + b * 151 + 4096) / 8192;
		img.cloak_fade_selector[i] = strength;
	}

}

template<bool bounds_check>
void draw_tile(tileset_image_data& img, size_t megatile_index, uint8_t* dst, size_t pitch, size_t offset_x, size_t offset_y, size_t width, size_t height) {
	auto* images = &img.vx4.at(megatile_index).images[0];
	size_t x = 0;
	size_t y = 0;
	for (size_t image_iy = 0; image_iy != 4; ++image_iy) {
		for (size_t image_ix = 0; image_ix != 4; ++image_ix) {
			auto image_index = *images;
			bool inverted = (image_index & 1) == 1;
			auto* bitmap = inverted ? &img.vr4.at(image_index / 2).inverted_bitmap[0] : &img.vr4.at(image_index / 2).bitmap[0];

			for (size_t iy = 0; iy != 8; ++iy) {
				for (size_t iv = 0; iv != 8 / sizeof(vr4_entry::bitmap_t); ++iv) {
					for (size_t b = 0; b != sizeof(vr4_entry::bitmap_t); ++b) {
						if (!bounds_check || (x >= offset_x && y >= offset_y && x < width && y < height)) {
							*dst = (uint8_t)(*bitmap >> (8 * b));
						}
						++dst;
						++x;
					}
					++bitmap;
				}
				x -= 8;
				++y;
				dst -= 8;
				dst += pitch;
			}
			x += 8;
			y -= 8;
			dst -= pitch * 8;
			dst += 8;
			++images;
		}
		x -= 32;
		y += 8;
		dst += pitch * 8;
		dst -= 32;
	}
}

static inline void draw_tile(tileset_image_data& img, size_t megatile_index, uint8_t* dst, size_t pitch, size_t offset_x, size_t offset_y, size_t width, size_t height) {
	if (offset_x == 0 && offset_y == 0 && width == 32 && height == 32) {
		draw_tile<false>(img, megatile_index, dst, pitch, offset_x, offset_y, width, height);
	} else {
		draw_tile<true>(img, megatile_index, dst, pitch, offset_x, offset_y, width, height);
	}
}

template<bool bounds_check, bool flipped, bool textured, typename remap_F>
void draw_frame(const grp_t::frame_t& frame, const uint8_t* texture, uint8_t* dst, size_t pitch, size_t offset_x, size_t offset_y, size_t width, size_t height, remap_F&& remap_f) {
	for (size_t y = 0; y != offset_y; ++y) {
		dst += pitch;
		if (textured) texture += frame.size.x;
	}

	for (size_t y = offset_y; y != height; ++y) {

		if (flipped) dst += frame.size.x - 1;
		if (textured && flipped) texture += frame.size.x - 1;

		const uint8_t* d = frame.data_container.data() + frame.line_data_offset.at(y);
		for (size_t x = flipped ? frame.size.x - 1 : 0; x != (flipped ? (size_t)0 - 1 : frame.size.x);) {
			int v = *d++;
			if (v & 0x80) {
				v &= 0x7f;
				x += flipped ? -v : v;
				dst += flipped ? -v : v;
				if (textured) texture += flipped ? -v : v;
			} else if (v & 0x40) {
				v &= 0x3f;
				int c = *d++;
				for (;v; --v) {
					if (!bounds_check || (x >= offset_x && x < width)) {
						*dst = remap_f(textured ? *texture : c, *dst);
					}
					dst += flipped ? -1 : 1;
					x += flipped ? -1 : 1;
					if (textured) texture += flipped ? -1 : 1;
				}
			} else {
				for (;v; --v) {
					int c = *d++;
					if (!bounds_check || (x >= offset_x && x < width)) {
						*dst = remap_f(textured ? *texture : c, *dst);
					}
					dst += flipped ? -1 : 1;
					x += flipped ? -1 : 1;
					if (textured) texture += flipped ? -1 : 1;
				}
			}
		}

		if (!flipped) dst -= frame.size.x;
		else ++dst;
		dst += pitch;
		if (textured) {
			if (!flipped) texture -= frame.size.x;
			else ++texture;
			texture += frame.size.x;
		}

	}
}

struct no_remap {
	uint8_t operator()(uint8_t new_value, [[maybe_unused]] uint8_t old_value) const {
		return new_value;
	}
};

template<typename remap_F = no_remap>
void draw_frame(const grp_t::frame_t& frame, bool flipped, uint8_t* dst, size_t pitch, size_t offset_x, size_t offset_y, size_t width, size_t height, remap_F&& remap_f = remap_F()) {
	if (offset_x == 0 && offset_y == 0 && width == frame.size.x && height == frame.size.y) {
		if (flipped) draw_frame<false, true, false>(frame, nullptr, dst, pitch, offset_x, offset_y, width, height, std::forward<remap_F>(remap_f));
		else draw_frame<false, false, false>(frame, nullptr, dst, pitch, offset_x, offset_y, width, height, std::forward<remap_F>(remap_f));
	} else {
		if (flipped) draw_frame<true, true, false>(frame, nullptr, dst, pitch, offset_x, offset_y, width, height, std::forward<remap_F>(remap_f));
		else draw_frame<true, false, false>(frame, nullptr, dst, pitch, offset_x, offset_y, width, height, std::forward<remap_F>(remap_f));
	}
}

template<typename remap_F = no_remap>
void draw_frame_textured(const grp_t::frame_t& frame, const uint8_t* texture, bool flipped, uint8_t* dst, size_t pitch, size_t offset_x, size_t offset_y, size_t width, size_t height, remap_F&& remap_f = remap_F()) {
	if (offset_x == 0 && offset_y == 0 && width == frame.size.x && height == frame.size.y) {
		if (flipped) draw_frame<false, true, true>(frame, texture, dst, pitch, offset_x, offset_y, width, height, std::forward<remap_F>(remap_f));
		else draw_frame<false, false, true>(frame, texture, dst, pitch, offset_x, offset_y, width, height, std::forward<remap_F>(remap_f));
	} else {
		if (flipped) draw_frame<true, true, true>(frame, texture, dst, pitch, offset_x, offset_y, width, height, std::forward<remap_F>(remap_f));
		else draw_frame<true, false, true>(frame, texture, dst, pitch, offset_x, offset_y, width, height, std::forward<remap_F>(remap_f));
	}
}

struct apm_t {
	a_deque<int> history;
	int current_apm = 0;
	int last_frame_div = 0;
	static const int resolution = 1;
	void add_action(int frame) {
		if (!history.empty() && frame / resolution == last_frame_div) {
			++history.back();
		} else {
			if (history.size() >= 10 * 1000 / 42 / resolution) history.pop_front();
			history.push_back(1);
			last_frame_div = frame / 12;
		}
	}
	void update(int frame) {
		if (history.empty() || frame / resolution != last_frame_div) {
			if (history.size() >= 10 * 1000 / 42 / resolution) history.pop_front();
			history.push_back(0);
			last_frame_div = frame / resolution;
		}
		if (frame % resolution) return;
		if (history.size() == 0) {
			current_apm = 0;
			return;
		}
		int sum = 0;
		for (auto& v : history) sum += v;
		current_apm = (int)(sum * ((int64_t)256 * 60 * 1000 / 42 / resolution) / history.size() / 256);
	}
};

struct ui_util_functions: replay_functions {

	explicit ui_util_functions(state& st, action_state& action_st, replay_state& replay_st) : replay_functions(st, action_st, replay_st) {}



};

struct ui_functions: ui_util_functions {
	image_data img;
	tileset_image_data tileset_img;
	int2 draw_size;

	std::tuple<
		simple::graphical::initializer,
		simple::interactive::initializer
	> simple_init;
	std::optional<simple::graphical::software_window> wnd;
	surface rgba_surface;
	surface indexed_surface;
	bool draw_ui_elements = true;

	bool exit_on_close = true;
	bool window_closed = false;

	simple::geom::segment<int2> view;

	game_player player;
	replay_state current_replay_state;
	action_state current_action_state;
	std::array<apm_t, 12> apm;

	action_functions* actions_proxy;
	std::optional<user_input_handler> user_input;

	ui_functions
	(
		game_player player,
		int2 draw_size,
		bool create_window
	) :
		ui_util_functions
		(
			player.st(),
			current_action_state,
			current_replay_state
		),
		draw_size(draw_size),
		simple_init(),
		wnd(create_window
			?  std::optional<simple::graphical::software_window>(
				std::in_place, "OpenBW", draw_size,
				simple::graphical::window::flags::resizable)
			: std::nullopt
		),
		rgba_surface(draw_size, simple::graphical::pixel_format::type::rgba32),
		indexed_surface(draw_size, simple::graphical::pixel_format::type::index8),
		view{draw_size, int2::zero()},
		player(std::move(player)), actions_proxy(this), user_input()
	{
		rgba_surface.blend(simple::graphical::blend_mode::none);
		indexed_surface.blend(simple::graphical::blend_mode::none);
		// rgba_surface.alpha(0);
		// indexed_surface.alpha(0);
	}

	void enable_user_input(int owner)
	{
		if(wnd)
		{
			user_input.emplace(*wnd, owner, *actions_proxy, view);
		}
	}

	std::function<void(a_vector<uint8_t>&, a_string)> load_data_file;

	sound_types_t sound_types;
	a_vector<a_string> sound_filenames;

	const sound_type_t* get_sound_type(Sounds id) const {
		if ((size_t)id >= (size_t)Sounds::None) error("invalid sound id %d", (size_t)id);
		return &sound_types.vec[(size_t)id];
	}

	a_vector<bool> has_loaded_sound;
	a_vector<std::unique_ptr<native_sound::sound>> loaded_sounds;
	a_vector<std::chrono::high_resolution_clock::time_point> last_played_sound;

	int global_volume = 50;

	struct sound_channel {
		bool playing = false;
		const sound_type_t* sound_type = nullptr;
		int priority = 0;
		int flags = 0;
		const unit_type_t* unit_type = nullptr;
		int volume = 0;
	};
	a_vector<sound_channel> sound_channels;

	void set_volume(int volume) {
		if (volume < 0) volume = 0;
		else if (volume > 100) volume = 100;
		global_volume = volume;
		for (auto& c : sound_channels) {
			if (c.playing) {
				native_sound::set_volume(&c - sound_channels.data(), (128 - 4) * (c.volume * global_volume / 100) / 100);
			}
		}
	}

	sound_channel* get_sound_channel(int priority) {
		sound_channel* r = nullptr;
		for (auto& c : sound_channels) {
			if (c.playing) {
				if (!native_sound::is_playing(&c - sound_channels.data())) {
					c.playing = false;
					r = &c;
				}
			} else r = &c;
		}
		if (r) return r;
		int best_prio = priority;
		for (auto& c : sound_channels) {
			if (c.flags & 0x20) continue;
			if (c.priority < best_prio) {
				best_prio = c.priority;
				r = &c;
			}
		}
		return r;
	}

	virtual void play_sound(int id, xy position, const unit_t* source_unit, bool add_race_index) override {
		if (global_volume == 0) return;
		if (add_race_index) id += 1;
		if ((size_t)id >= has_loaded_sound.size()) return;
		if (!has_loaded_sound[id]) {
			has_loaded_sound[id] = true;
			a_vector<uint8_t> data;
			load_data_file(data, "sound/" + sound_filenames[id]);
			loaded_sounds[id] = native_sound::load_wav(data.data(), data.size());
		}
		auto& s = loaded_sounds[id];
		if (!s) return;

		auto now = clock.now();
		if (now - last_played_sound[id] <= std::chrono::milliseconds(80)) return;
		last_played_sound[id] = now;

		const sound_type_t* sound_type = get_sound_type((Sounds)id);

		int volume = sound_type->min_volume;

		if (position != xy()) {
			int distance = 0;
			if (position.x < view.position.x()) distance += view.position.x() - position.x;
			else if (position.x > view.position.x() + (int)draw_size.x()) distance += position.x - (view.position.x() + (int)draw_size.x());
			if (position.y < view.position.y()) distance += view.position.y() - position.y;
			else if (position.y > view.position.y() + (int)draw_size.y()) distance += position.y - (view.position.y() + (int)draw_size.y());

			int distance_volume = 99 - 99 * distance / 512;

			if (distance_volume > volume) volume = distance_volume;
		}

		if (volume > 10) {
			int pan = 0;
//			if (position != xy()) {
//				int pan_x = (position.x - (view.position.x() + (int)draw_size.x() / 2)) / 32;
//				bool left = pan_x < 0;
//				if (left) pan_x = -pan_x;
//				if (pan_x <= 2) pan = 0;
//				else if (pan_x <= 5) pan = 52;
//				else if (pan_x <= 10) pan = 127;
//				else if (pan_x <= 20) pan = 191;
//				else if (pan_x <= 40) pan = 230;
//				else pan = 255;
//				if (left) pan = -pan;
//			}

			const unit_type_t* unit_type = source_unit ? source_unit->unit_type : nullptr;

			if (sound_type->flags & 0x10) {
				for (auto& c : sound_channels) {
					if (c.playing && c.sound_type == sound_type) {
						if (native_sound::is_playing(&c - sound_channels.data())) return;
						c.playing = false;
					}
				}
			} else if (sound_type->flags & 2 && unit_type) {
				for (auto& c : sound_channels) {
					if (c.playing && c.unit_type == unit_type && c.flags & 2) {
						if (native_sound::is_playing(&c - sound_channels.data())) return;
						c.playing = false;
					}
				}
			}

			auto* c = get_sound_channel(sound_type->priority);
			if (c) {
				native_sound::play(c - sound_channels.data(), &*s, (128 - 4) * (volume * global_volume / 100) / 100, pan);
				c->playing = true;
				c->sound_type = sound_type;
				c->flags = sound_type->flags;
				c->priority = sound_type->priority;
				c->unit_type = unit_type;
				c->volume = volume;
			}
		}
	}

	a_vector<uint8_t> creep_random_tile_indices = a_vector<uint8_t>(256 * 256);
	void init() {
		uint32_t rand_state = (uint32_t)clock.now().time_since_epoch().count();
		auto rand = [&]() {
			rand_state = rand_state * 22695477 + 1;
			return (rand_state >> 16) & 0x7fff;
		};
		for (auto& v : creep_random_tile_indices) {
			if (rand() % 100 < 4) v = 6 + rand() % 7;
			else v = rand() % 6;
		}

		a_vector<uint8_t> data;
		load_data_file(data, "arr/sfxdata.dat");
		sound_types = data_loading::load_sfxdata_dat(data);

		sound_filenames.resize(sound_types.vec.size());
		has_loaded_sound.resize(sound_filenames.size());
		loaded_sounds.resize(has_loaded_sound.size());
		last_played_sound.resize(loaded_sounds.size());

		string_table_data tbl;
		load_data_file(tbl.data, "arr/sfxdata.tbl");
		for (size_t i = 0; i != sound_types.vec.size(); ++i) {
			size_t index = sound_types.vec[i].filename_index;
			sound_filenames[i] = tbl[index];
		}
		native_sound::frequency = 44100;
		native_sound::channels = 8;
		native_sound::init();

		sound_channels.resize(8);

		load_data_file(images_tbl.data, "arr/images.tbl");

		load_all_image_data(load_data_file);

		set_image_data();
	}

	virtual void on_action(int owner, [[maybe_unused]] int action) override {
		apm.at(owner).add_action(st.current_frame);
	}

	rect_t<xy_t<size_t>> screen_tile_bounds() {
		size_t from_tile_y = view.position.y() / 32u;
		if (from_tile_y >= game_st.map_tile_height) from_tile_y = 0;
		size_t to_tile_y = (view.position.y() + view.size.y() + 31) / 32u;
		if (to_tile_y > game_st.map_tile_height) to_tile_y = game_st.map_tile_height;
		size_t from_tile_x = view.position.x() / 32u;
		if (from_tile_x >= game_st.map_tile_width) from_tile_x = 0;
		size_t to_tile_x = (view.position.x() + view.size.x() + 31) / 32u;
		if (to_tile_x > game_st.map_tile_width) to_tile_x = game_st.map_tile_width;

		return {{from_tile_x, from_tile_y}, {to_tile_x, to_tile_y}};
	}


	void draw_tiles(pixel_writer pixels) {
		uint8_t* data = pixels.row();
		size_t data_pitch = pixels.pitch();

		auto screen_tile = screen_tile_bounds();

		size_t tile_index = screen_tile.from.y * game_st.map_tile_width + screen_tile.from.x;
		auto* megatile_index = &st.tiles_mega_tile_index[tile_index];
		auto* tile = &st.tiles[tile_index];
		size_t width = screen_tile.to.x - screen_tile.from.x;

		xy dirs[9] = {{1, 1}, {0, 1}, {-1, 1}, {1, 0}, {-1, 0}, {1, -1}, {0, -1}, {-1, -1}, {0, 0}};

		for (size_t tile_y = screen_tile.from.y; tile_y != screen_tile.to.y; ++tile_y) {
			for (size_t tile_x = screen_tile.from.x; tile_x != screen_tile.to.x; ++tile_x) {

				int screen_x = tile_x * 32 - view.position.x();
				int screen_y = tile_y * 32 - view.position.y();

				size_t offset_x = 0;
				size_t offset_y = 0;
				if (screen_x < 0) {
					offset_x = -screen_x;
				}
				if (screen_y < 0) {
					offset_y = -screen_y;
				}

				uint8_t* dst = data + screen_y * data_pitch + screen_x;

				size_t width = 32;
				size_t height = 32;

				width = std::min(width, size_t(draw_size.x() - screen_x));
				height = std::min(height, size_t(draw_size.y() - screen_y));

				size_t index = *megatile_index;
				if (tile->flags & tile_t::flag_has_creep) {
					index = game_st.cv5.at(1).mega_tile_index[creep_random_tile_indices[tile_x + tile_y * game_st.map_tile_width]];
				}
				draw_tile(tileset_img, index, dst, data_pitch, offset_x, offset_y, width, height);

				if (~tile->flags & tile_t::flag_has_creep) {
					size_t creep_index = 0;
					for (size_t i = 0; i != 9; ++i) {
						int add_x = dirs[i].x;
						int add_y = dirs[i].y;
						if (tile_x + add_x >= game_st.map_tile_width) continue;
						if (tile_y + add_y >= game_st.map_tile_height) continue;
						if (st.tiles[tile_x + add_x + (tile_y + add_y) * game_st.map_tile_width].flags & tile_t::flag_has_creep) creep_index |= 1 << i;
					}
					size_t creep_frame = img.creep_edge_frame_index[creep_index];

					if (creep_frame) {

						auto& frame = tileset_img.creep_grp.frames.at(creep_frame - 1);

						screen_x += frame.offset.x;
						screen_y += frame.offset.y;

						size_t width = frame.size.x;
						size_t height = frame.size.y;

						if (screen_x < (int)draw_size.x() && screen_y < (int)draw_size.y()) {
							if (screen_x + (int)width > 0 && screen_y + (int)height > 0) {

								size_t offset_x = 0;
								size_t offset_y = 0;
								if (screen_x < 0) {
									offset_x = -screen_x;
								}
								if (screen_y < 0) {
									offset_y = -screen_y;
								}

								uint8_t* dst = data + screen_y * data_pitch + screen_x;

								width = std::min(width, size_t(draw_size.x() - screen_x));
								height = std::min(height, size_t(draw_size.y() - screen_y));

								draw_frame(frame, false, dst, data_pitch, offset_x, offset_y, width, height);
							}
						}
					}
				}

				if(user_input && not user_input->is_visible(*tile))
				{
					fill_rectangle(pixels, simple::geom::segment{int2(width, height), int2(screen_x + offset_x, screen_y + offset_y)},  0);
				}

				++megatile_index;
				++tile;
			}
			megatile_index -= width;
			megatile_index += game_st.map_tile_width;
			tile -= width;
			tile += game_st.map_tile_width;
		}
	}

	a_vector<uint8_t> temporary_warp_texture_buffer;

	void draw_image(const image_t* image, uint8_t* data, size_t data_pitch, size_t color_index) {

		if (is_new_image(image)) {
			image_draw_queue.push_back(image);
			return;
		}

		xy map_pos = get_image_map_position(image);

		int screen_x = map_pos.x - view.position.x();
		int screen_y = map_pos.y - view.position.y();

		if (screen_x >= (int)draw_size.x() || screen_y >= (int)draw_size.y()) return;

		auto& frame = image->grp->frames.at(image->frame_index);

		size_t width = frame.size.x;
		size_t height = frame.size.y;

		if (screen_x + (int)width <= 0 || screen_y + (int)height <= 0) return;

		size_t offset_x = 0;
		size_t offset_y = 0;
		if (screen_x < 0) {
			offset_x = -screen_x;
		}
		if (screen_y < 0) {
			offset_y = -screen_y;
		}

		uint8_t* dst = data + screen_y * data_pitch + screen_x;

		width = std::min(width, size_t(draw_size.x() - screen_x));
		height = std::min(height, size_t(draw_size.y() - screen_y));

		auto draw_alpha = [&](size_t index, auto remap_f) {
			auto& data = tileset_img.light_pcx.at(index).data;
			uint8_t* ptr = data.data();
			size_t size = data.size() / 256;
			auto glow = [ptr, size, remap_f](uint8_t new_value, uint8_t old_value) {
				new_value = remap_f(new_value, old_value);
				--new_value;
				if (new_value >= size) return (uint8_t)0;
				return ptr[256u * new_value + old_value];
			};
			draw_frame(frame, i_flag(image, image_t::flag_horizontally_flipped), dst, data_pitch, offset_x, offset_y, width, height, glow);
		};

		if (image->modifier == 0 || image->modifier == 1) {
			uint8_t* ptr = img.player_unit_colors.at(color_index).data();
			auto player_color = [ptr](uint8_t new_value, uint8_t) {
				if (new_value >= 8 && new_value < 16) return ptr[new_value - 8];
				return new_value;
			};
			draw_frame(frame, i_flag(image, image_t::flag_horizontally_flipped), dst, data_pitch, offset_x, offset_y, width, height, player_color);
		} else if (image->modifier == 2 || image->modifier == 4) {
			uint8_t* color_ptr = img.player_unit_colors.at(color_index).data();
			draw_alpha(4, [color_ptr](uint8_t new_value, uint8_t) {
				if (new_value >= 8 && new_value < 16) return color_ptr[new_value - 8];
				return new_value;
			});
			uint8_t* selector = tileset_img.cloak_fade_selector.data();
			int value = image->modifier_data1;
			auto cloaking = [color_ptr, selector, value](uint8_t new_value, uint8_t old_value) {
				if (selector[new_value] <= value) return old_value;
				if (new_value >= 8 && new_value < 16) return color_ptr[new_value - 8];
				return new_value;
			};
			draw_frame(frame, i_flag(image, image_t::flag_horizontally_flipped), dst, data_pitch, offset_x, offset_y, width, height, cloaking);
		} else if (image->modifier == 3) {
			uint8_t* color_ptr = img.player_unit_colors.at(color_index).data();
			draw_alpha(4, [color_ptr](uint8_t new_value, uint8_t) {
				if (new_value >= 8 && new_value < 16) return color_ptr[new_value - 8];
				return new_value;
			});
		} else if (image->modifier == 8) {
			size_t data_size = data_pitch * draw_size.y();
			auto distortion = [data_size, dst](uint8_t new_value, uint8_t& old_value) {
				size_t offset = &old_value - dst;
				if (offset >= new_value && data_size - offset > new_value) return *(&old_value + new_value);
				return old_value;
			};
			draw_frame(frame, i_flag(image, image_t::flag_horizontally_flipped), dst, data_pitch, offset_x, offset_y, width, height, distortion);
		} else if (image->modifier == 10) {
			uint8_t* ptr = &tileset_img.dark_pcx.data[256 * 18];
			auto shadow = [ptr](uint8_t, uint8_t old_value) {
				return ptr[old_value];
			};
			draw_frame(frame, i_flag(image, image_t::flag_horizontally_flipped), dst, data_pitch, offset_x, offset_y, width, height, shadow);
		} else if (image->modifier == 9) {
			draw_alpha(image->image_type->color_shift - 1, no_remap());
		} else if (image->modifier == 12) {
			if (temporary_warp_texture_buffer.size() < frame.size.x * frame.size.y) temporary_warp_texture_buffer.resize(frame.size.x * frame.size.y);
			auto& texture_frame = global_st.image_grp[(size_t)ImageTypes::IMAGEID_Warp_Texture]->frames.at(image->modifier_data1);
			draw_frame(texture_frame, false, temporary_warp_texture_buffer.data(), frame.size.x, 0, 0, frame.size.x, frame.size.y);
			draw_frame_textured(frame, temporary_warp_texture_buffer.data(), i_flag(image, image_t::flag_horizontally_flipped), dst, data_pitch, offset_x, offset_y, width, height);
		} else if (image->modifier == 17) {
			auto& data = tileset_img.light_pcx.at(0).data;
			uint8_t* ptr = &data.at(256u * (image->modifier_data1 - 1));
			size_t size = data.data() + data.size() - ptr;
			auto glow = [ptr, size](uint8_t, uint8_t old_value) {
				if (old_value >= size) return (uint8_t)0;
				return ptr[old_value];
			};
			draw_frame(frame, i_flag(image, image_t::flag_horizontally_flipped), dst, data_pitch, offset_x, offset_y, width, height, glow);
		} else error("don't know how to draw image modifier %d", image->modifier);

	}

	a_vector<const unit_t*> current_selection_sprites_set = a_vector<const unit_t*>(2500);
	a_vector<const sprite_t*> current_selection_sprites;

	void draw_selection_circle(const sprite_t* sprite, const unit_t* u, uint8_t* data, size_t data_pitch) {
		auto* image_type = get_image_type((ImageTypes)((int)ImageTypes::IMAGEID_Selection_Circle_22pixels + sprite->sprite_type->selection_circle));

		xy map_pos = sprite->position + xy(0, sprite->sprite_type->selection_circle_vpos);

		auto* grp = global_st.image_grp[(size_t)image_type->id];
		auto& frame = grp->frames.at(0);

		map_pos.x += int(frame.offset.x - grp->width / 2);
		map_pos.y += int(frame.offset.y - grp->height / 2);

		int screen_x = map_pos.x - view.position.x();
		int screen_y = map_pos.y - view.position.y();

		if (screen_x >= (int)draw_size.x() || screen_y >= (int)draw_size.y()) return;

		size_t width = frame.size.x;
		size_t height = frame.size.y;

		if (screen_x + (int)width <= 0 || screen_y + (int)height <= 0) return;

		size_t offset_x = 0;
		size_t offset_y = 0;
		if (screen_x < 0) {
			offset_x = -screen_x;
		}
		if (screen_y < 0) {
			offset_y = -screen_y;
		}

		uint8_t* dst = data + screen_y * data_pitch + screen_x;

		width = std::min(width, size_t(draw_size.x() - screen_x));
		height = std::min(height, size_t(draw_size.y() - screen_y));

		size_t color_index = st.players[sprite->owner].color;
		uint8_t color = img.player_unit_colors.at(color_index)[0];
		if (unit_is_mineral_field(u) || unit_is(u, UnitTypes::Resource_Vespene_Geyser)) {
			color = tileset_img.resource_minimap_color;
		}
		auto player_color = [color](uint8_t new_value, uint8_t) {
			if (new_value < 8) return color;
			return new_value;
		};
		draw_frame(frame, false, dst, data_pitch, offset_x, offset_y, width, height, player_color);

	}

	void draw_health_bars(const sprite_t* sprite, const unit_t* u, uint8_t* data, size_t data_pitch) {

		auto* selection_circle_image_type = get_image_type((ImageTypes)((int)ImageTypes::IMAGEID_Selection_Circle_22pixels + sprite->sprite_type->selection_circle));

		auto* selection_circle_grp = global_st.image_grp[(size_t)selection_circle_image_type->id];
		auto& selection_circle_frame = selection_circle_grp->frames.at(0);

		int offsety = sprite->sprite_type->selection_circle_vpos + selection_circle_frame.size.y / 2 + 8;

		bool has_shield = u->unit_type->has_shield;
		bool has_energy = ut_has_energy(u) || u_hallucination(u) || unit_is(u, UnitTypes::Zerg_Broodling);

		int width = sprite->sprite_type->health_bar_size;
		width -= (width - 1) % 3;
		if (width < 19) width = 19;
		int orig_width = width;
		int height = 5;
		if (has_shield) height += 2;
		if (has_energy) height += 6;

		xy map_pos = sprite->position + xy(0, offsety);

		map_pos.x += int(0 - width / 2);
		map_pos.y += int(0 - height / 2);

		int screen_x = map_pos.x - view.position.x();
		int screen_y = map_pos.y - view.position.y();

		if (screen_x >= (int)draw_size.x() || screen_y >= (int)draw_size.y()) return;
		if (screen_x + width <= 0 || screen_y + height <= 0) return;

		auto filled_width = [&](int percent) {
			int r = percent * width / 100;
			if (r < 3) r = 3;
			else if (r % 3) {
				if (r % 3 > 1) r += 3 - (r % 3);
				else r -= r % 3;
			}
			return r;
		};

		int hp_percent = unit_hp_percent(u);
		int dw = filled_width(hp_percent);

		int shield_dw = 0;
		if (has_shield) {
			int shield_percent = (int)u->shield_points.integer_part() * 100 / std::max(u->unit_type->shield_points, 1);
			shield_dw = filled_width(shield_percent);
		}

		int energy_dw = 0;
		if (has_energy) {
			int energy_percent;
			if (ut_has_energy(u)) energy_percent = (int)u->energy.integer_part() * 100 / std::max((int)unit_max_energy(u).integer_part(), 1);
			else energy_percent = (int)u->remove_timer / std::max((int)default_remove_timer(u), 1);
			energy_dw = filled_width(energy_percent);
		}

		const int no_shield_colors_66[] = {18, 0, 1, 2, 18};
		const int no_shield_colors_33[] = {18, 3, 4, 5, 18};
		const int no_shield_colors_0[] = {18, 6, 7, 8, 18};
		const int no_shield_colors_bg[] = {18, 15, 16, 17, 18};

		const int with_shield_colors_66[] = {18, 0, 0, 1, 1, 2, 18};
		const int with_shield_colors_33[] = {18, 3, 3, 4, 4, 5, 18};
		const int with_shield_colors_0[] = {18, 6, 6, 7, 7, 8, 18};
		const int with_shield_colors_bg[] = {18, 15, 15, 16, 16, 17, 18};

		const int* colors_66 = has_shield ? with_shield_colors_66 : no_shield_colors_66;
		const int* colors_33 = has_shield ? with_shield_colors_33 : no_shield_colors_33;
		const int* colors_0 = has_shield ? with_shield_colors_0 : no_shield_colors_0;
		const int* colors_bg = has_shield ? with_shield_colors_bg : no_shield_colors_bg;

		int offset_x = 0;
		int offset_y = 0;
		if (screen_x < 0) {
			offset_x = -screen_x;
			dw = std::max(dw + screen_x, 0);
			shield_dw = std::max(shield_dw + screen_x, 0);
			energy_dw = std::max(energy_dw + screen_x, 0);
			width = std::max(width + screen_x, 0);
			screen_x = 0;
		}
		if (screen_y < 0) {
			offset_y = -screen_y;
			height += screen_y;
			screen_y = 0;
		}

		uint8_t* dst = data + screen_y * data_pitch + screen_x;

		width = std::min(width, (int)draw_size.x() - screen_x);
		height = std::min(height, (int)draw_size.y() - screen_y);

		if (dw > width) dw = width;
		if (shield_dw > width) shield_dw = width;
		if (energy_dw > width) energy_dw = width;

		int hp_height = std::min(std::max((has_shield ? 7 : 5) - offset_y, 0), height);

		for (int i = offset_y; i < offset_y + hp_height; ++i) {
			int ci = hp_percent >= 66 ? colors_66[i] : hp_percent >= 33 ? colors_33[i] : colors_0[i];
			int c = img.hp_bar_colors.at(ci);

			if (dw > 0) memset(dst, c, dw);
			if (width - dw > 0) {
				c = img.hp_bar_colors.at(colors_bg[i]);
				memset(dst + dw, c, width - dw);
			}
			dst += data_pitch;
		}

		if (has_shield) {
			const int shield_colors[] = {18, 10, 11, 18};
			const int shield_colors_bg[] = {18, 16, 17, 18};

			dst = data + screen_y * data_pitch + screen_x;

			for (int i = offset_y; i < std::min(4, height); ++i) {
				int c = img.hp_bar_colors.at(shield_colors[i]);

				if (shield_dw > 0) memset(dst, c, shield_dw);
				if (width - shield_dw > 0) {
					c = img.hp_bar_colors.at(shield_colors_bg[i]);
					memset(dst + shield_dw, c, width - shield_dw);
				}
				dst += data_pitch;
			}
		}

		int energy_offset = std::max((has_shield ? 8 : 6) - offset_y, 0);
		int energy_begin = std::max(offset_y - (has_shield ? 8 : 6), 0);
		int energy_end = std::min(5, offset_y + height - (has_shield ? 8 : 6));

		if (has_energy ) {
			dst = data + (screen_y + energy_offset) * data_pitch + screen_x;
			const int energy_colors[] = {18, 12, 13, 14, 18};
			for (int i = energy_begin; i < energy_end; ++i) {
				int c = img.hp_bar_colors.at(energy_colors[i]);

				if (energy_dw > 0) memset(dst, c, energy_dw);
				if (width - energy_dw > 0) {
					c = img.hp_bar_colors.at(no_shield_colors_bg[i]);
					memset(dst + energy_dw, c, width - energy_dw);
				}
				dst += data_pitch;
			}
		}

		dst = data + screen_y * data_pitch + screen_x;
		if (offset_x % 3) dst += 3 - offset_x % 3;

		int c = img.hp_bar_colors.at(18);
		for (int x = 0; x < orig_width; x += 3) {
			if (x < offset_x || x >= offset_x + width) continue;
			for (int y = 0; y != hp_height; ++y) {
				*dst = c;
				dst += data_pitch;
			}
			if (has_energy && energy_end > energy_begin) {
				if (energy_offset) dst += data_pitch;
				for (int i = energy_begin; i != energy_end; ++i) {
					*dst = c;
					dst += data_pitch;
				}
				if (energy_offset) dst -= data_pitch;
				dst -= (energy_end - energy_begin) * data_pitch;
			}
			dst -= hp_height * data_pitch;
			dst += 3;
		}

	}

	void draw_sprite(const sprite_t* sprite, uint8_t* data, size_t data_pitch) {
		const unit_t* draw_selection_u = current_selection_sprites_set.at(sprite->index);
		const unit_t* draw_health_bars_u = draw_selection_u;
		for (auto* image : ptr(reverse(sprite->images))) {
			if (i_flag(image, image_t::flag_hidden)) continue;
			if (draw_selection_u && image->modifier != 10) {
				draw_selection_circle(sprite, draw_selection_u, data, data_pitch);
				draw_selection_u = nullptr;
			}
			draw_image(image, data, data_pitch, st.players[sprite->owner].color);
		}
		if (draw_health_bars_u && !u_invincible(draw_health_bars_u)) {
			draw_health_bars(sprite, draw_health_bars_u, data, data_pitch);
		}
	}

	a_vector<std::pair<uint32_t, const sprite_t*>> sorted_sprites;

	void draw_sprites(uint8_t* data, size_t data_pitch) {

		image_draw_queue.clear();

		sorted_sprites.clear();

		auto screen_tile = screen_tile_bounds();

		size_t from_y = screen_tile.from.y;
		if (from_y < 4) from_y = 0;
		else from_y -= 4;
		size_t to_y = screen_tile.to.y;
		if (to_y >= game_st.map_tile_height - 4) to_y = game_st.map_tile_height - 1;
		else to_y += 4;
		for (size_t y = from_y; y != to_y; ++y) {
			for (auto* sprite : ptr(st.sprites_on_tile_line.at(y))) {
				if (s_hidden(sprite)) continue;
				sorted_sprites.emplace_back(sprite_depth_order(sprite), sprite);
			}
		}

		std::sort(sorted_sprites.begin(), sorted_sprites.end());

		if(user_input)
		{
			for (auto u : actions_proxy->selected_units(user_input->owner)) {
				current_selection_sprites_set.at(u->sprite->index) = u;
				current_selection_sprites.push_back(u->sprite);
			}
		}
		else
		{
			for (auto uid : current_selection) {
				auto* u = get_unit(uid);
				if (!u) continue;
				current_selection_sprites_set.at(u->sprite->index) = u;
				current_selection_sprites.push_back(u->sprite);
			}
		}

		for (auto& [id, sprite] : sorted_sprites) {
			if(not user_input || user_input->is_visible(*sprite))
			{
				draw_sprite(sprite, data, data_pitch);
			}
		}

		for (auto* s : current_selection_sprites) {
			current_selection_sprites_set.at(s->index) = nullptr;
		}
		current_selection_sprites.clear();
	}

	bool unit_visble_on_minimap(unit_t* u) {
		if (u->owner < 8 && u->sprite->visibility_flags == 0) return false;
		if (user_input && not user_input->is_visible(*u)) return false;
		if (ut_turret(u)) return false;
		if (unit_is_trap(u)) return false;
		if (unit_is(u, UnitTypes::Spell_Dark_Swarm)) return false;
		if (unit_is(u, UnitTypes::Spell_Disruption_Web)) return false;
		return true;
	}

	simple::geom::segment<int2> get_minimap_area() {
		int minimap_width = std::max(game_st.map_tile_width, game_st.map_tile_height);
		int minimap_height = minimap_width;
		if (game_st.map_width < game_st.map_height) {
			minimap_width = minimap_width * minimap_width * game_st.map_tile_width / (minimap_height * game_st.map_tile_height);
		} else if (game_st.map_height < game_st.map_width) {
			minimap_height = minimap_height * minimap_height * game_st.map_tile_height / (minimap_width* game_st.map_tile_width);
		}
		if (draw_size.x() < minimap_width || draw_size.y() < minimap_height) return {};
		int map_screen_x = 4;
		int map_screen_y = draw_size.y() - 4 - minimap_height;
		return
		{
			int2(minimap_width, minimap_height),
			int2(map_screen_x, map_screen_y)
		};
	}

	void draw_minimap(pixel_writer pixels) {
		auto area = get_minimap_area();
		if(area.size != int2{game_st.map_tile_width, game_st.map_tile_height}) return;
		fill_rectangle(pixels, area, 0);
		outline_rectangle(pixels, area, 1, 0);

		auto minimap_pixels = pixel_writer(pixels, area);

		loop(minimap_pixels.size(), [this, &minimap_pixels](auto i)
		{
			int flat_i = i.y() * minimap_pixels.size().x() + i.x();
			auto& tile = st.tiles[flat_i];
			size_t index;
			if (~tile.flags & tile_t::flag_has_creep) index = st.tiles_mega_tile_index[flat_i];
			else index = game_st.cv5.at(1).mega_tile_index[creep_random_tile_indices[flat_i]];
			auto* images = &tileset_img.vx4.at(index).images[0];
			auto* bitmap = &tileset_img.vr4.at(*images / 2).bitmap[0];
			auto val = bitmap[55 / sizeof(vr4_entry::bitmap_t)];
			size_t shift = 8 * (55 % sizeof(vr4_entry::bitmap_t));
			val >>= shift;
			if(user_input && not user_input->is_visible(tile))
				val = 0;
			minimap_pixels.set(val, i);
		});

		for (size_t i = 12; i != 0;) {
			--i;
			for (unit_t* u : ptr(st.player_units[i])) {
				if (!unit_visble_on_minimap(u)) continue;

				int color = img.player_minimap_colors.at(st.players[u->owner].color);
				if (unit_is_mineral_field(u) || unit_is(u, UnitTypes::Resource_Vespene_Geyser)) {
					color = tileset_img.resource_minimap_color;
				}

				auto size = to_int2(u->unit_type->placement_size);
				size /= 32;
				auto min = int2::one(2);
				auto max = ut_building(u) ? int2::one(4) : area.size;
				size.clamp(min, max);

				simple::geom::segment unit_area
				{
					size,
					to_int2((u->sprite->position - u->unit_type->placement_size / 2) / 32u)
				};
				fill_rectangle(minimap_pixels, unit_area, color);
			}
		}

		line_rectangle( minimap_pixels, simple::geom::segment
			{
				(view.size + 31) / 32,
				view.position / 32
			},
			255);
	}

	int replay_frame = 0;

	simple::geom::segment<int2> get_replay_slider_area() {
#ifdef EMSCRIPTEN
		return {};
#endif
		int2 size(192, 32);
		int2 position = draw_size - 8 - int2::j(128) - size;
		if (not(position >= int2::zero())) return {};
		return {size, position};
	}

	void draw_ui(pixel_writer pixels) {
		auto area = get_replay_slider_area();
		if (area == simple::geom::segment<int2>{}) return;
		if (replay_st.end_frame == 0) return;
		fill_rectangle(pixels, area, 1);
		line_rectangle(pixels, area, 12);

		int2 button_size(16,32);
		int ow = area.size.x() - button_size.x();
		int ox = replay_frame * ow / replay_st.end_frame;

		if (st.current_frame != replay_frame) {
			int cox = st.current_frame * ow / replay_st.end_frame;
			line_rectangle(pixels,
				{
					area.position + int2(cox + button_size.x() / 2, 0),
					area.position + int2(cox + button_size.x() / 2 + 1, button_size.y())},
				50);
		}

		simple::geom::segment button_area
		{
			button_size,
			int2::i(ox) + area.position,
		};
		fill_rectangle(pixels, button_area, 10);
		line_rectangle(pixels, button_area, 51);


	}

	virtual void draw_callback([[maybe_unused]] uint8_t* data, [[maybe_unused]] size_t data_pitch) {
	}

	a_vector<const image_t*> image_draw_queue;

	bool use_new_images = false;

	bool new_images_index_loaded = false;

	a_vector<char> grp_new_image_state = a_vector<char>((size_t)ImageTypes::None);
	a_unordered_set<a_string> new_images_index;
	string_table_data images_tbl;

	// a_vector<a_vector<std::unique_ptr<native_window_drawing::surface>>> new_images;
	// a_vector<a_vector<std::unique_ptr<native_window_drawing::surface>>> new_images_flipped;

	bool is_new_image([[maybe_unused]] const image_t* image) {
		return false;
		// if (!use_new_images) return false;
		// if (!new_images_index_loaded) {
		// 	new_images_index_loaded = true;
		// 	async_read_file("index.txt", [this](const uint8_t* data, size_t len) {
		// 		if (!data) {
		// 			ui::log("failed to load index.txt :(\n");
		// 			return;
		// 		}
		// 		char* c = (char*)data;
		// 		char* e = (char*)data + len;
		// 		while (c != e && (*c == '\r' || *c =='\n' || *c == ' ')) ++c;
		// 		while (c != e) {
		// 			char* s = c;
		// 			while (c != e && *c != '\r' && *c !='\n' && *c != ' ') ++c;
		// 			a_string fn(s, c - s);
		// 			while (c != e && (*c == '\r' || *c =='\n' || *c == ' ')) ++c;
		// 			for (char& c : fn) {
		// 				if (c == '\\') c = '/';
		// 			}
		// 			if (!fn.empty() && fn.front() == '/') fn.erase(fn.begin());
		// 			if (!fn.empty() && fn.back() == '/') fn.erase(std::prev(fn.end()));
		// 			ui::log("index entry '%s'\n", fn);
		// 			new_images_index.insert(std::move(fn));
		// 		}
		// 	});
		// }
		// if (new_images_index.empty()) return false;
		// size_t index = image->grp - global_st.grps.data();
		// auto& state = grp_new_image_state.at(index);
		// if (state == 0) {
		// 	state = 2;
		// 	a_string fn = images_tbl.at(image->image_type->grp_filename_index);
		// 	for (char& c : fn) {
		// 		if (c == '\\') c = '/';
		// 	}
		// 	for (auto i = fn.rbegin(); i != fn.rend(); ++i) {
		// 		if (*i == '.') {
		// 			fn.erase(std::prev(i.base()), fn.end());
		// 		}
		// 		if (*i == '/') break;
		// 	}
		// 	if (!fn.empty() && fn.front() == '/') fn.erase(fn.begin());
		// 	fn = "unit/" + fn;
		// 	ui::log("checking '%s' (image %d, grp %u)\n", fn, (int)image->image_type->id, index);
		// 	if (new_images_index.count(fn)) {
		// 		size_t frames = image->grp->frames.size();
		// 		if (new_images.size() <= index) new_images.resize(index + 1);
		// 		new_images[index].resize(frames);
		// 		if (new_images_flipped.size() <= index) new_images_flipped.resize(index + 1);
		// 		new_images_flipped[index].resize(frames);
		// 		ui::log("loading %d frames...\n", frames);
		// 		auto frames_left = std::make_shared<size_t>(frames);
		// 		for (size_t i = 0; i != frames; ++i) {
		// 			a_string frame_fn = format("%s/%02u.png", fn, i);
		// 			async_read_file(frame_fn, [this, frame_fn, index, i, frames_left](const uint8_t* data, size_t len) {
		// 				if (!data) {
		// 					ui::log("failed to load '%s'\n", frame_fn);
		// 					return;
		// 				}
		// 				new_images.at(index).at(i) = native_window_drawing::load_image(data, len);
		// 				--*frames_left;
		// 				if (*frames_left == 0) {
		// 					grp_new_image_state.at(index) = 1;
        //
		// 					ui::log("grp %d successfully loaded %d frames\n", index, new_images.at(index).size());
		// 				}
		// 			});
		// 		}
		// 	}
		// }
		// //ui::log("index %d state %d\n", index, state);
		// return state == 1;
	}

	// native_window_drawing::surface* get_new_image_surface(const image_t* image, bool flipped) {
	// 	size_t index = image->grp - global_st.grps.data();
	// 	size_t frame = image->frame_index;
	// 	if (flipped) {
	// 		auto& r = new_images_flipped.at(index).at(frame);
	// 		if (!r) {
	// 			auto* s = new_images.at(index).at(frame).get();
	// 			if (!s) return nullptr;
	// 			r = flip_image(s);
	// 		}
	// 		return r.get();
	// 	} else {
	// 		return new_images.at(index).at(frame).get();
	// 	}
	// }

	// std::unique_ptr<native_window_drawing::surface> tmp_surface;

	// void draw_new_image(const image_t* image) {
	// 	xy map_pos = get_image_center_map_position(image);
    //
	// 	int screen_x = map_pos.x - view.position.x();
	// 	int screen_y = map_pos.y - view.position.y();
    //
	// 	auto* surface = get_new_image_surface(image, i_flag(image, image_t::flag_horizontally_flipped));
	// 	if (!surface) {
	// 		ui::log("ERROR: new image %d (grp %d) frame %d does not exist\n", (int)image->image_type->id, image->grp - global_st.grps.data(), image->frame_index);
	// 		return;
	// 	}
    //
	// 	auto scale = 114_fp8;
    //
	// 	size_t w = (fp8::integer(surface->w) * scale).integer_part();
	// 	size_t h = (fp8::integer(surface->w) * scale).integer_part();
    //
	// 	size_t orig_w = w;
	// 	size_t orig_h = h;
    //
	// 	screen_x -= w / 2;
	// 	screen_y -= w / 2;
    //
	// 	if (screen_x >= (int)draw_size.x() || screen_y >= (int)draw_size.y()) return;
	// 	if (screen_x + (int)w <= 0 || screen_y + (int)h <= 0) return;
    //
	// 	size_t offset_x = 0;
	// 	size_t offset_y = 0;
	// 	if (screen_x < 0) {
	// 		offset_x = -screen_x;
	// 		w += screen_x;
	// 		screen_x = 0;
	// 	}
	// 	if (screen_y < 0) {
	// 		offset_y = -screen_y;
	// 		h += screen_y;
	// 		screen_y = 0;
	// 	}
    //
	// 	w = std::min(w, draw_size.x() - screen_x);
	// 	h = std::min(h, draw_size.y() - screen_y);
    //
	// 	if (image->modifier == 10) {
	// 		if (!tmp_surface || (size_t)tmp_surface->w < orig_w || (size_t)tmp_surface->h < orig_h) {
	// 			tmp_surface = native_window_drawing::create_rgba_surface(orig_w, orig_h);
	// 		}
	// 		surface->set_blend_mode(native_window_drawing::blend_mode::none);
	// 		surface->blit_scaled(&*tmp_surface, 0, 0, orig_w, orig_h);
    //
	// 		size_t src_pitch = tmp_surface->pitch / 4;
	// 		size_t dst_pitch = rgba_surface->pitch / 4;
	// 		uint32_t* src = (uint32_t*)tmp_surface->lock();
	// 		uint32_t* dst = (uint32_t*)rgba_surface->lock();
    //
	// 		src += src_pitch * offset_y + offset_x;
	// 		dst += dst_pitch * screen_y + screen_x;
    //
	// 		size_t src_skip = src_pitch - w;
	// 		size_t dst_skip = dst_pitch - w;
    //
	// 		for (size_t y = h; y--;) {
    //
	// 			for (size_t x = w; x--;) {
	// 				uint32_t s = *src;
	// 				uint32_t d = *dst;
	// 				if (s >> 24 >= 16) *dst = (d & 0xfefefe) / 2 | 0xff000000;
	// 				++src;
	// 				++dst;
	// 			}
    //
	// 			src += src_skip;
	// 			dst += dst_skip;
	// 		}
    //
	// 		tmp_surface->unlock();
	// 		rgba_surface->unlock();
    //
	// 	} else {
	// 		surface->set_blend_mode(native_window_drawing::blend_mode::alpha);
	// 		surface->blit_scaled(&*rgba_surface, screen_x - offset_x, screen_y - offset_y, orig_w, orig_h);
	// 	}
	// }

	void draw_image_queue() {
		// for (auto* image : image_draw_queue) {
		// 	draw_new_image(image);
		// }
	}

	fp8 game_speed = fp8::integer(1);

	std::chrono::high_resolution_clock clock;
	std::chrono::high_resolution_clock::time_point last_draw;
	std::chrono::high_resolution_clock::time_point last_input_poll;
	std::chrono::high_resolution_clock::time_point last_fps;
	int fps_counter = 0;
	size_t scroll_speed_n = 0;

	void resize(int2 size) {

		draw_size = size;
		view.size = draw_size;

		if (wnd)
			wnd->update_surface();

		rgba_surface = bwgame::resize(rgba_surface, draw_size);
		indexed_surface = bwgame::resize(indexed_surface, draw_size);
	}

	a_vector<unit_id> current_selection;

	bool current_selection_is_selected(unit_t* u) {
		auto uid = get_unit_id(u);
		return std::find(current_selection.begin(), current_selection.end(), uid) != current_selection.end();
	}

	void current_selection_add(unit_t* u) {

		auto uid = get_unit_id(u);
		if (current_selection.size() == 12 || std::find(current_selection.begin(), current_selection.end(), uid) != current_selection.end()) return;
		current_selection.push_back(uid);
		temp_selected_unit_buffer.push_back(u);
	}

	void current_selection_clear() {
		current_selection.clear();
	}

	void current_selection_remove(const unit_t* u) {
		auto uid = get_unit_id(u);
		auto i = std::find(current_selection.begin(), current_selection.end(), uid);
		if (i != current_selection.end())
		{
			current_selection.erase(i);
		}
	}

	bool is_moving_minimap = false;
	bool is_moving_replay_slider = false;
	bool is_paused = false;
	bool is_drag_selecting = false;
	bool is_dragging_screen = false;
	sup::range<int2> drag_select{};
	int2 drag_screen_pos;

	std::vector<unit_t*> temp_selected_unit_buffer;

	void update() {
		auto now = clock.now();

		if (now - last_fps >= std::chrono::seconds(1)) {
			//ui::log("draw fps: %g\n", fps_counter / std::chrono::duration_cast<std::chrono::duration<double, std::ratio<1, 1>>>(now - last_fps).count());
			last_fps = now;
			fps_counter = 0;
		}
		++fps_counter;

		auto minimap_area = get_minimap_area();
		auto replay_slider_area = get_replay_slider_area();

		auto move_minimap = [&](int2 position) {

			// this is some kind of a generic thing, like get local coordinate in range
			auto minimap_valid_positions = minimap_area;
			minimap_valid_positions.size -= 1;
			// NOTE: wow, range clamp/intersect is really unwieldy with implicit conversion :/
			sup::clamp_in_place<int2>(position, minimap_valid_positions);
			position = position - minimap_area.position;

			auto tile_size = int2(game_st.map_tile_width, game_st.map_tile_height);
			position = position * tile_size / minimap_area.size;
			view.position = 32 * position - view.size / 2;
		};

		auto check_move_minimap = [&](auto& e) {
			if(sup::intersects_lower<int2>(minimap_area, e.data.position))
			{
				is_moving_minimap = true;
				move_minimap(e.data.position);
			}
		};

		auto move_replay_slider = [&](int2 position) {
			int x = position.x() - replay_slider_area.position.x();
			int button_w = 16;
			x -= button_w / 2;
			int ow = replay_slider_area.size.x() - button_w;
			if (x < 0) x = 0;
			if (x >= ow) x = ow - 1;
			replay_frame = x * replay_st.end_frame / ow;
		};

		auto check_move_replay_slider = [&](auto& e) {
			if(simple::support::intersects_lower<int2>(replay_slider_area, e.data.position))
			{
				is_moving_replay_slider = true;
				move_replay_slider(e.data.position);
			}
		};

		auto end_drag_select = [&](bool double_clicked) {
			if(user_input)
			{
				user_input->select(drag_select, double_clicked);
				is_drag_selecting = false;
				return;
			}

			using namespace simple::interactive;
			bool shift = pressed(scancode::lshift) || pressed(scancode::rshift);
			bool ctrl = pressed(scancode::lctrl) || pressed(scancode::rctrl);

			drag_select.fix();

			auto small_box = not(drag_select.upper() - drag_select.lower() > int2::one(4));

			if (small_box) {
				unit_t* u = select_get_unit_at(to_xy(view.position + drag_select.lower()));
				if (u) {
					if (double_clicked || ctrl) {
						if (!shift) current_selection_clear();
						auto is_tank = [&](unit_t* a) {
							return unit_is(a, UnitTypes::Terran_Siege_Tank_Siege_Mode) || unit_is(a, UnitTypes::Terran_Siege_Tank_Tank_Mode);
						};
						auto is_same_type = [&](unit_t* a, unit_t* b) {
							if (unit_is_mineral_field(a) && unit_is_mineral_field(b)) return true;
							if (is_tank(a) && is_tank(b)) return true;
							return a->unit_type == b->unit_type;
						};
						for (unit_t* u2 : find_units(to_rect(view))) {
							if (u2->owner != u->owner) continue;
							if (!is_same_type(u, u2)) continue;
							current_selection_add(u2);
						}
					} else {
						if (shift) {
							if (current_selection_is_selected(u)) current_selection_remove(u);
							else current_selection_add(u);
						} else {
							current_selection_clear();
							current_selection_add(u);
						}
					}
				}
			} else {
				if (!shift) current_selection_clear();
				a_vector<unit_t*> new_units;
				bool any_non_neutrals = false;
				for (unit_t* u : find_units(to_rect(drag_select + view.position))) {
					if (!unit_can_be_selected(u)) continue;
					new_units.push_back(u);
					if (u->owner != 11) any_non_neutrals = true;
				}
				for (unit_t* u : new_units) {
					if (u->owner == 11 && any_non_neutrals) continue;
					current_selection_add(u);
				}
			}
			is_drag_selecting = false;
		};

		using namespace simple::interactive;
		if (wnd) {
			while (auto e = next_event())
			{
				std::visit( simple::support::overloaded
				{
					[&](quit_request)
					{
						if (exit_on_close) std::exit(0);
						else window_closed = true;
					},
					[&](const window_resized& e)
					{
						resize(e.data.value);
					},
					[&](const mouse_down& e)
					{
						switch(e.data.button)
						{
						case mouse_button::left:
						{
							check_move_minimap(e);
							check_move_replay_slider(e);
							auto user_input_click = user_input && user_input->left_click(e);
							if (!is_moving_minimap && !is_moving_replay_slider && !user_input_click) {
								is_drag_selecting = true;
								drag_select.lower() = e.data.position;
								drag_select.upper() = e.data.position;
							}
						}
						break;
						case mouse_button::right:
							if(!user_input)
							{
								is_dragging_screen = true;
								drag_screen_pos = view.position + e.data.position;
							}
						break;
						default:;
						}
					},
					[&](const mouse_motion& e)
					{
						if (e.data.button_state && mouse_button_mask::left) {
							if (is_moving_minimap) check_move_minimap(e);
							if (is_moving_replay_slider) check_move_replay_slider(e);
							if (is_drag_selecting) {
								drag_select.upper() = e.data.position;
							}
						} else if (e.data.button_state && mouse_button_mask::right) {
							if (is_dragging_screen) {
								view.position = drag_screen_pos - e.data.position;
								//view.position -= xy((fp16::integer(e.mouse_x - drag_screen_x)).integer_part(), (fp16::integer(e.mouse_y - drag_screen_y)).integer_part());
							}
						}

						// weird
						if (is_drag_selecting && !(e.data.button_state && mouse_button_mask::left)) end_drag_select(false);
					},
					[&](const mouse_up& e)
					{
						if (e.data.button == mouse_button::left) {
							if (is_moving_minimap) is_moving_minimap = false;
							if (is_moving_replay_slider) is_moving_replay_slider = false;
							if (is_drag_selecting) {
								end_drag_select(e.data.clicks >= 2 && e.data.clicks % 2 == 0);
							}
						} else if (e.data.button == mouse_button::right) {
							if(!user_input)
								is_dragging_screen = false;
						}
					},
					[&](const key_pressed& e)
					{

						//if (e.sym == 'q') {
						//	use_new_images = !use_new_images;
						//}
#ifndef EMSCRIPTEN
						switch(e.data.keycode)
						{
							case keycode::space:
							case keycode::p:
								is_paused = !is_paused;
							break;

							case keycode::a:
							case keycode::u:
								if (game_speed < fp8::integer(128)) game_speed *= 2;
							break;

							case keycode::z:
							case keycode::d:
								if (game_speed > 2_fp8) game_speed /= 2;
							break;

							case keycode::backspace:
							{
								int t = 5 * 42 / 1000;
								if (replay_frame < t) replay_frame = 0;
								else replay_frame -= t;
							}
							break;

							default:;
						}
#endif
					},
					[](auto){}
				}, *e);

				if(user_input)
					user_input->handle_event(*e);
			}
		}

		if (wnd) {
			auto input_poll_speed = std::chrono::milliseconds(12);

			auto input_poll_t = now - last_input_poll;
			while (input_poll_t >= input_poll_speed) {
				if (input_poll_t >= input_poll_speed * 20) last_input_poll = now - input_poll_speed;
				else last_input_poll += input_poll_speed;
				std::array<int, 6> scroll_speeds = {2, 2, 4, 6, 6, 8};

				if (!is_drag_selecting) {
					int scroll_speed = scroll_speeds[scroll_speed_n];
					auto prev_screen_pos = view.position;
					if (pressed(scancode::k)) view.position.y() += scroll_speed;
					else if (pressed(scancode::i)) view.position.y() -= scroll_speed;
					if (pressed(scancode::l)) view.position.x() += scroll_speed;
					else if (pressed(scancode::j)) view.position.x() -= scroll_speed;
					if (view.position != prev_screen_pos) {
						if (scroll_speed_n != scroll_speeds.size() - 1) ++scroll_speed_n;
					} else scroll_speed_n = 0;
				}

				input_poll_t = now - last_input_poll;
			}

			if (is_moving_minimap) {
				move_minimap(simple::interactive::last_mouse_state().position);
			}
			if (is_moving_replay_slider) {
				move_replay_slider(simple::interactive::last_mouse_state().position);
			}
		}

		if (view.position.y() + view.size.y() > int(game_st.map_height)) view.position.y() = game_st.map_height - view.size.y();
		if (view.position.y() < 0) view.position.y() = 0;
		if (view.position.x() + view.size.x() > int(game_st.map_width)) view.position.x() = game_st.map_width - view.size.x();
		if (view.position.x() < 0) view.position.x() = 0;

		auto indexed_pixels = std::get<pixel_writer>(indexed_surface.pixels());
		auto data = indexed_pixels.row();
		draw_tiles(indexed_pixels);
		draw_sprites(data, indexed_pixels.pitch());

		draw_callback(data, indexed_pixels.pitch());

		if (draw_ui_elements) {
			draw_minimap(indexed_pixels);
			draw_ui(indexed_pixels);
		}

		fill(rgba_surface,rgba_surface.format().color(rgba_pixel::white(0)));
		blit(indexed_surface, rgba_surface);

		draw_image_queue();

		auto rgba_pixels = std::get<pixel_writer_rgba>(rgba_surface.pixels());

		if (is_drag_selecting) {
			if (is_drag_selecting) {
				line_rectangle_rgba(rgba_pixels, drag_select.fixed(), 0x10fc18ff_rgba);
			}
		}

		if(user_input)
			user_input->draw(rgba_pixels);

		if (wnd) {
			blit(rgba_surface, wnd->surface());
			wnd->update();
		}
	}

	std::tuple<int, int, uint32_t*> get_rgba_buffer() {
		auto rgba_pixels = std::get<pixel_writer_rgba>(rgba_surface.pixels());
		return std::make_tuple(rgba_pixels.pitch() / 4, rgba_pixels.size().y(), (uint32_t*)rgba_pixels.row());
	}

	template<typename cb_F>
	void async_read_file(a_string filename, cb_F cb) {
#ifdef EMSCRIPTEN
		auto uptr = std::make_unique<cb_F>(std::move(cb));
		auto f = [](void* ptr, uint8_t* data, size_t size) {
			cb_F* cb_p = (cb_F*)ptr;
			std::unique_ptr<cb_F> uptr(cb_p);
			(*cb_p)(data, size);
		};
		auto* a = filename.c_str();
		auto* b = (void(*)(void*, uint8_t*, size_t))f;
		auto* c = uptr.release();
		EM_ASM_({js_download_file($0, $1, $2);}, a, b, c);
#else
		filename = "data/" + filename;
		FILE* f = fopen(filename.c_str(), "rb");
		if (!f) {
			cb(nullptr, 0);
		} else {
			a_vector<uint8_t> data;
			fseek(f, 0, SEEK_END);
			data.resize(ftell(f));
			fseek(f, 0, SEEK_SET);
			data.resize(fread(data.data(), 1, data.size(), f));
			fclose(f);
			cb(data.data(), data.size());
		}
#endif
	}

	std::array<tileset_image_data, 8> all_tileset_img;

	template<typename load_data_file_F>
	void load_all_image_data(load_data_file_F&& load_data_file) {
		load_image_data(img, load_data_file);
		for (size_t i = 0; i != 8; ++i) {
			load_tileset_image_data(all_tileset_img[i], i, load_data_file);
		}
	}

	void set_image_data() {
		auto palette = *indexed_surface.format().palette();
		tileset_img = all_tileset_img.at(game_st.tileset_index);

		if (tileset_img.wpe.size() != 256 * 4) error("wpe size invalid (%d)", tileset_img.wpe.size());
		for (size_t i = 0; i != 256; ++i) {
			palette.set_color(i, rgba_pixel(
				tileset_img.wpe[4 * i + 0],
				tileset_img.wpe[4 * i + 1],
				tileset_img.wpe[4 * i + 2],
				tileset_img.wpe[4 * i + 3]
			));
		}
	}

	void reset() {
		apm = {};
		replay_frame = 0;
		auto& game = *st.game;
		st = state();
		game = game_state();
		replay_st = replay_state();
		action_st = action_state();

		st.global = &global_st;
		st.game = &game;
	}
};

}

