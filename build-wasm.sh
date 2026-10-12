#!/bin/bash
#
# build-wasm.sh — build the WebAssembly (browser) port of Final Frontier.
#
# Steps 1-4 of the port plan: emulator core + shim (canvas display,
# keyboard, mouse/grab). Audio and the WebGL 3D renderer come later.
#
# Prereqs: emcc (emsdk), plus a working native toolchain for as68k.
# Output:  frontier.js + frontier.wasm + frontier.data (+ frontier.html shell)
#
# Run:     ./build-wasm.sh          (incremental: the 68k interpreter
#                                    objects are cached by content hash)
# Then:    python3 -m http.server 8000   and open http://localhost:8000/frontier.html
#
set -e
cd "$(dirname "$0")"

EMCC=${EMCC:-emcc}
BACKEND="wasm"
OBJDIR=wasm/obj/$BACKEND
mkdir -p "$OBJDIR"

BACKEND_FLAGS=""
FE2_PART1_FLAGS="-O1"

# NOTE: no -fcommon here (unlike the native build). wasm-ld has no common
# symbols, so _host.c defines the emulator state in PART1 and declares it
# extern for PART2 (see _host.c / FE2_STATE).
FE2FLAGS="-fomit-frame-pointer -Wall -Wno-unused -fwrapv"
echo "==> 1/4 building the 68k assembler + generating fe2.s.c / fe2.s.bin"
make -C as68k/ >/dev/null

# Regenerating fe2.s.c would force a rebuild of the 68k interpreter
# (~35 min), so only do it when an input actually changed.
regen=0
[ ! -f fe2.s.c ] && regen=1
[ ! -f fe2.s.bin ] && regen=1
[ fe2.s -nt fe2.s.c ] && regen=1
[ as68k/as68k -nt fe2.s.c ] && regen=1
if [ "$regen" = 1 ]; then
	./as68k/as68k --output-cfunc fe2.s    # -> fe2.s.c   (block functions + wasm yield hooks)
	./as68k/as68k fe2.s                   # -> fe2.s.bin (the 68k program, preloaded)
else
	echo "    fe2.s.c / fe2.s.bin up to date"
fi

echo "==> 2/4 compiling the 68k interpreter for $BACKEND"
# Cache by content hash: the objects take far too long to rebuild just
# because a timestamp changed. _host.c is #included by fe2.s.c, so hash it
# too, along with the flags the interpreter is compiled with.
fe2_sum=$( { cat _host.c fe2.s.c; echo "$FE2FLAGS $FE2_PART1_FLAGS"; } | md5sum | cut -d' ' -f1)
if [ ! -f "$OBJDIR/fe2.md5" ] || [ "$(cat "$OBJDIR/fe2.md5")" != "$fe2_sum" ] \
   || [ ! -f "$OBJDIR/fe2.part1.o" ] || [ ! -f "$OBJDIR/fe2.part2.o" ]; then
	# PART1 = block functions + dispatch loop, PART2 = 24701 small instruction functions
	$EMCC -DPART1 $FE2FLAGS $FE2_PART1_FLAGS -c fe2.s.c -o "$OBJDIR/fe2.part1.o"
	$EMCC -DPART2 -O1 $FE2FLAGS -c fe2.s.c -o "$OBJDIR/fe2.part2.o"
	echo "$fe2_sum" > "$OBJDIR/fe2.md5"
else
	echo "    fe2 part objects up to date"
fi

echo "==> 3/4 compiling the host code + shim"
# No -DOGG_MUSIC yet: the vorbis/WebAudio step comes later, and audio.c
# compiles fine without it.
HOSTFLAGS="-O1 -Wall -Wno-unused -Iwasm"
HDRS="wasm/sdl12.h wasm/GL/gl.h wasm/GL/glu.h m68000.h"
for f in src/audio.c src/hostcall.c src/keymap.c src/main.c \
         src/screen.c src/shortcut.c src/input.c wasm/shim.c; do
	o="$OBJDIR/$(basename "${f%.c}").o"
	rebuild=0
	[ ! -f "$o" ] && rebuild=1
	[ "$f" -nt "$o" ] && rebuild=1
	for h in $HDRS; do [ "$h" -nt "$o" ] && rebuild=1; done
	if [ "$rebuild" = 1 ]; then
		$EMCC $HOSTFLAGS -c "$f" -o "$o"
	else
		echo "    $(basename "$o") up to date"
	fi
done

echo "==> 4/4 linking ($BACKEND)"
$EMCC -O1 \
	"$OBJDIR"/*.o \
	$BACKEND_FLAGS \
	-lidbfs.js \
	-sALLOW_MEMORY_GROWTH=1 \
	-sINITIAL_MEMORY=64MB \
	-sSTACK_SIZE=8MB \
	-sEXIT_RUNTIME=0 \
	-sEXPORTED_RUNTIME_METHODS='["UTF8ToString","cwrap","ccall","FS"]' \
	-sEXPORTED_FUNCTIONS='["_Start680x0","_wasm_key_event","_wasm_mouse_motion","_wasm_mouse_button","_wasm_68k_entry","_wasm_68k_yield_check","_fe2_get_vbl_count","_fe2_get_frame_count","_fe2_get_yield_count","_fe2_get_entry_count","_fe2_get_backedge_count","_fe2_get_line_no","_fe2_get_rdest","_fe2_get_done","_fe2_dump_trace","_fe2_get_event_count","_main","_malloc","_free"]' \
	--pre-js wasm/glue.js \
	--preload-file fe2.s.bin \
	--preload-file sfx \
	--preload-file music \
	-o frontier.js

ls -la frontier.js frontier.data $( [ "$BACKEND" = wasm ] && echo frontier.wasm )
echo
echo "Done ($BACKEND build). Serve this directory and open frontier.html, e.g.:"
echo "    python3 -m http.server 8000    # then http://localhost:8000/frontier.html"
echo "Headless check:  node wasm/headless-test.js"
