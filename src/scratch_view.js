function parseView(raw) {
    const p = String(raw || "").split(",");
    return { position: Number(p[0]) || 0, pitch: Number(p[1]) || 0,
        locked: Number(p[2]) === 1, zoom: Number(p[3]) || 0,
        playhead: Number(p[4]), envelope: p[5] || "" };
}

function relativeDelta(value) {
    if (value >= 1 && value <= 63) return value;
    if (value >= 65 && value <= 127) return -(128 - value);
    return 0;
}

function clamp(value, low, high) {
    return Math.max(low, Math.min(high, value));
}

function reanchorView(ctx) {
    const view = parseView(ctx.getParam("scratch_view_status"));
    ctx.state.position = view.position;
    ctx.state.envelope = view.envelope;
    ctx.state.playhead = view.playhead;
    ctx.state.zoom = view.zoom;
    ctx.state.envelopePosition = view.position;
}

function setVisualTarget(ctx, target, durationMs) {
    ctx.state.rampFrom = ctx.state.speed;
    ctx.state.targetSpeed = target;
    ctx.state.rampStarted = ctx.now();
    ctx.state.rampDuration = Math.max(0, durationMs || 0);
    if (!ctx.state.rampDuration) ctx.state.speed = target;
}

/* Compact 3x5 face for the fullscreen performance grid. The stock 5x7 face
 * cannot fit four labelled 32 px cells, which is why the previous layout
 * collided. Keep this local: canvas scripts are evaluated, not ES modules. */
const MINI = {
    " ":[0,0,0,0,0], "-":[0,0,7,0,0], ".":[0,0,0,0,2],
    "0":[7,5,5,5,7], "1":[2,6,2,2,7], "2":[7,1,7,4,7],
    "3":[7,1,7,1,7], "4":[5,5,7,1,1], "5":[7,4,7,1,7],
    "6":[7,4,7,5,7], "7":[7,1,2,2,2], "8":[7,5,7,5,7],
    "9":[7,5,7,1,7],
    "A":[2,5,7,5,5], "B":[6,5,6,5,6], "C":[3,4,4,4,3],
    "D":[6,5,5,5,6], "E":[7,4,6,4,7], "F":[7,4,6,4,4],
    "G":[3,4,5,5,3], "H":[5,5,7,5,5], "I":[7,2,2,2,7],
    "J":[1,1,1,5,2], "K":[5,5,6,5,5], "L":[4,4,4,4,7],
    "M":[5,7,7,5,5], "N":[5,7,7,7,5], "O":[2,5,5,5,2],
    "P":[6,5,6,4,4], "Q":[2,5,5,3,1], "R":[6,5,6,5,5],
    "S":[3,4,2,1,6], "T":[7,2,2,2,2], "U":[5,5,5,5,7],
    "V":[5,5,5,5,2], "W":[5,5,7,7,5], "X":[5,5,2,5,5],
    "Y":[5,5,2,2,2], "Z":[7,1,2,4,7]
};

function miniWidth(text) { return Math.max(0, String(text).length * 4 - 1); }
function miniPrint(ctx, x, y, text, color = 1) {
    const s = String(text).toUpperCase();
    for (let i = 0; i < s.length; ++i) {
        const rows = MINI[s[i]] || MINI[" "];
        for (let row = 0; row < 5; ++row)
            for (let col = 0; col < 3; ++col)
                if (rows[row] & (1 << (2 - col))) ctx.setPixel(x + i * 4 + col, y + row, color);
    }
}

function miniCell(ctx, column, y, label, value, highlighted = false) {
    const x = column * 32;
    const lw = miniWidth(label), vw = miniWidth(value);
    const labelX = x + Math.max(0, Math.floor((31 - lw) / 2));
    if (highlighted) ctx.fillRect(x, y - 1, 31, 7, 1);
    miniPrint(ctx, labelX, y, label, highlighted ? 0 : 1);
    miniPrint(ctx, x + Math.max(0, Math.floor((31 - vw) / 2)), y + 7, value);
}

function drawWave(ctx, state, height, top = 0) {
    const width = Math.min(128, ctx.width || 128);
    const middle = Math.floor(height / 2);
    const columns = Math.min(128, state.envelope.length);
    for (let column = 0; column < columns; ++column) {
        const level = parseInt(state.envelope[column], 16);
        if (!Number.isFinite(level) || level === 0) continue;
        const half = Math.max(1, Math.round(level * (middle - 5) / 15));
        ctx.fillRect(column, top + middle - half, 1, half * 2 + 1, 1);
    }
    const playhead = state.zoom === 3 && Number.isFinite(state.playhead)
        ? Math.max(0, Math.min(width - 1, Math.round(state.playhead * (width - 1) / 127)))
        : Math.floor(width / 2);
    ctx.fillRect(playhead, top, 1, height, 1);
    ctx.fillRect(playhead - 2, top, 5, 1, 1);
}

