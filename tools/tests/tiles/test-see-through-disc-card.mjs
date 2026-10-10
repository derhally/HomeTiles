// A screensaver tile with a low fill opacity (user 2026-10-10: Opacity 30 %)
// drew a black circle behind its icon: card_of() walked past a card below
// 50 % fill to the screen behind it. Every tile card is a button, and every
// disc sits directly on its card, so the walk stops at the card.
import assert from 'node:assert/strict';
import {readRepoFile} from '../../lib/admin-source.mjs';

const disc = readRepoFile('src/tiles/runtime/tile_icon_disc.h').replace(/\r\n/g, '\n');
assert.match(disc, /for \(int depth = 0; host && depth < 3 && !lv_obj_check_type\(host, &lv_button_class\) &&\n\s*lv_obj_get_style_bg_opa\(host, LV_PART_MAIN\) < LV_OPA_50; \+\+depth\) \{/);
// The circle of a see-through card is a veil in the card's own color.
assert.match(disc, /see_through_card = see_through\(host\);/);
// Tile renderers create their cards as buttons.
for (const file of ['sensor/renderer.cpp', 'switch/renderer.cpp', 'binary_sensor/renderer.cpp', 'energy/renderer.cpp']) {
  assert.match(readRepoFile(`src/types/${file}`), /= lv_button_create\(parent\);/, file);
}
console.log('Circles on see-through cards take the card color');
