add_library(PicoVGA INTERFACE)

target_sources(PicoVGA INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/vga.cpp
        ${CMAKE_CURRENT_LIST_DIR}/vga_layer.cpp
        ${CMAKE_CURRENT_LIST_DIR}/vga_pal.cpp
        ${CMAKE_CURRENT_LIST_DIR}/vga_screen.cpp
        ${CMAKE_CURRENT_LIST_DIR}/vga_util.cpp
        ${CMAKE_CURRENT_LIST_DIR}/vga_vmode.cpp
        ${CMAKE_CURRENT_LIST_DIR}/vga_blitkey.S
        ${CMAKE_CURRENT_LIST_DIR}/vga_render.S
        ${CMAKE_CURRENT_LIST_DIR}/util/overclock.cpp
        ${CMAKE_CURRENT_LIST_DIR}/render/vga_atext.S
        ${CMAKE_CURRENT_LIST_DIR}/render/vga_attrib8.S
        ${CMAKE_CURRENT_LIST_DIR}/render/vga_color.S
        ${CMAKE_CURRENT_LIST_DIR}/render/vga_ctext.S
        ${CMAKE_CURRENT_LIST_DIR}/render/vga_dtext.S
        ${CMAKE_CURRENT_LIST_DIR}/render/vga_fastsprite.S
        ${CMAKE_CURRENT_LIST_DIR}/render/vga_ftext.S
        ${CMAKE_CURRENT_LIST_DIR}/render/vga_graph1.S
        ${CMAKE_CURRENT_LIST_DIR}/render/vga_graph2.S
        ${CMAKE_CURRENT_LIST_DIR}/render/vga_graph4.S
        ${CMAKE_CURRENT_LIST_DIR}/render/vga_graph8.S
        ${CMAKE_CURRENT_LIST_DIR}/render/vga_graph8mat.S
        ${CMAKE_CURRENT_LIST_DIR}/render/vga_graph8persp.S
        ${CMAKE_CURRENT_LIST_DIR}/render/vga_gtext.S
        ${CMAKE_CURRENT_LIST_DIR}/render/vga_level.S
        ${CMAKE_CURRENT_LIST_DIR}/render/vga_levelgrad.S
        ${CMAKE_CURRENT_LIST_DIR}/render/vga_mtext.S
        ${CMAKE_CURRENT_LIST_DIR}/render/vga_oscil.S
        ${CMAKE_CURRENT_LIST_DIR}/render/vga_oscline.S
        ${CMAKE_CURRENT_LIST_DIR}/render/vga_persp.S
        ${CMAKE_CURRENT_LIST_DIR}/render/vga_persp2.S
        ${CMAKE_CURRENT_LIST_DIR}/render/vga_plane2.S
        ${CMAKE_CURRENT_LIST_DIR}/render/vga_progress.S
        ${CMAKE_CURRENT_LIST_DIR}/render/vga_sprite.S
        ${CMAKE_CURRENT_LIST_DIR}/render/vga_tile.S
        ${CMAKE_CURRENT_LIST_DIR}/render/vga_tile2.S
        ${CMAKE_CURRENT_LIST_DIR}/render/vga_tilepersp.S
        ${CMAKE_CURRENT_LIST_DIR}/render/vga_tilepersp15.S
        ${CMAKE_CURRENT_LIST_DIR}/render/vga_tilepersp2.S
        ${CMAKE_CURRENT_LIST_DIR}/render/vga_tilepersp3.S
        ${CMAKE_CURRENT_LIST_DIR}/render/vga_tilepersp4.S 
        )

target_include_directories(PicoVGA INTERFACE ${CMAKE_CURRENT_LIST_DIR})
