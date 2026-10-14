"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");

// Only the DOM operations used by the MOD callback; no browser or npm packages.
class Element {
    constructor(tag) {
        this.tag = tag;
        this.children = [];
        this.attributes = {};
        this.listeners = {};
        this.className = "";
        this.value = "";
        this.style = {};
        this._text = "";
        this.classList = {
            contains: name => this.className.split(/\s+/).includes(name),
            toggle: (name, on) => {
                const classes = new Set(this.className.split(/\s+/).filter(Boolean));
                if (on) { classes.add(name); } else { classes.delete(name); }
                this.className = [...classes].join(" ");
            }
        };
    }
    set textContent(value) { this._text = value; this.children = []; }
    get textContent() { return this._text; }
    appendChild(child) {
        this.children.push(child);
        if (this.tag === "select" && this.children.length === 1) {
            this.value = child.value;
        }
    }
    setAttribute(name, value) { this.attributes[name] = String(value); }
    getAttribute(name) { return this.attributes[name]; }
    addEventListener(name, callback) {
        (this.listeners[name] || (this.listeners[name] = [])).push(callback);
    }
    setPointerCapture() {}
    emit(type, event = {}) {
        const input = {target: this, preventDefault() {}, ...event};
        for (const callback of this.listeners[type] || []) { callback(input); }
    }
    matches(selector) {
        if (selector.startsWith(".")) { return this.classList.contains(selector.slice(1)); }
        const match = selector.match(/^\[([^=\]]+)(?:="([^"]*)")?\]$/);
        if (match) {
            return match[2] === undefined ? match[1] in this.attributes :
                this.attributes[match[1]] === match[2];
        }
        return this.tag === selector;
    }
    querySelectorAll(selector) {
        const result = [];
        for (const child of this.children) {
            if (child.matches(selector)) { result.push(child); }
            result.push(...child.querySelectorAll(selector));
        }
        return result;
    }
    querySelector(selector) { return this.querySelectorAll(selector)[0] || null; }
}

