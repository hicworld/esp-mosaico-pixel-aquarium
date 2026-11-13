import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { closeSync, openSync, readFileSync, writeFileSync } from 'node:fs';

const WIDTH = 480, HEIGHT = 400;
const GRID = 384, ORIGIN_X = 48, ORIGIN_Y = 8;
const FRAME_MS = 50, REVEAL_MS = 5000, FEED_MS = 4200;
const FAST_HZ = 60;
const EVENT_TIMER = 1, EVENT_PREPARE_FRAME = 9, EVENT_DRAIN_FRAME = 10;

function fastStepMs(frame) {
    return Math.floor((frame + 1) * 1000 / FAST_HZ) - Math.floor(frame * 1000 / FAST_HZ);
}
const wasm = await WebAssembly.compile(readFileSync(new URL('./dist/pixel-fish.wasm', import.meta.url)));

function rgb888ToRgb565(rgb) {
    return ((rgb >> 8) & 0xf800) | ((rgb >> 5) & 0x07e0) | ((rgb >> 3) & 0x001f);
}

function rgb565ToRgb888(pixel) {
    const r = pixel >> 11, g = (pixel >> 5) & 63, b = pixel & 31;
    return ((r << 3 | r >> 2) << 16) | ((g << 2 | g >> 4) << 8) | (b << 3 | b >> 2);
}

async function createHarness({ initialStorage = {}, audioAvailable = true } = {}) {
    // Start near the end of the 32-bit clock so the reveal crosses its wrap.
    let now = 0xfffffc00;
    let scenes = 0;
    let scenesThisEntry = 0, images = 0;
    let maxCommands = 0, maxImages = 0;
    const canvas = new Uint32Array(WIDTH * HEIGHT);
    const storage = new Map(Object.entries(initialStorage));
    const audioCalls = [];

    function checkBounds(x, y, width, height) {
        assert(x >= 0 && y >= 0 && width > 0 && height > 0);
        assert(x + width <= WIDTH && y + height <= HEIGHT);
    }

    const instance = await WebAssembly.instantiate(wasm, { platform: {
        millis: () => now,
        audio(kind, value) {
            audioCalls.push({ kind, value });
            return audioAvailable ? 0 : -1;
        },
        scene(pointer, count, background) {
            assert.equal(background, 0);
            assert(++scenesThisEntry <= 1, 'scene call quota exceeded');
            scenes++;
            assert(count > 0 && count <= 96, `scene command count ${count}`);
            maxCommands = Math.max(maxCommands, count);
            const bytes = new DataView(instance.exports.memory.buffer);
            canvas.fill(background);
            for (let i = 0; i < count; i++) {
                const offset = pointer + i * 64;
                const [kind, rgb, x, y, width, height] = Array.from(
                    { length: 6 }, (_, field) => bytes.getInt32(offset + field * 4, true),
                );
                assert.equal(kind, 1);
                checkBounds(x, y, width, height);
                for (let row = y; row < y + height; row++) {
                    const start = row * WIDTH + x;
                    canvas.fill(rgb565ToRgb888(rgb888ToRgb565(rgb)), start, start + width);
                }
            }
            return 0;
        },
        image(x, y, width, height, pointer) {
            assert(scenes > 0, 'images require a scene canvas');
            assert(width <= 128 && height <= 128);
            assert(++images <= 8, 'image call quota exceeded');
            maxImages = Math.max(maxImages, images);
            checkBounds(x, y, width, height);
            const bytes = new DataView(instance.exports.memory.buffer, pointer, width * height * 2);
            for (let row = 0; row < height; row++) {
                const destination = (y + row) * WIDTH + x;
                for (let col = 0; col < width; col++) {
                    const pixel = bytes.getUint16(2 * (row * width + col), true);
                    canvas[destination + col] = rgb565ToRgb888(pixel);
                }
            }
            return 0;
        },
        kv_get(key, size, output, capacity) {
            const name = Buffer.from(instance.exports.memory.buffer, key, size).toString();
            const saved = storage.get(name);
            if (saved === undefined) return 0;
            assert(capacity >= 1);
            new Uint8Array(instance.exports.memory.buffer)[output] = saved;
            return 1;
        },
        kv_set(key, size, value, length) {
            assert.equal(length, 1);
            const name = Buffer.from(instance.exports.memory.buffer, key, size).toString();
            storage.set(name, new Uint8Array(instance.exports.memory.buffer)[value]);
            return 1;
        },
    } });

    function invoke(entry, ...args) {
        scenesThisEntry = 0;
        images = 0;
        instance.exports[entry](...args);
        return images;
    }

    const event = (type, x = 0, y = 0) => invoke('app_event', type, x, y);
    function tick(ms = FRAME_MS) {
        now = (now + ms) >>> 0;
        return event(EVENT_TIMER, now);
    }

    function tickFast(ms) {
        now = (now + ms) >>> 0;
        assert.equal(event(EVENT_PREPARE_FRAME, now, FAST_HZ), 0, 'preparation must not publish patches');
        let patches = event(EVENT_TIMER, now, FAST_HZ);
        for (let phase = 0; phase < 3; phase++) {
            patches += event(EVENT_DRAIN_FRAME, now, FAST_HZ);
        }
        assert.equal(event(EVENT_DRAIN_FRAME, now, FAST_HZ), 0, 'all patches finish within three drains');
        return patches;
    }

    return {
        canvas, storage, audioCalls, event, tick, tickFast,
        init: () => invoke('app_init'),
        steps: count => { for (let i = 0; i < count; i++) tick(); },
        black: () => canvas.every(rgb => rgb === 0),
        sceneCalls: () => scenes,
        frameHash: () => createHash('sha256').update(Buffer.from(canvas.buffer)).digest('hex'),
        maxCommands: () => maxCommands,
        maxImages: () => maxImages,
    };
}

