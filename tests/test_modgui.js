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
callback({type: "change", icon: [root], uri: uri + ":error", value: "Bad WAV"}, funcs);
assert.equal(root.querySelector(".sampler16-status").textContent, "Bad WAV");
callback({type: "start", icon: [root], ports: []}, {set_port_value() {}});
assert(root.querySelector(".sampler16-status").textContent.includes("lacks the MOD Patch API"));
console.log("MOD GUI callback tests passed");
