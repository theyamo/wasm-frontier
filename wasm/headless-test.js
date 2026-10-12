#!/usr/bin/env node
/*
 * wasm/headless-test.js — smoke test for the wasm build (run after
 * ./build-wasm.sh):
 *
 *     node wasm/headless-test.js
 *
 * Stubs just enough DOM (a 2D-canvas element, window/document, ImageData)
 * for wasm/shim.c to boot, requires the emscripten output, lets the
 * emulator run for a few seconds and then checks that VBL ticks and
 * presented frames are actually happening. The shim's requestAnimationFrame
 * fallback (setTimeout) is what drives the loop under Node.
 *
 * Exit code 0 = PASS, 1 = FAIL.
 */
'use strict';

const path = require ('path');
const root = path.join (__dirname, '..');

let presents = 0;
const listeners = {};

function makeContext (canvas) {
	return {
		canvas: canvas,
		putImageData: function () { presents++; },
		createImageData: function (w, h) {
			return { width: w, height: h, data: new Uint8ClampedArray (w * h * 4) };
		}
	};
}

const canvas = {
	id: 'canvas',
	width: 320,
	height: 200,
	style: {},
	addEventListener: function (ev, fn) {
		(listeners[ev] = listeners[ev] || []).push (fn);
	},
	removeEventListener: function () {},
	getBoundingClientRect: function () {
		return { left: 0, top: 0, width: 640, height: 480 };
	},
	getContext: function (type) { return type === '2d' ? makeContext (this) : null; },
	requestPointerLock: function () {},
	focus: function () {}
};

global.window = global;
global.self = global;
// global.navigator = { userAgent: 'node-headless-test' };
/* emscripten's preloaded-file code reads location.pathname */
global.location = { href: 'node:headless', pathname: '/' };
/* capture the listeners wasm/glue.js installs so we can fire fake DOM
 * events through the real input path */
const winListeners = {};
global.addEventListener = function (ev, fn) {
	(winListeners[ev] = winListeners[ev] || []).push (fn);
};
global.removeEventListener = function () {};
global.document = {
	title: 'Frontier',
	body: { appendChild: function () {} },
	documentElement: { appendChild: function () {} },
	getElementById: function (id) { return id === 'canvas' ? canvas : null; },
	createElement: function () { return canvas; },
	addEventListener: function (ev, fn) { (listeners[ev] = listeners[ev] || []).push (fn); },
	removeEventListener: function () {},
	pointerLockElement: null,
	exitPointerLock: function () {}
};
global.ImageData = function (w, h) {
	this.width = w;
	this.height = h;
	this.data = new Uint8ClampedArray (w * h * 4);
};
global.AudioContext = undefined;        /* force the "no audio" path */

process.on ('uncaughtException', function (err) {
	console.error ('FAIL: uncaught exception:');
	console.error (err && err.stack ? err.stack : String (err));
	process.exit (1);
});

console.log ('loading frontier.js ...');
/* the preloaded package (frontier.data) is opened relative to the cwd */
process.chdir (root);
const jsFile = process.env.FRONTIER_JS || 'frontier.js';
const Module = require (path.join (root, jsFile));
const started = Date.now ();

function exported (name) {
	const fn = Module['_' + name] || Module[name];
	return (typeof fn === 'function') ? fn () : -1;
}

/* fire a synthetic DOM key event through the listeners glue.js installed */
function fireKey (type, code) {
	const ev = {
		code: code,
		preventDefault: function () {},
		getModifierState: function () { return false; },
		shiftKey: false,
		ctrlKey: false,
		altKey: false,
		location: 0
	};
	const fns = winListeners[type] || [];
	fns.forEach (function (f) { f (ev); });
	return fns.length;
}

let keyResult = 'not fired';

setTimeout (function () {
	const before = exported ('fe2_get_event_count');
	const listeners = fireKey ('keydown', 'Enter') + fireKey ('keyup', 'Enter');
	if (!listeners) {
		keyResult = 'FAIL: glue.js installed no key listeners';
		return;
	}
	const after = exported ('fe2_get_event_count');
	if (after - before >= 2) {
		keyResult = 'ok (' + (after - before) + ' events queued via DOM glue)';
	} else {
		keyResult = 'FAIL: key events did not reach the C queue (' + before + ' -> ' + after + ')';
	}
}, 2500);

setTimeout (function () {
	const secs = (Date.now () - started) / 1000;
	const vbl = exported ('fe2_get_vbl_count');
	const frames = exported ('fe2_get_frame_count');
	const events = exported ('fe2_get_event_count');

	console.log ('after ' + secs.toFixed (1) + 's: vbls=' + vbl +
	             ' frames=' + frames + ' events=' + events +
	             ' canvas presents=' + presents);
	console.log ('diag: entries=' + exported ('fe2_get_entry_count') +
	             ' back-edges=' + exported ('fe2_get_backedge_count') +
	             ' yields=' + exported ('fe2_get_yield_count') +
	             ' 68k-line=' + exported ('fe2_get_line_no') +
	             ' rdest=0x' + (exported ('fe2_get_rdest') >>> 0).toString (16) +
	             ' done=' + exported ('fe2_get_done') +
	             ' glue.resume=' + (Module.__fe2 ? Module.__fe2.resume_calls : 'n/a') +
	             ' glue.entries=' + (Module.__fe2 ? Module.__fe2.entries : 'n/a'));
	console.log ('keyboard path: ' + keyResult);

	if (vbl > 5 && frames > 5 && presents > 5 && keyResult.indexOf ('ok') === 0) {
		console.log ('PASS: emulator runs, presents frames, and input reaches the core');
		process.exit (0);
	}
	console.log ('FAIL: see the checks above');
	process.exit (1);
}, 4000);
