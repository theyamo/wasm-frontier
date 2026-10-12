/*
 * wasm/glue.js — browser glue for the wasm port (loaded via emcc --pre-js).
 *
 * Owns everything that has to be JavaScript:
 *   - the <canvas> display (320x200, pixelated, scaled to fit the window)
 *   - DOM key/mouse listeners -> exported C event push functions
 *   - pointer lock (the game's right-button mouse grab)
 *   - re-entering the emulator loop (Start680x0) on requestAnimationFrame.
 *
 * The C side (wasm/shim.c) calls into Module.__fe2.*; the only names it
 * relies on are those defined at the bottom of this file.
 */
(function () {
  'use strict';

  var win = (typeof window !== 'undefined') ? window
          : (typeof globalThis !== 'undefined') ? globalThis
          : this;
  var doc = (typeof document !== 'undefined') ? document : null;

  /* --- DOM KeyEvent.code -> [SDL 1.2 keysym, PC set-1 scancode] ---------
   * scancodes matter: the game's Keymap_PcToStScanCode() learns its
   * "offset" from the first recognised key and then passes the scancode
   * straight through as an ST scan code, so set-1 values key in as
   * offset 0. Keysyms (sym) are the real SDL 1.2 values — keymap.c's
   * tables are indexed by them (SDLK_LAST is 323).
   */
  var KEY = {
    KeyA: [97, 0x1e], KeyB: [98, 0x30], KeyC: [99, 0x2e], KeyD: [100, 0x20],
    KeyE: [101, 0x12], KeyF: [102, 0x21], KeyG: [103, 0x22], KeyH: [104, 0x23],
    KeyI: [105, 0x17], KeyJ: [106, 0x24], KeyK: [107, 0x25], KeyL: [108, 0x26],
    KeyM: [109, 0x32], KeyN: [110, 0x31], KeyO: [111, 0x18], KeyP: [112, 0x19],
    KeyQ: [113, 0x10], KeyR: [114, 0x13], KeyS: [115, 0x1f], KeyT: [116, 0x14],
    KeyU: [117, 0x16], KeyV: [118, 0x2f], KeyW: [119, 0x11], KeyX: [120, 0x2d],
    KeyY: [121, 0x15], KeyZ: [122, 0x2c],

    Digit0: [48, 0x0b], Digit1: [49, 0x02], Digit2: [50, 0x03], Digit3: [51, 0x04],
    Digit4: [52, 0x05], Digit5: [53, 0x06], Digit6: [54, 0x07], Digit7: [55, 0x08],
    Digit8: [56, 0x09], Digit9: [57, 0x0a],

    Enter: [13, 0x1c], Space: [32, 0x39], Backspace: [8, 0x0e],
    Escape: [27, 0x01], Tab: [9, 0x0f],

    Semicolon: [59, 0x29], Quote: [39, 0x28], Comma: [44, 0x33], Minus: [45, 0x2c],
    Period: [46, 0x34], Slash: [47, 0x2b], Backslash: [92, 0x2b], Equal: [61, 0x2d],
    BracketLeft: [91, 0x1b], BracketRight: [93, 0x27], Backquote: [96, 0x2f],

    ShiftLeft: [304, 0x2a], ShiftRight: [303, 0x36],
    ControlLeft: [306, 0x1d], ControlRight: [305, 0x1d],
    AltLeft: [308, 0x38], AltRight: [307, 0x38],
    CapsLock: [301, 0x3a], NumLock: [300, 0x45],

    ArrowUp: [273, 0x48], ArrowDown: [274, 0x50],
    ArrowLeft: [276, 0x4b], ArrowRight: [275, 0x4d],

    F1: [282, 0x3b], F2: [283, 0x3c], F3: [284, 0x3d], F4: [285, 0x3e],
    F5: [286, 0x3f], F6: [287, 0x40], F7: [288, 0x41], F8: [289, 0x42],
    F9: [290, 0x43], F10: [291, 0x44], F11: [292, 0x57], F12: [293, 0x58],

    Numpad0: [256, 0x52], Numpad1: [257, 0x4f], Numpad2: [258, 0x50],
    Numpad3: [259, 0x51], Numpad4: [260, 0x4c], Numpad5: [261, 0x4d],
    Numpad6: [262, 0x4e], Numpad7: [263, 0x47], Numpad8: [264, 0x48],
    Numpad9: [265, 0x49], NumpadDecimal: [266, 0x53], NumpadDivide: [267, 0x35],
    NumpadMultiply: [268, 0x37], NumpadSubtract: [269, 0x4a], NumpadAdd: [270, 0x4e],
    NumpadEnter: [271, 0x9c], NumpadEqual: [272, 0x5d]
  };

  /* SDL 1.2 KMOD_* bits */
  function mods (e) {
    var m = 0;
    if (e.getModifierState) {
      if (e.getModifierState ('Shift')) m |= 0x0001;
      if (e.getModifierState ('Control')) m |= 0x0002;
      if (e.getModifierState ('Alt')) m |= 0x0004;
      if (e.getModifierState ('NumLock')) m |= 0x0010;
      if (e.getModifierState ('CapsLock')) m |= 0x0020;
    }
    if (e.shiftKey) m |= (e.location === 0 ? 0x0100 : 0x0200); /* LSHIFT/RSHIFT */
    if (e.ctrlKey) m |= (e.location === 0 ? 0x0400 : 0x0800);  /* LCTRL/RCTRL  */
    if (e.altKey) m |= (e.location === 0 ? 0x1000 : 0x2000);   /* LALT/RALT    */
    return m;
  }

  /* Emitted C functions are visible as Module._name (the C symbol);
   * accept either spelling so this keeps working across emscripten
   * versions/configurations. */
  function cfn (name) {
    return (typeof Module['_' + name] === 'function') ? Module['_' + name]
         : (typeof Module[name] === 'function') ? Module[name]
         : null;
  }

  var fe2 = {
    dead: false,
    setup: false,
    w: 640, h: 480,          /* the game's "window" size in game pixels */
    canvas: null, ctx: null, imgdata: null,
    locked: false,
    vx: 0, vy: 0,            /* virtual cursor (window units) while locked */
    lastX: 0, lastY: 0, haveLast: false,

    /* Scale the canvas to fill the browser window, keeping the game's
     * window aspect (the desktop build stretches 320x200 to screen_w x
     * screen_h the same way). */
    fit: function () {
      var c = fe2.canvas;
      if (!c || !c.style) return;
      var vw = win.innerWidth || fe2.w;
      var vh = win.innerHeight || fe2.h;
      var scale = Math.min (vw / fe2.w, vh / fe2.h);
      var cw = Math.max (320, Math.floor (fe2.w * scale));
      var ch = Math.max (200, Math.floor (cw * fe2.h / fe2.w));
      c.style.width = cw + 'px';
      c.style.height = ch + 'px';
    },

    /* window-pixel coords of a mouse event */
    toWindow: function (e) {
      var r = fe2.canvas.getBoundingClientRect ();
      var x = (e.clientX - r.left) * fe2.w / (r.width || fe2.w);
      var y = (e.clientY - r.top) * fe2.h / (r.height || fe2.h);
      return [Math.max (0, Math.min (fe2.w - 1, x | 0)),
              Math.max (0, Math.min (fe2.h - 1, y | 0))];
    },

    press: function (e, down) {
      var b = (e.button === 0) ? 1 : (e.button === 1) ? 2 : (e.button === 2) ? 3 : 0;
      if (!b) return;
      var p = fe2.toWindow (e);
      fe2.lastX = p[0]; fe2.lastY = p[1]; fe2.haveLast = true;
      var fn = cfn ('wasm_mouse_button');
      if (fn) fn (down ? 1 : 0, b, p[0], p[1]);
    },

    setup_video: function (w, h) {
      fe2.w = w || 640;
      fe2.h = h || 480;
      if (fe2.setup) { fe2.fit (); return; }
      fe2.setup = true;

      var canvas = null;
      if (doc) {
        canvas = doc.getElementById ? doc.getElementById ('canvas') : null;
        if (!canvas && doc.createElement) {
          canvas = doc.createElement ('canvas');
          canvas.id = 'canvas';
          var host = doc.body || doc.documentElement;
          if (host && host.appendChild) host.appendChild (canvas);
        }
      }
      if (!canvas) canvas = {};                 /* headless: no DOM at all */

      fe2.canvas = canvas;
      canvas.width = 320;
      canvas.height = 200;
      if (canvas.style) canvas.style.imageRendering = 'pixelated';

      if (canvas.getContext) {
        fe2.ctx = canvas.getContext ('2d');
        if (fe2.ctx && fe2.ctx.createImageData)
          fe2.imgdata = fe2.ctx.createImageData (320, 200);
      }
      fe2.fit ();

      if (win.addEventListener) win.addEventListener ('resize', fe2.fit, false);

      /* --- keyboard --- */
      function keyHandler (down) {
        return function (e) {
          var k = e.code ? KEY[e.code] : null;
          if (!k) return;                        /* unmapped: drop it */
          if (e.preventDefault) e.preventDefault ();
          var fn = cfn ('wasm_key_event');
          if (fn) fn (down, k[0], k[1], mods (e));
        };
      }
      if (win.addEventListener) {
        win.addEventListener ('keydown', keyHandler (1), false);
        win.addEventListener ('keyup', keyHandler (0), false);
      }

      /* --- mouse --- */
      if (canvas.addEventListener) {
        canvas.addEventListener ('mousemove', function (e) {
          var x, y, xr, yr;
          if (fe2.locked) {
            /* pointer locked: build the absolute position from deltas */
            fe2.vx = Math.max (0, Math.min (fe2.w - 1, fe2.vx + (e.movementX | 0)));
            fe2.vy = Math.max (0, Math.min (fe2.h - 1, fe2.vy + (e.movementY | 0)));
            x = fe2.vx; y = fe2.vy;
            xr = e.movementX | 0; yr = e.movementY | 0;
          } else {
            var p = fe2.toWindow (e);
            x = p[0]; y = p[1];
            xr = fe2.haveLast ? (x - fe2.lastX) : 0;
            yr = fe2.haveLast ? (y - fe2.lastY) : 0;
          }
          fe2.lastX = x; fe2.lastY = y; fe2.haveLast = true;
          var fn = cfn ('wasm_mouse_motion');
          if (fn) fn (x, y, xr, yr);
        }, false);
        canvas.addEventListener ('mousedown', function (e) {
          if (e.preventDefault) e.preventDefault ();
          fe2.press (e, 1);
        }, false);
        canvas.addEventListener ('mouseup', function (e) {
          if (e.preventDefault) e.preventDefault ();
          fe2.press (e, 0);
        }, false);
        /* right-hold is the game's mouse grab: no context menu, please */
        canvas.addEventListener ('contextmenu', function (e) {
          if (e.preventDefault) e.preventDefault ();
        }, false);
      }

      /* --- pointer lock (SDL_WM_GrabInput) --- */
      if (doc && doc.addEventListener) {
        doc.addEventListener ('pointerlockchange', function () {
          var on = (doc.pointerLockElement === fe2.canvas);
          if (on && !fe2.locked) {   /* start from where the cursor is */
            fe2.vx = fe2.lastX; fe2.vy = fe2.lastY;
          }
          fe2.locked = on;
        }, false);
      }
    },

    /* one frame of the 320x200 RGBA framebuffer */
    present: function (ptr) {
      var idata = fe2.imgdata, ctx = fe2.ctx;
      if (!idata || !ctx || !Module.HEAPU8) return;
      idata.data.set (Module.HEAPU8.subarray (ptr, ptr + 320 * 200 * 4));
      ctx.putImageData (idata, 0, 0);
    },

    /* The emulator loop. main() calls Start680x0() once; that call returns
     * to the browser (it yields when the chunk budget or idle-loop logic
     * says so) and from then on the loop lives here: each tick runs one
     * chunk of 68k and the chunk itself decides when to hand control back
     * (wasm/shim.c). This also covers the case where the first call
     * returns for some other reason. */
    start_loop: function () {
      if (fe2.looping) return;
      fe2.looping = true;
      var raf = (typeof requestAnimationFrame === 'function') ? requestAnimationFrame
              : function (f) { return setTimeout (f, 16); };
      var tick = function () {
        if (fe2.dead) return;
        var step = cfn ('Start680x0');
        if (!step) { fe2.no_entry_fn = true; return; }
        fe2.entries = (fe2.entries || 0) + 1;
        step ();
        raf (tick);
      };
      raf (tick);
    },

    /* called by the C yield path; the rAF loop above is already running */
    resume: function () {
      fe2.resume_calls = (fe2.resume_calls || 0) + 1;
    },

    grab: function (on) {
      var c = fe2.canvas;
      if (!c) return;
      if (on) {
        if (c.requestPointerLock) {
          try {
            var p = c.requestPointerLock ();
            if (p && p.catch) p.catch (function () {});
          } catch (e) {}
        }
      } else if (doc && doc.exitPointerLock) {
        try { doc.exitPointerLock (); } catch (e) {}
      }
    },

    warp: function (x, y) {
      /* browsers can't move the real cursor; only our virtual one */
      if (fe2.locked) { fe2.vx = x; fe2.vy = y; }
    },

    show_cursor: function (on) {
      if (fe2.canvas && fe2.canvas.style)
        fe2.canvas.style.cursor = on ? 'default' : 'none';
    },

    set_caption: function (s) {
      if (doc) doc.title = s;
    },

    /* --- SFX (WebAudio) ----------------------------------------------
     * The game's Call_PlaySFX hostcall reaches wasm_play_sfx() (shim.c),
     * which calls play_sfx() here. We decode the 33 preloaded WAVs into
     * AudioBuffers and play one source per channel; WebAudio does the
     * mixing and sample-rate conversion off the main thread. */
    audio: null,
    sfx: [],
    sfx_nodes: {},

    init_audio: function () {
      var AC = win.AudioContext || win.webkitAudioContext;
      if (AC && !fe2.audio) {
        try { fe2.audio = new AC (); } catch (e) { fe2.audio = null; }
      }
      if (!fe2.audio || !Module.FS) return;      /* headless: no audio */
      for (var i = 0; i < 33; i++) {
        (function (i) {
          var name = '/sfx/sfx_' + (i < 10 ? '0' : '') + i + '.wav';
          var bytes;
          try { bytes = Module.FS.readFile (name); } catch (e) { return; }
          fe2.audio.decodeAudioData (bytes.buffer.slice (0),
            function (buf) { fe2.sfx[i] = buf; },
            function () { /* undecodable: leave silent */ });
        }) (i);
      }
    },

    play_sfx: function (sample, channel) {
      if (!fe2.audio || !fe2.sfx[sample]) return;
      if (fe2.audio.state === 'suspended') fe2.audio.resume ();
      var old = fe2.sfx_nodes[channel];
      if (old) { try { old.stop (); } catch (e) {} }
      var src = fe2.audio.createBufferSource ();
      src.buffer = fe2.sfx[sample];
      /* samples 19 (hyperspace) and 23 (noise) loop, as in audio.c */
      if (sample === 19) { src.loop = true; src.loopStart = 0.5; }
      else if (sample === 23) { src.loop = true; src.loopStart = 0; }
      src.connect (fe2.audio.destination);
      src.start (0);
      fe2.sfx_nodes[channel] = src;
    }
  };

  Module.__fe2 = fe2;

  /* Browsers keep an AudioContext suspended until a user gesture; resume
   * it on the first click/key so the SFX start working. */
  function resume_audio () {
    if (fe2.audio && fe2.audio.state === 'suspended') fe2.audio.resume ();
  }
  if (win.addEventListener) {
    win.addEventListener ('pointerdown', resume_audio);
    win.addEventListener ('keydown', resume_audio);
  }

  /* --- Save games: persist the guest's ./savs directory --------------
   * The game saves/loads with plain fopen/opendir on "./savs" (cwd is
   * "/"), so mount an emscripten IDBFS there: those files then live in
   * IndexedDB and survive a reload, while the guest code is unchanged.
   * FS.syncfs() flushes to / hydrates from IndexedDB. (Node/headless has
   * no indexedDB, so fall back to an in-memory /savs.) */
  Module.preRun = Module.preRun || [];
  Module.preRun.push (function () {
    try { FS.mkdir ('/savs'); } catch (e) {}
    if (typeof indexedDB !== 'undefined') {
      try { FS.mount (IDBFS, {}, '/savs'); } catch (e) {}
    }
  });

  /* Called from C (wasm_fs_sync) after a save file is written or deleted. */
  fe2.save_sync = function () {
    if (Module.FS && Module.FS.syncfs) Module.FS.syncfs (false, function () {});
  };

  /* Hydrate saved games from IndexedDB, then start the emulator. */
  Module.postRun = Module.postRun || [];
  Module.postRun.push (function () {
    var start = function () { fe2.init_audio (); fe2.start_loop (); };
    if (Module.FS && Module.FS.syncfs) Module.FS.syncfs (true, start);
    else start ();
  });
})();
