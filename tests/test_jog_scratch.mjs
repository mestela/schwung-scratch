import assert from "node:assert/strict";
import fs from "node:fs";
import vm from "node:vm";

const source = fs.readFileSync(new URL("../src/scratch_view.js", import.meta.url), "utf8");
vm.runInThisContext(source);

const writes = [];
let now = 1000;
let controlMode = 2;
const ctx = {
    width: 128,
    height: 64,
    state: {},
    getParam(key) {
        if (key === "scratch_view_status") return `3.000,0,1,1,64,${"123456789abcdef".repeat(9).slice(0, 128)}`;
        if (key === "jog_sensitivity") return "0.5";
        if (key === "knob_sensitivity") return "0.18";
        if (key === "knob_smoothing_ms") return "25";
        if (key === "touch_inertia_ms") return "45";
        if (key === "jog_touch") return "0";
        if (key === "virtual_play") return "1";
        if (key === "virtual_speed") return "1";
        if (key === "control_mode") return String(controlMode);
        return "";
    },
    setParam(key, value) { writes.push([key, value]); },
    setParamImmediate(key, value) { writes.push([key, value]); return true; },
    now() { return now; },
    clear() {}, fillRect() {}, setPixel() {}, print() {},
};

const overlay = globalThis.canvas_overlay;
overlay.onOpen(ctx);
assert.deepEqual(writes.slice(0, 2), [["jog_active", "1"], ["jog_gate", "0"]]);

overlay.onMidi(ctx, { data: [0xb0, 14, 3] });
overlay.onMidi(ctx, { data: [0xb0, 14, 126] });
assert.deepEqual(writes.slice(2, 4), [["jog_rate", "1.5"], ["jog_rate", "-6.25"]]);

const jogTuneStart = writes.length;
overlay.onMidi(ctx, { data: [0xb0, 72, 1] });
overlay.onMidi(ctx, { data: [0xb0, 73, 1] });
overlay.onMidi(ctx, { data: [0xb0, 74, 127] });
assert.deepEqual(writes.slice(jogTuneStart), [
    ["jog_sensitivity", "0.55"],
    ["knob_smoothing_ms", "30"],
    ["touch_inertia_ms", "40"],
]);
const motorStart = writes.length;
overlay.onMidi(ctx, { data: [0xb0, 78, 127] });
overlay.onMidi(ctx, { data: [0xb0, 78, 1] });
assert.deepEqual(writes.slice(motorStart), [
    ["virtual_play", "0"], ["virtual_play", "1"],
]);
const jogTouchStart = writes.length;
overlay.onMidi(ctx, { data: [0xb0, 75, 1] });
overlay.onMidi(ctx, { data: [0xb0, 76, 1] });
for (let i = 0; i < 8; ++i) overlay.onMidi(ctx, { data: [0xb0, 77, 1] });
overlay.onMidi(ctx, { data: [0x90, 9, 127] });
assert.equal(ctx.state.platterTouched, true);
now += 130;
overlay.tick(ctx);
assert.notEqual(ctx.state.targetSpeed, 1.05);
overlay.onMidi(ctx, { data: [0x80, 9, 0] });
assert.equal(ctx.state.platterTouched, false);
assert.deepEqual(writes.slice(jogTouchStart), [
    ["jog_touch", "1"], ["virtual_speed", "1.05"], ["wave_zoom", "2"],
    ["knob_touch", "1"], ["knob_touch", "0"],
]);

overlay.onMidi(ctx, { data: [0x90, 36, 100] });
overlay.onMidi(ctx, { data: [0x90, 37, 100] });
overlay.onMidi(ctx, { data: [0x80, 36, 0] });
overlay.onMidi(ctx, { data: [0x80, 37, 0] });
assert.deepEqual(writes.slice(jogTouchStart + 5, jogTouchStart + 9), [
    ["jog_gate", "2"], ["jog_gate", "2"],
    ["jog_gate", "1"], ["jog_gate", "0"],
]);

overlay.draw(ctx);
now += 130;
overlay.tick(ctx);
assert.equal(ctx.state.speed, 1.05);
overlay.onClose(ctx);
assert.deepEqual(writes.slice(-2), [["jog_gate", "0"], ["jog_active", "0"]]);

controlMode = 1;
ctx.state = {};
overlay.onOpen(ctx);
const knobStart = writes.length;
overlay.onMidi(ctx, { data: [0xb0, 75, 1] });
overlay.onMidi(ctx, { data: [0x90, 0, 127] });
overlay.onMidi(ctx, { data: [0xb0, 71, 2] });
overlay.onMidi(ctx, { data: [0x80, 0, 0] });
assert.deepEqual(writes.slice(knobStart), [
    ["jog_touch", "1"],
    ["knob_touch", "1"], ["jog_rate", "0.70875"], ["knob_touch", "0"],
]);

const tuneStart = writes.length;
overlay.onMidi(ctx, { data: [0xb0, 72, 1] });
overlay.onMidi(ctx, { data: [0xb0, 73, 1] });
overlay.onMidi(ctx, { data: [0xb0, 74, 127] });
assert.deepEqual(writes.slice(tuneStart), [
    ["knob_sensitivity", "0.20"],
    ["knob_smoothing_ms", "30"],
    ["touch_inertia_ms", "40"],
]);
const syncedEnvelope = "a".repeat(128);
overlay.onValues(ctx, { values: {
    scratch_view_status: `8.250,0.625,1,1,64,${syncedEnvelope}`,
} });
assert.equal(ctx.state.position, 8.25);
assert.equal(ctx.state.speed, 0.625);
assert.equal(ctx.state.envelope, syncedEnvelope);

controlMode = 0;
ctx.state = {};
overlay.onOpen(ctx);
const dvsControlStart = writes.length;
overlay.onMidi(ctx, { data: [0xb0, 76, 1] });
for (let i = 0; i < 8; ++i) overlay.onMidi(ctx, { data: [0xb0, 77, 1] });
assert.deepEqual(writes.slice(dvsControlStart), [
    ["virtual_speed", "1.05"], ["wave_zoom", "2"],
]);
const liveEnvelope = "f".repeat(128);
overlay.onValues(ctx, { values: {
    scratch_view_status: `12.500,-0.75,1,0,64,${liveEnvelope}`,
} });
assert.equal(ctx.state.position, 12.5);
assert.equal(ctx.state.speed, -0.75);
assert.equal(ctx.state.envelope, liveEnvelope);
console.log("scratch performance view: all tests passed");