// Logical view is transposed by default for the angled reflector.
const touchX = ORIGIN_X + 155, touchY = ORIGIN_Y + 225;
function fishCoverage(canvas) {
    let visible = 0, nearFood = 0;
    for (let y = ORIGIN_Y; y < ORIGIN_Y + GRID; y++) {
        for (let x = ORIGIN_X; x < ORIGIN_X + GRID; x++) {
            const rgb = canvas[y * WIDTH + x];
            const brightest = Math.max(rgb >> 16, (rgb >> 8) & 255, rgb & 255);
            if (brightest < 48) continue;
            visible++;
            if ((x - touchX) ** 2 + (y - touchY) ** 2 <= 96 ** 2) nearFood++;
        }
    }
    return { visible, nearFraction: nearFood / visible };
}

const foodRgb = rgb565ToRgb888(rgb888ToRgb565(0xc58b43));
function foodMarks(canvas) {
    let count = 0;
    for (let y = touchY - 14; y <= touchY + 14; y++) {
        for (let x = touchX - 14; x <= touchX + 14; x++) {
            if (canvas[y * WIDTH + x] === foodRgb) count++;
        }
    }
    return count;
}

const reflection = { initialStorage: { pond: 0 } };
const test = await createHarness(reflection);
test.init();
assert(test.black());
test.tick(REVEAL_MS - 1);
assert(test.black(), 'the five-second reveal must stay black until its deadline');
test.tick(1);
test.steps(10);
const firstFish = fishCoverage(test.canvas).visible;
assert(firstFish > 0, 'the first koi enters shortly after the reveal');

const firstFootprint = [];
for (let i = 0; i < test.canvas.length; i++) {
    if (test.canvas[i] !== 0) firstFootprint.push(i);
}
test.steps(60);
const before = fishCoverage(test.canvas);
assert(firstFootprint.every(i => test.canvas[i] === 0), 'the first koi leaves no lit trails');
assert.equal(test.sceneCalls(), 1, 'swimming uses the canvas created at startup');
assert(before.visible > firstFish * 1.5, 'the later koi add visible area');
const swimmingFrame = test.frameHash();
test.steps(2);
assert.notEqual(test.frameHash(), swimmingFrame, 'the rendered fish should move');

const control = await createHarness(reflection);
control.init();
control.tick(REVEAL_MS);
control.steps(72);
assert.equal(control.frameHash(), test.frameHash());
test.event(2, touchX, touchY);
test.tick();
assert(foodMarks(test.canvas) > 0, 'a tap adds visible food marks by the next frame');
test.steps(55);
control.steps(56);
const gathered = fishCoverage(test.canvas).nearFraction;
const patrol = fishCoverage(control.canvas).nearFraction;
assert(gathered > patrol + 0.12,
    `fish should gather at the touch (fed ${gathered.toFixed(2)}, patrol ${patrol.toFixed(2)})`);
test.steps(FEED_MS / FRAME_MS - 56 + 1);
assert.equal(foodMarks(test.canvas), 0, 'the food pixels should disappear');
assert.equal(test.sceneCalls(), 1, 'feeding uses the persistent canvas');
assert(fishCoverage(test.canvas).visible > firstFish * 1.5);

test.event(2, touchX, touchY);
test.steps(3);
test.event(2, touchX + 2, touchY - 2);
assert(test.black(), 'double tap restarts with a black frame');
assert.equal(test.sceneCalls(), 2, 'replay clears the old canvas once');
test.tick(REVEAL_MS - 1);
assert(test.black());
test.tick(1);
test.steps(10);
assert(!test.black());

