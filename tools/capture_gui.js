"use strict";
// Optional developer tool; screenshots are local MOD callback renders, not Pi captures.
const fs = require("node:fs");
const path = require("node:path");
const {chromium} = require(process.env.PLAYWRIGHT_MODULE || "playwright");

async function main() {
    const root = path.resolve(__dirname, "..");
    const browser = await chromium.launch({headless: true});
    try {
        const page = await browser.newPage({viewport: {width: 900, height: 900}, deviceScaleFactor: 1});
        const errors = [];
        page.on("pageerror", error => errors.push(error.message));
        const icon = fs.readFileSync(path.join(root, "modgui/icon.html"), "utf8").replace("{{cns}}", "");
        const css = fs.readFileSync(path.join(root, "modgui/style.css"), "utf8");
        await page.setContent('<!doctype html><html><meta charset="utf-8"><style>' +
            css + "</style><body style='margin:0;background:transparent'>" + icon + "</body></html>");
        const callback = fs.readFileSync(path.join(root, "modgui/script.js"), "utf8");
        await page.evaluate(source => {
            const render = new Function("return (" + source + ")")();
            const icon = document.querySelector(".sampler16");
            render({type: "start", icon: [icon], ports: [], parameters: []},
                   {set_port_value() {}, patch_get() {}});
            for (const [symbol, value] of [["level_l", -30], ["level_r", 6]]) {
                render({type: "change", icon: [icon], symbol, value}, {});
            }
            const left = icon.querySelector('[data-meter="level_l"]');
            const right = icon.querySelector('[data-meter="level_r"]');
            if (left.querySelector(".sampler16-meter-fill").style.width !== "50%" ||
                !right.classList.contains("sampler16-meter-clip") ||
                left.getBoundingClientRect().width <= left.getBoundingClientRect().height) {
                throw new Error("Stereo horizontal meter feedback failed");
            }
            for (const symbol of ["level_l", "level_r"]) {
                render({type: "change", icon: [icon], symbol, value: -60}, {});
            }
        }, callback);
        if (await page.locator("[data-trigger]").count() !== 16 || errors.length) {
            throw new Error("GUI render failed: " + errors.join("; "));
        }
        await page.evaluate(() => {
            const root = document.querySelector(".sampler16");
            const wrapper = document.createElement("div");
            root.before(wrapper);
            wrapper.appendChild(root);
            wrapper.style.width = "46px";
            wrapper.style.transformOrigin = "top left";
            wrapper.style.transform = "scale(0.283)";
            // MOD-UI repeatedly copies jQuery child content dimensions to its wrapper.
            for (let iteration = 0; iteration < 20; iteration++) {
                const style = getComputedStyle(root);
                const contentWidth = parseFloat(style.width) -
                    parseFloat(style.paddingLeft) - parseFloat(style.paddingRight) -
                    parseFloat(style.borderLeftWidth) - parseFloat(style.borderRightWidth);
                wrapper.style.width = contentWidth + "px";
                if (Math.abs(root.getBoundingClientRect().width / 0.283 - 640) > 1) {
                    throw new Error("MOD-UI sizing feedback shrank the plugin");
                }
            }
            wrapper.before(root);
            wrapper.remove();
        });
        await page.evaluate(() => {
            const header = document.querySelector(".sampler16 header");
            const items = [...header.children].map(item => item.getBoundingClientRect());
            const centers = items.map(bounds => bounds.top + bounds.height / 2);
            if (items.length !== 3 || Math.max(...centers) - Math.min(...centers) > 1 ||
                items[0].right > items[1].left || items[1].right > items[2].left ||
                items[1].width > 220 || items[2].right > header.getBoundingClientRect().right) {
                throw new Error("Logo, compact stereo meter and subtitle must share one row");
            }
            if (document.querySelector(".sampler16-grid").getBoundingClientRect().top -
                header.getBoundingClientRect().bottom > 14) {
                throw new Error("Excessive empty space below header");
            }
            const pedal = document.querySelector(".sampler16").getBoundingClientRect();
            const grid = document.querySelector(".sampler16-grid").getBoundingClientRect();
            const midi = document.querySelector('[mod-port-symbol="events"]').getBoundingClientRect();
            const outs = ["left", "right"].map(symbol =>
                document.querySelector('[mod-port-symbol="' + symbol + '"]').getBoundingClientRect());
            if (midi.right > pedal.left + 4 || outs.some(out => out.left < pedal.right - 4) ||
                outs[0].top >= outs[1].top ||
                [midi, ...outs].some(jack => jack.top < grid.top || jack.bottom > grid.bottom)) {
                throw new Error("Jacks must sit on the pedal edges, beside the pad grid");
            }
            for (const button of document.querySelectorAll("[data-trigger]")) {
                const bounds = button.getBoundingClientRect();
                if (Math.abs(bounds.width - bounds.height) > 1) {
                    throw new Error("Pad is not square");
                }
                const number = button.querySelector(".sampler16-number");
                const numberBounds = number.getBoundingClientRect();
                if (numberBounds.left < bounds.left + bounds.width / 2 ||
                    numberBounds.top < bounds.top + bounds.height / 2) {
                    throw new Error("Pad number is not at the bottom right");
                }
            }
            if (document.querySelector("footer") || document.body.textContent.includes("No sample")) {
                throw new Error("Unwanted footer or sample placeholder");
            }
        });
        const screenshot = await page.locator(".sampler16").screenshot({
            path: path.join(root, "modgui/screenshot.png"), omitBackground: true
        });
        const thumbnail = await page.evaluate(async data => {
            const image = new Image();
            image.src = "data:image/png;base64," + data;
            await image.decode();
            const scale = Math.min(256 / image.width, 64 / image.height, 1);
            const canvas = document.createElement("canvas");
            canvas.width = Math.max(1, Math.round(image.width * scale));
            canvas.height = Math.max(1, Math.round(image.height * scale));
            canvas.getContext("2d").drawImage(image, 0, 0, canvas.width, canvas.height);
            return canvas.toDataURL("image/png").split(",")[1];
        }, screenshot.toString("base64"));
        fs.writeFileSync(path.join(root, "modgui/thumbnail.png"), Buffer.from(thumbnail, "base64"));
        console.log("Captured local MOD callback screenshot and <=256x64 thumbnail");
    } finally {
        await browser.close();
    }
}
main().catch(error => { console.error(error); process.exitCode = 1; });