globalThis.canvas_overlay = {
    drawPage(ctx, { values }) {
        const state = parseView(values.scratch_view_status);
        const width = Math.min(128, ctx.width || 128);
        const height = ctx.height || 42;
        drawWave(ctx, state, height);
        ctx.print(1, 1, `${state.position.toFixed(2)}s`, 1);
        const direction = Math.abs(state.pitch) < 0.02 ? "STOP" : (state.pitch < 0 ? "REV" : "FWD");
        const zoomLabel = ["1S", "4S", "16S", "ALL"][state.zoom] || "?";
        ctx.print(Math.max(70, width - 44), 1, `${zoomLabel} ${direction}`, 1);
    },

    onOpen(ctx) {
        const view = parseView(ctx.getParam("scratch_view_status"));
        const controlMode = Number(ctx.getParam("control_mode")) || 0;
        const sensitivity = controlMode === 1
            ? (Number(ctx.getParam("knob_sensitivity")) || 0.18)
            : (Number(ctx.getParam("jog_sensitivity")) || 0.55);
        Object.assign(ctx.state, view, { speed: view.pitch, targetSpeed: view.pitch,
            rampFrom: view.pitch, rampStarted: ctx.now(), rampDuration: 0,
            envelopePosition: view.position, lastMotion: 0,
            lastTick: ctx.now(), held: {},
            sensitivity,
            smoothing: Number(ctx.getParam("knob_smoothing_ms")) || 0,
            inertia: Number(ctx.getParam("touch_inertia_ms")) || 0,
            jogTouch: Number(ctx.getParam("jog_touch")) !== 0,
            platterTouched: false,
            touchedControl: -1,
            zoomAccumulator: 0,
            motorSpeed: Number(ctx.getParam("virtual_speed")) || 1,
            controlMode });
        const motorPlay = ctx.getParam("virtual_play");
        ctx.state.motorPlay = motorPlay === null || motorPlay === ""
            ? true : Number(motorPlay) !== 0;
        ctx.setParam("jog_active", "1");
        ctx.setParam("jog_gate", "0");
    },

    onMidi(ctx, { data }) {
        const status = (data[0] || 0) & 0xf0;
        const number = data[1] || 0;
        const value = data[2] || 0;
        if (number <= 7 && (status === 0x80 || status === 0x90))
            ctx.state.touchedControl = status === 0x90 && value !== 0 ? number :
                (ctx.state.touchedControl === number ? -1 : ctx.state.touchedControl);
        const motionCc = ctx.state.controlMode === 1 ? 71 : 14;
        if (ctx.state.controlMode !== 0 && status === 0xb0 && number === motionCc) {
            const delta = relativeDelta(value);
            if (delta !== 0) {
                const now = ctx.now();
                let rate;
                if (ctx.state.controlMode === 1) {
                    const eventMs = ctx.state.lastMotion
                        ? clamp(now - ctx.state.lastMotion, 8, 100) : 35;
                    const measured = clamp(delta * ctx.state.sensitivity * 35 / eventMs, -8, 8);
                    /* A short velocity estimator removes UI/MIDI interval jitter;
                     * the DSP then performs sample-continuous interpolation. */
                    rate = ctx.state.speed * 0.55 + measured * 0.45;
                } else {
                    const eventMs = ctx.state.lastMotion
                        ? clamp(now - ctx.state.lastMotion, 8, 160) : 50;
                    rate = clamp(delta * ctx.state.sensitivity * 50 / eventMs, -8, 8);
                }
                ctx.setParam("jog_rate", String(rate));
                setVisualTarget(ctx, rate, ctx.state.smoothing);
                ctx.state.lastMotion = now;
            }
            return;
        }
        if (ctx.state.controlMode !== 0 && status === 0xb0 && number >= 72 && number <= 74) {
            const delta = relativeDelta(value);
            if (!delta) return;
            if (number === 72) {
                const isKnob = ctx.state.controlMode === 1;
                const low = isKnob ? 0.02 : 0.1;
                const high = isKnob ? 1 : 2;
                const step = isKnob ? 0.02 : 0.05;
                ctx.state.sensitivity = clamp(ctx.state.sensitivity + delta * step, low, high);
                ctx.setParam(isKnob ? "knob_sensitivity" : "jog_sensitivity",
                    ctx.state.sensitivity.toFixed(2));
            } else if (number === 73) {
                ctx.state.smoothing = clamp(ctx.state.smoothing + delta * 5, 0, 120);
                ctx.setParam("knob_smoothing_ms", String(ctx.state.smoothing));
            } else {
                ctx.state.inertia = clamp(ctx.state.inertia + delta * 5, 0, 250);
                ctx.setParam("touch_inertia_ms", String(ctx.state.inertia));
            }
            return;
        }
        if (ctx.state.controlMode !== 0 && status === 0xb0 && number === 75) {
            const delta = relativeDelta(value);
            if (!delta) return;
            ctx.state.jogTouch = delta > 0;
            ctx.setParam("jog_touch", ctx.state.jogTouch ? "1" : "0");
            return;
        }
        if (status === 0xb0 && number === 76) {
            const delta = relativeDelta(value);
            if (!delta) return;
            ctx.state.motorSpeed = clamp(ctx.state.motorSpeed + delta * 0.05, 0.1, 2);
            ctx.setParam("virtual_speed", ctx.state.motorSpeed.toFixed(2));
            return;
        }
        if (status === 0xb0 && number === 77) {
            const delta = relativeDelta(value);
            if (!delta) return;
            ctx.state.zoomAccumulator += delta;
            const steps = Math.trunc(ctx.state.zoomAccumulator / 8);
            if (!steps) return;
            ctx.state.zoomAccumulator -= steps * 8;
            ctx.state.zoom = clamp(ctx.state.zoom + steps, 0, 3);
            const zoomValue = String(ctx.state.zoom);
            const immediate = typeof ctx.setParamImmediate === "function" &&
                ctx.setParamImmediate("wave_zoom", zoomValue, 25);
            if (!immediate) ctx.setParam("wave_zoom", zoomValue);
            /* Fullscreen canvas draw/tick deliberately cannot read params.
             * Refresh the zoom-dependent envelope here, on the user event. */
            reanchorView(ctx);
            return;
        }
        if (ctx.state.controlMode !== 0 && status === 0xb0 && number === 78) {
            const delta = relativeDelta(value);
            if (!delta) return;
            ctx.state.motorPlay = delta > 0;
            ctx.setParam("virtual_play", ctx.state.motorPlay ? "1" : "0");
            if (!ctx.state.platterTouched)
                setVisualTarget(ctx, ctx.state.motorPlay ? ctx.state.motorSpeed : 0,
                    ctx.state.inertia);
            return;
        }
        if (ctx.state.controlMode === 1 && ctx.state.jogTouch && number === 0 &&
            (status === 0x80 || status === 0x90)) {
            const touched = status === 0x90 && value !== 0;
            const immediate = typeof ctx.setParamImmediate === "function" &&
                ctx.setParamImmediate("knob_touch", touched ? "1" : "0", 25);
            if (!immediate)
                ctx.setParam("knob_touch", touched ? "1" : "0");
            reanchorView(ctx);
            setVisualTarget(ctx, touched ? 0 :
                (ctx.state.motorPlay ? ctx.state.motorSpeed : 0), ctx.state.inertia);
            ctx.state.lastMotion = touched ? ctx.now() : 0;
            return;
        }
        if (ctx.state.controlMode === 2 && ctx.state.jogTouch && number === 9 &&
            (status === 0x80 || status === 0x90)) {
            const touched = status === 0x90 && value !== 0;
            ctx.state.platterTouched = touched;
            const immediate = typeof ctx.setParamImmediate === "function" &&
                ctx.setParamImmediate("knob_touch", touched ? "1" : "0", 25);
            if (!immediate)
                ctx.setParam("knob_touch", touched ? "1" : "0");
            reanchorView(ctx);
            setVisualTarget(ctx, touched ? 0 :
                (ctx.state.motorPlay ? ctx.state.motorSpeed : 0), ctx.state.inertia);
            ctx.state.lastMotion = touched ? ctx.now() : 0;
            return;
        }
        /* Depending on the active Move layout, the canvas may receive either
         * physical pad notes or translated musical notes. Notes 0-9 are the
         * capacitive knob sensors; every higher note is playable input. */
        if (number >= 10 && status === 0x90 && value !== 0) {
            const wasHeld = !!ctx.state.held[number];
            ctx.state.held[number] = true;
            if (!wasHeld) ctx.setParam("jog_gate", "2");
        } else if (number >= 10 &&
                   (status === 0x80 || (status === 0x90 && value === 0))) {
            delete ctx.state.held[number];
            ctx.setParam("jog_gate", Object.keys(ctx.state.held).length ? "1" : "0");
        }
    },

    onValues(ctx, { values }) {
        if (ctx.state.controlMode !== 0 || !values || !values.scratch_view_status) return;
        const view = parseView(values.scratch_view_status);
        ctx.state.position = view.position;
        ctx.state.speed = view.pitch;
        ctx.state.targetSpeed = view.pitch;
        ctx.state.rampDuration = 0;
        ctx.state.envelope = view.envelope;
        ctx.state.envelopePosition = view.position;
        ctx.state.playhead = view.playhead;
        ctx.state.zoom = view.zoom;
    },

    tick(ctx) {
        const now = ctx.now();
        const elapsed = Math.max(0, Math.min(0.1, (now - ctx.state.lastTick) / 1000));
        if (ctx.state.controlMode === 2 && !ctx.state.platterTouched &&
            now - ctx.state.lastMotion > 120 &&
            ctx.state.targetSpeed !== (ctx.state.motorPlay ? ctx.state.motorSpeed : 0))
            setVisualTarget(ctx, ctx.state.motorPlay ? ctx.state.motorSpeed : 0,
                ctx.state.inertia);
        if (ctx.state.rampDuration) {
            const progress = clamp((now - ctx.state.rampStarted) / ctx.state.rampDuration, 0, 1);
            ctx.state.speed = ctx.state.rampFrom +
                (ctx.state.targetSpeed - ctx.state.rampFrom) * progress;
            if (progress >= 1) ctx.state.rampDuration = 0;
        }
        ctx.state.position = Math.max(0, ctx.state.position + ctx.state.speed * elapsed);
        ctx.state.lastTick = now;
    },

    draw(ctx) {
        ctx.clear();
        const width = ctx.width || 128;
        const height = ctx.height || 64;
        const envelope = ctx.state.envelope || "";
        const pixelsPerSecond = [128, 32, 8, 0][ctx.state.zoom] || 0;
        const rawOffset = envelope.length && pixelsPerSecond
            ? Math.round((ctx.state.position - ctx.state.envelopePosition) * pixelsPerSecond) : 0;
        const offset = envelope.length ?
            ((rawOffset % envelope.length) + envelope.length) % envelope.length : 0;
        const shifted = envelope.slice(offset) + envelope.slice(0, offset);
        drawWave(ctx, { ...ctx.state, envelope: shifted }, 22, 20);
        const direction = ctx.state.speed < -0.02 ? "REV" :
            (ctx.state.speed > 0.02 ? "FWD" : "STOP");
        const mode = ["DVS", "KNOB", "JOG"][ctx.state.controlMode] || "?";
        miniCell(ctx, 0, 0, "SCRATCH", `${mode} ${direction}`, ctx.state.touchedControl === 0);
        if (ctx.state.controlMode !== 0) {
            miniCell(ctx, 1, 0, "FEEL", ctx.state.sensitivity.toFixed(2), ctx.state.touchedControl === 1);
            miniCell(ctx, 2, 0, "SMOOTH", `${Math.round(ctx.state.smoothing)}MS`, ctx.state.touchedControl === 2);
            miniCell(ctx, 3, 0, "INERTIA", `${Math.round(ctx.state.inertia)}MS`, ctx.state.touchedControl === 3);
            miniCell(ctx, 0, 50, "TOUCH", ctx.state.jogTouch ? "ON" : "OFF", ctx.state.touchedControl === 4);
            miniCell(ctx, 1, 50, "SPEED", `${ctx.state.motorSpeed.toFixed(2)}X`, ctx.state.touchedControl === 5);
            miniCell(ctx, 2, 50, "ZOOM", ["1 SEC", "4 SEC", "16 SEC", "ALL"][ctx.state.zoom] || "?", ctx.state.touchedControl === 6);
            miniCell(ctx, 3, 50, "MOTOR", ctx.state.motorPlay ? "PLAY" : "STOP", ctx.state.touchedControl === 7);
        } else {
            miniCell(ctx, 0, 50, "PADS", "CUT");
            miniCell(ctx, 1, 50, "SPEED", `${ctx.state.motorSpeed.toFixed(2)}X`, ctx.state.touchedControl === 5);
            miniCell(ctx, 2, 50, "ZOOM", ["1 SEC", "4 SEC", "16 SEC", "ALL"][ctx.state.zoom] || "?", ctx.state.touchedControl === 6);
            miniCell(ctx, 3, 50, "EXIT", "CLICK");
        }
    },

    onClose(ctx) {
        ctx.setParam("knob_touch", "0");
        ctx.setParam("jog_gate", "0");
        ctx.setParam("jog_active", "0");
    },
};
