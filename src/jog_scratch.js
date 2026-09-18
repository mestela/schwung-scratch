function parseScratchStatus(raw) {
    const fields = String(raw || "").split(",");
    return {
        position: Number(fields[0]) || 0,
        envelope: fields[5] || "",
    };
}

function jogDelta(value) {
    if (value >= 1 && value <= 63) return value;
    if (value >= 65 && value <= 127) return -(128 - value);
    return 0;
}

globalThis.canvas_overlay = {
    onOpen(ctx) {
        const view = parseScratchStatus(ctx.getParam("scratch_view_status"));
        ctx.state.position = view.position;
        ctx.state.envelope = view.envelope;
        ctx.state.speed = 0;
        ctx.state.lastMotion = 0;
        ctx.state.lastTick = ctx.now();
        ctx.state.held = {};
        ctx.state.sensitivity = Number(ctx.getParam("jog_sensitivity")) || 0.55;
        const motorPlay = ctx.getParam("virtual_play");
        ctx.state.motorPlay = motorPlay === null || motorPlay === ""
            ? true : Number(motorPlay) !== 0;
        ctx.state.motorSpeed = Number(ctx.getParam("virtual_speed")) || 1;
        ctx.state.controlMode = Number(ctx.getParam("control_mode")) || 0;
        ctx.setParam("jog_active", ctx.state.controlMode === 2 ? "1" : "0");
        ctx.setParam("jog_gate", "0");
    },

    onMidi(ctx, { data }) {
        const status = (data[0] || 0) & 0xf0;
        const number = data[1] || 0;
        const value = data[2] || 0;

        if (status === 0xb0 && number === 14) {
            const delta = jogDelta(value);
            if (delta !== 0) {
                ctx.setParam("jog_delta", String(delta));
                ctx.state.speed = delta * ctx.state.sensitivity;
                ctx.state.position = Math.max(0, ctx.state.position + ctx.state.speed * 0.05);
                ctx.state.lastMotion = ctx.now();
            }
            return;
        }

        if (number >= 68 && number <= 99 && status === 0x90 && value !== 0) {
            const wasHeld = !!ctx.state.held[number];
            ctx.state.held[number] = true;
            if (!wasHeld) ctx.setParam("jog_gate", "2");
        } else if (number >= 68 && number <= 99 &&
                   (status === 0x80 || (status === 0x90 && value === 0))) {
            delete ctx.state.held[number];
            const anyHeld = Object.keys(ctx.state.held).length > 0;
            ctx.setParam("jog_gate", anyHeld ? "1" : "0");
        }
    },

    tick(ctx) {
        const now = ctx.now();
        const elapsed = Math.max(0, Math.min(0.1, (now - ctx.state.lastTick) / 1000));
        if (now - ctx.state.lastMotion > 120)
            ctx.state.speed = ctx.state.motorPlay ? ctx.state.motorSpeed : 0;
        ctx.state.position = Math.max(0, ctx.state.position + ctx.state.speed * elapsed);
        ctx.state.lastTick = now;
    },

    draw(ctx) {
        ctx.clear();
        const width = ctx.width || 128;
        const height = ctx.height || 64;
        const middle = 31;
        const envelope = ctx.state.envelope || "";
        if (ctx.state.controlMode !== 2) {
            ctx.print(8, 18, "SET CONTROL: JOG", 1);
            ctx.print(13, 34, "THEN REOPEN", 1);
            ctx.print(31, 52, "BACK", 1);
            return;
        }
        const shift = Math.round(ctx.state.position * 24);

        for (let x = 0; x < width; ++x) {
            if (!envelope.length) break;
            const index = ((x + shift) % envelope.length + envelope.length) % envelope.length;
            const level = parseInt(envelope[index], 16) || 0;
            const half = Math.max(1, Math.round(level * 17 / 15));
            ctx.fillRect(x, middle - half, 1, half * 2 + 1, 1);
        }

        ctx.fillRect(Math.floor(width / 2), 9, 1, 44, 1);
        ctx.fillRect(Math.floor(width / 2) - 2, 9, 5, 1, 1);
        const direction = ctx.state.speed < 0 ? "REV" : (ctx.state.speed > 0 ? "FWD" : "STOP");
        ctx.print(1, 0, `${ctx.state.position.toFixed(2)}s`, 1);
        ctx.print(Math.max(76, width - 50), 0, direction, 1);
        ctx.print(1, height - 9, "PADS CUT", 1);
        ctx.print(Math.max(63, width - 65), height - 9, "CLICK/BACK", 1);
    },

    onClose(ctx) {
        ctx.setParam("jog_gate", "0");
        ctx.setParam("jog_active", "0");
    },
};
