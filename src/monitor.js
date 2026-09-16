function clamp(v, lo, hi) {
    return Math.max(lo, Math.min(hi, v));
}

function parseStatus(raw) {
    const p = String(raw || "").split(",");
    return {
        left: Number(p[0]) || 0,
        right: Number(p[1]) || 0,
        pitch: Number(p[2]) || 0,
        position: Number(p[3]) || 0,
        quality: Number(p[4]) || 0,
        locked: Number(p[5]) === 1,
        age: Number(p[6]) || 0,
        word: p[7] || "00000",
        cc: Number(p[8]),
        ccValue: Number(p[9]),
        padGate: Number(p[10]) === 1,
        padHeld: Number(p[11]) === 1,
        note: Number(p[12]),
        noteSource: Number(p[13]),
        scope: p[14] || "",
    };
}

function rect(ctx, x, y, w, h) {
    ctx.fillRect(x, y, w, 1, 1);
    ctx.fillRect(x, y + h - 1, w, 1, 1);
    ctx.fillRect(x, y, 1, h, 1);
    ctx.fillRect(x + w - 1, y, 1, h, 1);
}

function amplitudeDb(value) {
    if (!(value > 0)) return -99;
    return Math.max(-99, 20 * Math.log10(value));
}

function meter(ctx, x, y, w, value) {
    rect(ctx, x, y, w, 5);
    // Show the useful -80..0 dBFS range. A linear meter rendered phono-level
    // signals as zero pixels even though the decoder could just detect them.
    const fill = Math.round((w - 2) * clamp((amplitudeDb(value) + 80) / 80, 0, 1));
    if (fill > 0) ctx.fillRect(x + 1, y + 1, fill, 3, 1);
}

function plotScope(ctx, encoded) {
    const x0 = 76, y0 = 1, size = 30;
    rect(ctx, x0, y0, size + 2, size + 2);
    ctx.setPixel(x0 + 16, y0 + 16, 1);
    for (let i = 0; i + 1 < encoded.length; i += 2) {
        const x = parseInt(encoded[i], 16);
        const y = parseInt(encoded[i + 1], 16);
        if (Number.isFinite(x) && Number.isFinite(y)) {
            ctx.setPixel(x0 + 1 + Math.round(x * 29 / 15),
                         y0 + 1 + Math.round((15 - y) * 29 / 15), 1);
        }
    }
}

globalThis.canvas_overlay = {
    drawPage(ctx, { values }) {
        const s = parseStatus(values.dvs_status);
        const direction = Math.abs(s.pitch) < 0.02 ? "STOP" : (s.pitch < 0 ? "REV" : "FWD");
        const q = Math.round(clamp(s.quality, 0, 1) * 100);

        meter(ctx, 9, 1, 60, s.left);
        meter(ctx, 9, 8, 60, s.right);
        ctx.print(1, 1, "L", 1);
        ctx.print(1, 8, "R", 1);
        ctx.print(72, 1, `${Math.round(amplitudeDb(s.left))}`, 1);
        ctx.print(72, 8, `${Math.round(amplitudeDb(s.right))}`, 1);
        ctx.print(1, 16, s.locked ? "LOCK" : "SEEK", 1);
        ctx.print(30, 16, `Q${q}`, 1);
        ctx.print(1, 24, `${direction} ${s.pitch.toFixed(2)}x`, 1);
        ctx.print(1, 32, `${s.position.toFixed(2)}s ${s.word}`, 1);
        if (Number.isFinite(s.cc) && Number.isFinite(s.ccValue))
            ctx.print(76, 34, `CC${s.cc}:${s.ccValue}`, 1);
        if (s.padGate)
            ctx.print(76, 42, s.padHeld ? "PAD DOWN" : "PAD UP", 1);
        if (Number.isFinite(s.note))
            ctx.print(76, 50, `N${s.note} S${s.noteSource}`, 1);
        plotScope(ctx, s.scope);
    },
};
