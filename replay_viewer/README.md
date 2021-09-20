# Fugly changes

- Basic user input (select, right click, building placement and some hotkeys)
- External wrapper libraries for SDL2
- License is now GPL3, sorry proprietary apps

# Dependencies

- [libsdl2](https://libsdl.org)
- [boost_asio](https://libsdl.org)
- [libsimple_graphical](https://notabug.org/namark/libsimple_graphical)
- [libsimple_interactive](https://notabug.org/namark/libsimple_interactive)
- [libsimple_sdlcore](https://notabug.org/namark/libsimple_sdlcore)
- [libsimple_geom](https://notabug.org/namark/libsimple_geom)
- [libsimple_support](https://notabug.org/namark/libsimple_support)
- [cpp_tools](https://notabug.org/namark/cpp_tools)

# Build Instructions

This is a single binary application. Dependencies can be installed in this directory as prefix, instead of system wide. Afterwards:

```
make
./out/replay_viewer [replay_file]
```

The mpq files must be available in the "current directory", so you can run the replay_viewer in your game folder or copy the relevant files over
