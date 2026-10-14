function (event, jsFuncs) {
    var root = event.icon[0];
    var uri = "https://github.com/NeOdYmS/16-pads-lv2#sampler";
    var status = root.querySelector(".sampler16-status");
    var state = root.sampler16State || (root.sampler16State = {modes: {}, active: {}});
    function updatePad(suffix) {
        var button = root.querySelector('[data-trigger="trigger' + suffix + '"]');
        if (!button) { return; }
        var active = state.active[suffix] >= 0.5;
        var mode = Number(state.modes[suffix] || 0);
        button.classList.toggle("sampler16-active", active);
        button.classList.toggle("sampler16-blinking", active && (mode === 1 || mode === 2));
    }
    function setControl(symbol, value) {
        var meter = root.querySelector('[data-meter="' + symbol + '"]');
        if (meter && Number.isFinite(Number(value))) {
            var db = Math.max(-60, Math.min(0, Number(value)));
            meter.querySelector(".sampler16-meter-fill").style.width = ((db + 60) / 60 * 100) + "%";
            meter.setAttribute("aria-valuenow", db);
            meter.setAttribute("aria-valuetext", Number(value).toFixed(1) + " dBFS");
            meter.classList.toggle("sampler16-meter-clip", Number(value) >= 0);
        }
        var control = root.querySelector('[data-port="' + symbol + '"]');
        if (control) { control.value = value; }
        var trigger = root.querySelector('[data-trigger="' + symbol + '"]');
        if (trigger) {
            trigger.classList.toggle("sampler16-held", value >= 0.5);
        }
        if (symbol.indexOf("mode") === 0) {
            state.modes[symbol.slice(4)] = value;
            updatePad(symbol.slice(4));
        } else if (symbol.indexOf("active") === 0) {
            state.active[symbol.slice(6)] = value;
            updatePad(symbol.slice(6));
        }
    }
    function setParameter(parameter) {
        if (parameter.uri === uri + ":error") {
            status.textContent = parameter.value;
            return;
        }
        var sample = root.querySelector('[data-sample="' + parameter.uri + '"]');
        if (sample) {
            sample.textContent = parameter.value ?
                parameter.value.split(/[\\/]/).pop() : "";
            sample.title = parameter.value || "";
        }
    }
    function bindTrigger(button) {
        var held = false;
        function gate(value) {
            if (held === (value === 1)) { return; }
            held = value === 1;
            button.classList.toggle("sampler16-held", held);
            jsFuncs.set_port_value(button.getAttribute("data-trigger"), value);
        }
        button.addEventListener("pointerdown", function (input) {
            if (input.button !== 0) { return; }
            button.setPointerCapture(input.pointerId);
            gate(1);
        });
        button.addEventListener("pointerup", function () { gate(0); });
        button.addEventListener("pointercancel", function () { gate(0); });
        button.addEventListener("lostpointercapture", function () { gate(0); });
        button.addEventListener("keydown", function (input) {
            if (input.key === " " || input.key === "Enter") {
                input.preventDefault();
                if (!input.repeat) { gate(1); }
            }
        });
        button.addEventListener("keyup", function (input) {
            if (input.key === " " || input.key === "Enter") { gate(0); }
        });
        button.addEventListener("blur", function () { gate(0); });
    }
    if (event.type === "start") {
        var grid = root.querySelector(".sampler16-grid");
        grid.textContent = "";
        var patchSupported = typeof jsFuncs.patch_get === "function";
        for (var pad = 1; pad <= 16; pad++) {
            var suffix = ("0" + pad).slice(-2);
            var card = document.createElement("div");
            card.className = "sampler16-pad";
            var heading = document.createElement("button");
            heading.type = "button";
            heading.className = "sampler16-hit";
            heading.setAttribute("data-trigger", "trigger" + suffix);
            heading.setAttribute("aria-label", "Trigger pad " + suffix);
            var number = document.createElement("span");
            number.className = "sampler16-number";
            number.textContent = suffix;
            heading.appendChild(number);
            bindTrigger(heading);
            card.appendChild(heading);
            var mode = document.createElement("select");
            [["0", "One-shot"], ["1", "Loop"], ["2", "Trig"]].forEach(function (spec) {
                var option = document.createElement("option");
                option.value = spec[0];
                option.textContent = spec[1];
                mode.appendChild(option);
            });
            mode.setAttribute("data-port", "mode" + suffix);
            mode.setAttribute("aria-label", "Pad " + suffix + " playback mode");
            var label = document.createElement("label");
            label.textContent = "Mode ";
            label.appendChild(mode);
            card.appendChild(label);
            var sample = document.createElement("span");
            sample.className = "sampler16-sample";
            sample.setAttribute("data-sample", uri + ":sample" + suffix);
            sample.textContent = "";
            card.appendChild(sample);
            grid.appendChild(card);
            mode.addEventListener("change", function (event) {
                var value = Number(event.target.value);
                if (!Number.isFinite(value)) {
                    status.textContent = "Enter a finite control value.";
                    return;
                }
                value = Math.max(0, Math.min(2, Math.round(value)));
                setControl(event.target.getAttribute("data-port"), value);
                jsFuncs.set_port_value(event.target.getAttribute("data-port"), value);
            });
        }
        (event.ports || []).forEach(function (port) {
            setControl(port.symbol, port.value);
        });
        (event.parameters || []).forEach(setParameter);
        if (patchSupported) {
            for (var pad = 1; pad <= 16; pad++) {
                jsFuncs.patch_get(uri + ":sample" + ("0" + pad).slice(-2));
            }
        } else {
            status.textContent = "Host lacks the MOD Patch API. Use generic host controls and state.";
        }
    } else if (event.type === "change") {
        if (event.symbol) {
            setControl(event.symbol, event.value);
        } else if (event.uri) {
            setParameter(event);
        }
    }
}