const root = new Element("div");
for (const name of ["sampler16-grid", "sampler16-status"]) {
    const element = new Element("div");
    element.className = name;
    root.appendChild(element);
}
const source = fs.readFileSync("modgui/script.js", "utf8");
for (const symbol of ["level_l", "level_r"]) {
    const meter = new Element("div");
    meter.setAttribute("data-meter", symbol);
    const fill = new Element("div");
    fill.className = "sampler16-meter-fill";
    meter.appendChild(fill);
    root.appendChild(meter);
}
const callback = new vm.Script("(" + source + ")").runInNewContext({
    document: {createElement: tag => new Element(tag)}
});
const calls = [], requests = [];
const funcs = {
    set_port_value: (symbol, value) => calls.push({symbol, value}),
    patch_get: uri => requests.push(uri)
};
callback({type: "start", icon: [root], ports: [{symbol: "mode01", value: 1}]}, funcs);
assert.equal(root.querySelectorAll("[data-trigger]").length, 16);
assert.equal(root.querySelectorAll("[data-port]").length, 16);
assert.equal(requests.length, 16);
assert.equal(root.querySelector("input"), null);
assert.equal(root.querySelector('[data-port="gain01"]'), null);
assert.equal(root.querySelector('[data-port="note01"]'), null);
const button = root.querySelector('[data-trigger="trigger01"]');
assert.equal(button.querySelector(".sampler16-number").textContent, "01");
assert.equal(root.querySelectorAll(".sampler16-number").length, 16);
assert.equal(root.querySelector("[data-sample]").textContent, "");
assert(!fs.readFileSync("modgui/icon.html", "utf8").includes("<footer"));
button.emit("keydown", {key: " ", repeat: false});
button.emit("keydown", {key: " ", repeat: true});
button.emit("keyup", {key: " "});
button.emit("blur");
assert.deepEqual(calls, [{symbol: "trigger01", value: 1}, {symbol: "trigger01", value: 0}]);
button.emit("pointerdown", {button: 0, pointerId: 1});
button.emit("pointercancel");
assert.equal(calls.at(-1).value, 0);
const mode = root.querySelector('[data-port="mode01"]');
mode.value = 2;
mode.emit("change");
assert.deepEqual(calls.at(-1), {symbol: "mode01", value: 2});
function port(symbol, value) { callback({type: "change", icon: [root], symbol, value}, funcs); }
port("level_l", -30);
port("level_r", 6);
assert.equal(root.querySelector('[data-meter="level_l"]').querySelector(".sampler16-meter-fill").style.width, "50%");
assert.equal(root.querySelector('[data-meter="level_r"]').querySelector(".sampler16-meter-fill").style.width, "100%");
assert(root.querySelector('[data-meter="level_r"]').classList.contains("sampler16-meter-clip"));
port("level_r", -60);
assert.equal(root.querySelector('[data-meter="level_r"]').querySelector(".sampler16-meter-fill").style.width, "0%");
assert(!root.querySelector('[data-meter="level_r"]').classList.contains("sampler16-meter-clip"));
assert(!fs.readFileSync("modgui/icon.html", "utf8").includes('mod-role="bypass"'));
{
    const html = fs.readFileSync("modgui/icon.html", "utf8");
    const input = html.slice(html.indexOf('class="mod-pedal-input"'), html.indexOf('class="mod-pedal-output"'));
    const output = html.slice(html.indexOf('class="mod-pedal-output"'));
    assert(/mod-role="input-midi-port" mod-port-symbol="events"/.test(input));
    assert(/mod-port-symbol="left"/.test(output) && /mod-port-symbol="right"/.test(output));
    assert.equal((html.match(/mod-role="(input|output)-[a-z]+-port"/g) || []).length, 3);
    // MOD-UI zooms/focuses the pedal on each click whose target has class mod-drag-handle;
    // keep exactly one full-surface handle so a click never triggers two zooms.
    const handles = html.match(/<[^>]*mod-role="drag-handle"[^>]*>/g);
    assert.equal(handles.length, 1);
    assert(/class="sampler16-drag-surface mod-drag-handle"/.test(handles[0]));
    const css = fs.readFileSync("modgui/style.css", "utf8");
    assert(/\.sampler16 \.sampler16-drag-surface\.mod-drag-handle \{[^}]*z-index: -1/.test(css));
    // Header text and read-only meters are click-through: move cursor, no text selection.
    assert(/\.sampler16 header \{[^}]*pointer-events: none;[^}]*user-select: none/.test(css));
}
port("mode01", 1);
port("active01", 1);
assert(button.classList.contains("sampler16-blinking"));
port("trigger01", 0);
assert(button.classList.contains("sampler16-blinking"));
port("active01", 0);
assert(!button.classList.contains("sampler16-blinking"));
port("mode01", 2);
port("active01", 1);
assert(button.classList.contains("sampler16-blinking"));
port("active01", 0);
assert(!button.classList.contains("sampler16-blinking"));
port("mode01", 0);
port("active01", 1);
assert(button.classList.contains("sampler16-active"));
assert(!button.classList.contains("sampler16-blinking"));
const uri = "https://github.com/NeOdYmS/16-pads-lv2#sampler";
callback({type: "change", icon: [root], uri: uri + ":sample01",
          value: "/host/kick.wav"}, funcs);
assert.equal(root.querySelector('[data-sample="' + uri + ':sample01"]').textContent, "kick.wav");
callback({type: "change", icon: [root], uri: uri + ":sample01", value: ""}, funcs);
assert.equal(root.querySelector('[data-sample="' + uri + ':sample01"]').textContent, "");
callback({type: "change", icon: [root], uri: uri + ":error", value: "Bad WAV"}, funcs);
assert.equal(root.querySelector(".sampler16-status").textContent, "Bad WAV");
callback({type: "start", icon: [root], ports: []}, {set_port_value() {}});
assert(root.querySelector(".sampler16-status").textContent.includes("lacks the MOD Patch API"));
console.log("MOD GUI callback tests passed");