test.event(7);
const orientations = new Set();
for (let i = 0; i < 8; i++) {
    orientations.add(test.frameHash());
    test.event(2, 240, 200);
}
assert.equal(orientations.size, 8);
test.event(2, 240, 200);
const savedAlignment = test.frameHash();
test.event(7);
assert.equal(test.storage.get('orient'), 5);
assert(test.black());
test.init();
test.event(7);
assert.equal(test.frameHash(), savedAlignment);
assert.equal(test.storage.get('orient'), 5);

const fast = await createHarness(reflection);
let fastFrame = 0;
const nextFastFrame = () => fast.tickFast(fastStepMs(fastFrame++));
const advanceFast = count => {
    for (let i = 0; i < count; i++) nextFastFrame();
};
fast.init();
advanceFast(5 * FAST_HZ - 1);
assert(fast.black(), 'the fast clock must also honor the full five-second reveal');
nextFastFrame();
advanceFast(FAST_HZ / 2);
const fastFirstFish = fishCoverage(fast.canvas).visible;
assert(fastFirstFish > 0, 'the first koi enters on the fast clock');
advanceFast(3 * FAST_HZ);
assert(fishCoverage(fast.canvas).visible > fastFirstFish * 1.5, 'all koi enter on the fast clock');

// Count timer groups after preparation and all drains, not individual image patches.
const sampleFrames = 2 * FAST_HZ;
let publishedFrames = 0, changedFrames = 0;
let repeatedFrames = 0, longestRepeat = 0;
let previousFastImage = fast.frameHash();
for (let i = 0; i < sampleFrames; i++) {
    if (nextFastFrame()) publishedFrames++;
    const image = fast.frameHash();
    if (image !== previousFastImage) {
        changedFrames++;
        repeatedFrames = 0;
    } else {
        longestRepeat = Math.max(longestRepeat, ++repeatedFrames);
    }
    previousFastImage = image;
}
assert(publishedFrames >= sampleFrames - 2,
    `60 Hz must publish moving koi throughout the sample (${publishedFrames}/${sampleFrames})`);
assert(changedFrames >= Math.floor(sampleFrames * 0.85),
    `60 Hz must advance the image rather than submit repeated patches (${changedFrames}/${sampleFrames})`);
assert(longestRepeat <= 2, `moving koi image repeated for ${longestRepeat} consecutive frame periods`);
assert.equal(fast.sceneCalls(), 1, 'fast swimming uses the persistent canvas');

fast.event(2, touchX, touchY);
nextFastFrame();
assert(foodMarks(fast.canvas) > 0, 'fast feeding publishes food in the next frame group');
advanceFast(Math.ceil(FEED_MS * FAST_HZ / 1000) + 2);
assert.equal(foodMarks(fast.canvas), 0, 'fast feeding removes expired food pixels');
assert.equal(fast.sceneCalls(), 1, 'fast feeding uses the persistent canvas');

const pond = await createHarness();
const pondReflection = await createHarness(reflection);
pond.init();
pondReflection.init();
assert.deepEqual(pond.audioCalls, [{ kind: 1, value: 2 }], 'the pond starts its soundtrack');
function tickPond(ms) {
    pond.tickFast(ms);
    pondReflection.tickFast(ms);
}

// Give the initial water tiles time to fill the canvas on the real host cadence.
for (let i = 0; i < 30; i++) tickPond(fastStepMs(i));
const waterColors = new Set();
for (let y = ORIGIN_Y; y < ORIGIN_Y + GRID; y += 7) {
    for (let x = ORIGIN_X; x < ORIGIN_X + GRID; x += 7) {
        const rgb = pond.canvas[y * WIDTH + x];
        assert.notEqual(rgb, 0, `missing pond background at ${x},${y}`);
        waterColors.add(rgb);
    }
}
assert(waterColors.size >= 8, 'the water is a textured image rather than the boot fill');
const stillPond = pond.frameHash();
tickPond(REVEAL_MS - 501);
assert.equal(pond.frameHash(), stillPond, 'the fully loaded pond stays fish-free before the reveal');
assert.equal(pond.sceneCalls(), 1, 'the backdrop uses the persistent canvas');

