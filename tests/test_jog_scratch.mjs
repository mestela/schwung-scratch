import assert from "node:assert/strict";
import fs from "node:fs";
import vm from "node:vm";

const source = fs.readFileSync(new URL("../src/jog_scratch.js", import.meta.url), "utf8");
vm.runInThisContext(source);

const writes = [];
let now = 1000;
const ctx = {
    width: 128,
    height: 64,
    state: {},
    getParam(key) {
        if (key === "scratch_view_status") return `3.000,0,1,1,64,${"123456789abcdef".repeat(9).slice(0, 128)}`;
        if (key === "jog_sensitivity") return "0.5";
        if (key === "virtual_play") return "1";
        if (key === "virtual_speed") return "1";
        if (key === "control_mode") return "2";
        return "";
    },
    setParam(key, value) { writes.push([key, value]); },
    now() { return now; },
    clear() {}, fillRect() {}, print() {},
};

const overlay = globalThis.canvas_overlay;
overlay.onOpen(ctx);
assert.deepEqual(writes.slice(0, 2), [["jog_active", "1"], ["jog_gate", "0"]]);

overlay.onMidi(ctx, { data: [0xb0, 14, 3] });
overlay.onMidi(ctx, { data: [0xb0, 14, 126] });
assert.deepEqual(writes.slice(2, 4), [["jog_delta", "3"], ["jog_delta", "-2"]]);

overlay.onMidi(ctx, { data: [0x90, 68, 100] });
overlay.onMidi(ctx, { data: [0x90, 69, 100] });
overlay.onMidi(ctx, { data: [0x80, 68, 0] });
overlay.onMidi(ctx, { data: [0x80, 69, 0] });
assert.deepEqual(writes.slice(4, 8), [
    ["jog_gate", "2"], ["jog_gate", "2"],
    ["jog_gate", "1"], ["jog_gate", "0"],
]);

overlay.draw(ctx);
now += 130;
overlay.tick(ctx);
assert.equal(ctx.state.speed, 1);
overlay.onClose(ctx);
assert.deepEqual(writes.slice(-2), [["jog_gate", "0"], ["jog_active", "0"]]);
console.log("jog_scratch: all tests passed");
