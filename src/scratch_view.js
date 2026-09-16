function parseView(raw) {
    const p = String(raw || "").split(",");
    return {
        position: Number(p[0]) || 0,
        pitch: Number(p[1]) || 0,
        locked: Number(p[2]) === 1,
        envelope: p[3] || "",
    };
}

globalThis.canvas_overlay = {
    drawPage(ctx, { values }) {
        const s = parseView(values.scratch_view_status);
        const width = Math.min(128, ctx.width || 128);
        const height = ctx.height || 42;
        const middle = Math.floor(height / 2);

        const columns = Math.min(128, s.envelope.length);
        const columnWidth = width / 128;
        for (let column = 0; column < columns; ++column) {
            const level = parseInt(s.envelope[column], 16);
            if (!Number.isFinite(level) || level === 0) continue;
            const half = Math.max(1, Math.round(level * (middle - 5) / 15));
            const x = Math.floor(column * columnWidth);
            const nextX = Math.floor((column + 1) * columnWidth);
            ctx.fillRect(x, middle - half, Math.max(1, nextX - x), half * 2 + 1, 1);
        }

        // Fixed playhead: the waveform moves beneath this line.
        const playhead = Math.floor(width / 2);
        ctx.fillRect(playhead, 0, 1, height, 1);
        ctx.fillRect(playhead - 2, 0, 5, 1, 1);
        ctx.print(1, 1, `${s.position.toFixed(2)}s`, 1);
        const direction = Math.abs(s.pitch) < 0.02 ? "STOP" : (s.pitch < 0 ? "REV" : "FWD");
        ctx.print(Math.max(70, width - 44), 1,
                  `${s.locked ? "L" : "?"} ${direction}`, 1);
    },
};