tickPond(1);
for (let i = 0; i < FAST_HZ / 2; i++) tickPond(fastStepMs(i));
const openWaterStart = pond.canvas.slice();
const reflectionStart = pondReflection.canvas.slice();
let koiPixels = 0;
for (let i = 0; i < openWaterStart.length; i++) {
    if (reflectionStart[i] !== 0 && openWaterStart[i] === reflectionStart[i]) koiPixels++;
}
assert(koiPixels > 100, 'the fish sprite is drawn over the water after the reveal');
for (let i = 0; i < FAST_HZ; i++) tickPond(fastStepMs(i));
let changedOpenWater = 0;
for (let y = ORIGIN_Y; y < ORIGIN_Y + GRID; y++) {
    for (let x = ORIGIN_X; x < ORIGIN_X + GRID; x++) {
        const i = y * WIDTH + x;
        if (reflectionStart[i] === 0 && pondReflection.canvas[i] === 0 &&
            pond.canvas[i] !== openWaterStart[i]) changedOpenWater++;
    }
}
assert(changedOpenWater > 8, 'the water light moves in areas clear of fish');
assert.equal(pond.sceneCalls(), 1, 'the water shimmers without clearing the scene');

// Verify frame publication with the extra water work and all three koi active.
for (let i = 0; i < FAST_HZ; i++) pond.tickFast(fastStepMs(i));
let pondPublished = 0, pondChanged = 0;
let previousPondImage = pond.frameHash();
for (let i = 0; i < sampleFrames; i++) {
    if (pond.tickFast(fastStepMs(i))) pondPublished++;
    const image = pond.frameHash();
    if (image !== previousPondImage) pondChanged++;
    previousPondImage = image;
}
assert(pondPublished >= sampleFrames - 2,
    `60 Hz pond must publish throughout the sample (${pondPublished}/${sampleFrames})`);
assert(pondChanged >= Math.floor(sampleFrames * 0.85),
    `60 Hz pond must advance the image (${pondChanged}/${sampleFrames})`);

pond.event(2, 24, 24);
assert.deepEqual(pond.audioCalls.at(-1), { kind: 1, value: 0 }, 'the left control mutes music');
assert.equal(pond.storage.get('music'), 0);
pond.event(2, 456, 24);
assert.equal(pond.storage.get('pond'), 0, 'the right control saves reflection mode');
assert(pond.black(), 'reflection mode starts with the original black canvas');
assert.equal(pond.audioCalls.at(-1).value, 0, 'switching the backdrop keeps the mute preference');

const restored = await createHarness({ initialStorage: Object.fromEntries(pond.storage) });
restored.init();
assert(restored.black(), 'a new instance restores reflection mode');
assert.deepEqual(restored.audioCalls, [{ kind: 1, value: 0 }], 'a new instance remains muted');
restored.event(2, 24, 24);
assert.deepEqual(restored.audioCalls.at(-1), { kind: 1, value: 2 }, 'music can be unmuted in reflection mode');
restored.event(2, 456, 24);
assert.equal(restored.storage.get('pond'), 1, 'the right control returns to pond mode');
restored.steps(12);
assert(!restored.black(), 'the pond backdrop returns on the legacy host');

const withoutAudio = await createHarness({ ...reflection, audioAvailable: false });
withoutAudio.init();
withoutAudio.tick(REVEAL_MS);
withoutAudio.steps(10);
assert(fishCoverage(withoutAudio.canvas).visible > 0, 'animation works when the speaker is unavailable');

// Optional headerless RGB24, 384 x 384 at 20 fps, in the aquarium's viewing orientation.
if (process.argv[2]) {
    const preview = await createHarness();
    preview.init();
    preview.steps(12);
    preview.tick(REVEAL_MS - 12 * FRAME_MS);
    const output = openSync(process.argv[2], 'w');
    const rgb = Buffer.allocUnsafe(GRID * GRID * 3);
    try {
        for (let frame = 0; frame < 250; frame++) {
            if (frame === 95) preview.event(2, touchX, touchY);
            let offset = 0;
            for (let row = 0; row < GRID; row++) {
                for (let col = 0; col < GRID; col++) {
                    const pixel = preview.canvas[(ORIGIN_Y + col) * WIDTH + ORIGIN_X + row];
                    rgb[offset++] = pixel >> 16;
                    rgb[offset++] = pixel >> 8;
                    rgb[offset++] = pixel;
                }
            }
            writeFileSync(output, rgb);
            preview.tick();
        }
    } finally {
        closeSync(output);
    }
}

console.log(`PASS legacy reveal, clock wrap, persistent canvas, swimming without trails, feeding, replay, 8 orientations and persistence (peak ${test.maxCommands()} scene commands and ${test.maxImages()} image calls per callback)`);
console.log(`PASS 60 Hz firmware protocol: ${publishedFrames}/${sampleFrames} frame groups published, ${changedFrames}/${sampleFrames} distinct consecutive images, repeated frame periods max ${longestRepeat}; peak ${fast.maxImages()} image calls per callback`);
console.log(`PASS pond background and moving light (${changedOpenWater} open-water pixels changed), 60 Hz pond ${pondPublished}/${sampleFrames} frame groups and ${pondChanged} distinct images, reflection toggle, soundtrack, mute persistence and unavailable audio`);
