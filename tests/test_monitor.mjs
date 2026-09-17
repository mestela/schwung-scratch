import "../src/monitor.js";

const calls = [];
const ctx = {
    width: 128,
    height: 42,
    fillRect(...args) { calls.push(["fillRect", ...args]); },
    setPixel(...args) { calls.push(["setPixel", ...args]); },
    print(...args) { calls.push(["print", ...args]); },
};

globalThis.canvas_overlay.drawPage(ctx, {
    values: {
        dvs_status: "0.5,0.4,-1.02,12.345,0.92,1,0.7,abcde,14,127,1,1,60,2,0123456789abcdef0123456789abcdef,1,2646000,3.25,1",
    },
});

if (!calls.some((call) => call[0] === "print" && call[3] === "LOCK"))
    throw new Error("monitor did not render decoder lock state");
if (!calls.some((call) => call[0] === "print" && String(call[3]).includes("REV")))
    throw new Error("monitor did not render reverse direction");
if (!calls.some((call) => call[0] === "setPixel"))
    throw new Error("monitor did not render phase scope");

console.log(`monitor: rendered ${calls.length} draw calls`);
