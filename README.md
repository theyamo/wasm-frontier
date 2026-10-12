wasm-frontier
=============

WASM port of final-frontier, which is based on GLFrontier (Frontier: Elite II's OpenGL port).

NOTE: LLMs were used exclusively for this port!

The code generator was altered to emit one C function per basic block plus a dispatch loop, so the control flow is reducible. (The old single-switch output is both unloadable in a browser and unhandleable by LLVM's wasm CFG pass.)

There's also a bugfix: the original probably couldn't be built with anything but -O0 because some of the code relied on signed 32-bit wraparound, which with -O1 might be folded away. Uses -fwrapv to make the wraparound explicit.

Some features of final-frontier / GLFrontier are not available, e.g. actual GL rendering - I prefer
the original software rendered, low-poly, low-color version myself. Music is also not supported, though 
it would be minor work to add it.

Game save/load should be somewhat working, saves are persisted in the browser's indexed db.

