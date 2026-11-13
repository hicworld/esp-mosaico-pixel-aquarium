import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { closeSync, openSync, readFileSync, writeSync } from 'node:fs';

const { instance } = await WebAssembly.instantiate(readFileSync(process.argv[2]));
const scene = instance.exports;
const side = 480;
const pixels = () => new Uint16Array(scene.memory.buffer, scene.pond_preview_pixels(), side * side);
const picture = () => createHash('sha256').update(new Uint8Array(pixels().buffer, pixels().byteOffset, pixels().byteLength)).digest('hex');
const frame = ms => assert(scene.pond_scene_frame(ms), 'canvas frame');
const touch = (down, x, y, ms) => scene.pond_scene_touch(down, x, y, ms);
const audio = expected => assert.equal(scene.pond_preview_audio(), expected, 'weather audio');

frame(0);
audio(0);
const filled = pixels();
for (let i = 0; i < side; ++i) {
    for (const index of [i, (side - 1) * side + i, i * side, i * side + side - 1]) {
        assert.notEqual(filled[index], 0, 'the pond covers the complete panel');
    }
}
for (let now = 17; now <= 5000; now += 17) frame(now);
const sunny = picture();
frame(5050);
assert.notEqual(sunny, picture(), 'sunny animation moves');

const volume = expected => assert.equal(scene.pond_audio_volume(), expected, 'volume setting');
const swipe = (x, y, dx, dy, ms) => {
    touch(1, x, y, ms);
    touch(1, x + dx, y + dy, ms + 90);
    touch(0, x + dx, y + dy, ms + 120);
};

touch(1, 232, 198, 5100);
touch(1, 232, 198, 7120);
frame(7120);
audio(0);
touch(0, 232, 198, 7160);
assert.equal(scene.pond_preview_pellets(1), 0, 'stationary long press does not feed');

volume(70);
touch(1, 286, 198, 7250);
touch(1, 214, 201, 7350);
frame(7350);
audio(0);
touch(0, 214, 201, 7400);
frame(7400);
audio(1);
const rainy = picture();
assert.notEqual(sunny, rainy, 'weather changes the canvas');
volume(70);

swipe(232, 220, 6, -72, 7500);
volume(80);
frame(7650);
audio(1);
assert.notEqual(rainy, picture(), 'rain animation moves');
swipe(232, 166, 3, 80, 7750);
volume(70);
swipe(180, 170, 64, 66, 8000);
volume(70);
frame(8150);
audio(1);
assert.equal(scene.pond_preview_pellets(1), 0, 'swipes do not feed');

touch(1, 232, 198, 8400);
touch(1, 286, 198, 8460);
touch(0, 232, 198, 8510);
assert.equal(scene.pond_preview_pellets(1), 0, 'a drag returning to its start does not feed');
swipe(210, 198, 76, 4, 8700);
frame(8840);
audio(0);
volume(70);

const feed = (x, y, now) => {
    const before = scene.pond_preview_pellets(1);
    touch(1, x, y, now);
    touch(0, x, y, now + 35);
    const added = scene.pond_preview_pellets(1) - before;
    assert(added >= 3 && added <= 6, `a tap scattered ${added} pellets`);
};
feed(226, 210, 13525);
feed(238, 172, 13570);
let maxFish = 0, meals = 0;
for (let i = 0; i < 750; i++) {
    if (i === 375) scene.pond_scene_request_weather(1);
    frame(13620 + i * 17);
    maxFish = Math.max(maxFish, scene.pond_scene_visible_fish());
    meals += scene.pond_preview_pellets(2);
    if (i % 19 === 0) assert(scene.pond_preview_damage_matches(), 'dirty redraw matches all nine tiles');
}
assert.equal(maxFish, 8, 'all eight fish can be visible');
assert(meals > 0, 'fish claim food as they reach it');
scene.pond_scene_request_weather(0);


if (process.argv[3]) {
    const output = openSync(process.argv[3], 'w');
    try {
        const rgb = Buffer.allocUnsafe(side * side * 3);
        for (let i = 0; i < 240; ++i) {
            const time = 26400 + 50 * i;
            if (i === 12 || i === 132) {
                touch(1, 240, 197, time - 30);
                touch(0, 240, 197, time);
            }
            if (i === 120) scene.pond_scene_request_weather(1);
            frame(time);
            const source = pixels();
            for (let p = 0, offset = 0; p < source.length; ++p) {
                const color = source[p];
                rgb[offset++] = ((color >> 11) * 255 + 15) / 31;
                rgb[offset++] = (((color >> 5) & 63) * 255 + 31) / 63;
                rgb[offset++] = ((color & 31) * 255 + 15) / 31;
            }
            writeSync(output, rgb);
        }
    } finally { closeSync(output); }
}
console.log('PASS full 480×480 panel, eight visible fish, two successive feeds, pellet claims, dirty redraw, swipes, weather and volume');
