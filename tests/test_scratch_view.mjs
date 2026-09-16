import "../src/scratch_view.js";

const calls = [];
const ctx = {
    width: 128,
    height: 64,
    fillRect(...args) { calls.push(["fillRect", ...args]); },
    print(...args) { calls.push(["print", ...args]); },
};

globalThis.canvas_overlay.drawPage(ctx, {
    values: {
        scratch_view_status: `12.345,-1.02,1,${"123456789abcdef".repeat(9).slice(0, 128)}`,
    },
});

if (!calls.some((call) => call[0] === "fillRect" && call[1] === 64 && call[3] === 1))
    throw new Error("scratch view did not render the fixed centre playhead");
if (!calls.some((call) => call[0] === "print" && String(call[3]).includes("REV")))
    throw new Error("scratch view did not render reverse direction");
if (calls.filter((call) => call[0] === "fillRect").length < 100)
    throw new Error("scratch view did not render the waveform envelope");

console.log(`scratch view: rendered ${calls.length} draw calls`);
