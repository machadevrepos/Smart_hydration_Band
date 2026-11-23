# Umbra, Image Asset Requests

For each asset: generate with the prompt below, save with the exact
filename, and drop it at the exact path. I'll wire each one into the app
once it lands, nothing here blocks current work, these are additive
requests, not dependencies I'm stuck waiting on.

**General direction for every prompt below:** no literal fitness-tracker
product shots, no stock-photo people, no medical/clinical imagery, no
visible faces. Everything stays dark-first with a single restrained gold
accent, consistent with the rest of the app. If a generation comes back
looking like a Fitbit ad or a hospital pamphlet, it's off-brand, regenerate
rather than force it in. When in doubt, less in the image is more.

**Onboarding illustration direction (v3, replaces the original pure-glow
backgrounds below):** the first pass (soft glows and bokeh, no imagery) read
as too empty given how much vertical canvas each onboarding page actually
has. The replacement direction is a small **vector line illustration** per
screen, in the same single-continuous-gold-line technique as the app icon's
crescent mark (#1 below), so the three onboarding assets and the icon read
as one family, not three unrelated pieces of concept art. The app targets
people who drink socially, so screen 2 is allowed to show a glass, unlike
the original brief, restraint still applies (one illustration, thin line
weight, no clutter) but "no drinks, no glasses" is no longer a constraint.

Every value below (hex, opacity, px) is pulled directly from the app's own
design tokens (`lib/core/constants/app_colors.dart`,
`lib/core/constants/app_theme.dart`), not invented for these prompts, so an
image generated from this spec should sit on the real screen without
looking like a separate, slightly-off asset next to real UI.

**Shared system, referenced by all three prompts below:**

| Token | Hex / value | Use in the illustration |
|---|---|---|
| `bgVoid` | `#000000` | Base canvas color, top of any gradient |
| `bgSurface` | `#0E0E10` | Gradient floor, never pure black everywhere, avoids a dead flat fill |
| `gold` | `#C9A227` | Primary line color, screens 1 and 2 |
| `goldMuted` | `#8C6D1F` | Secondary/background linework, distant elements |
| `champagne` | `#E8D9A0` | Primary line color, screen 3 only |
| `goldGlow` | `#C9A227` at 14% opacity | Any soft glow behind linework |
| `goldTint` | `#C9A227` at 12% opacity | Any fill inside a line shape, never higher than this |
| `textPrimary` | `#F5F1E8` (warm ivory) | The **only** acceptable "white," for a catchlight or highlight stroke. Pure `#FFFFFF` is never used anywhere in this app, it reads cold against true black, do not let the generator default to it |

**Canvas and safe zones, identical for all three:**
- 1284×2778px, portrait, PNG, fully opaque (no alpha channel needed)
- sRGB, no embedded color profile weirdness, flatten to 8-bit, no color banding in the gradient, dither if needed
- Side margins: keep all illustrated linework at least 90px in from the left and right canvas edges (matches the app's real 20pt/60px screen padding at this asset's ~3x scale, with headroom)
- **Text safe zone: the bottom 1050px of the 2778px canvas (the bottom ~38%) must stay within a few percent of pure black, `#000000`–`#0E0E10` only, no linework, no glow, no gradient brightening enters this band.** Headline and body copy render directly on top of it in the real app, exactly like the previous version, so contrast there is non-negotiable
- Fine film-grain texture over the entire canvas, uniform, subtle, roughly 3 to 5 percent grain opacity, not a smooth flat digital gradient, not banding

**Line technique, identical for all three, copied from the app icon spec so
the family actually matches:** one continuous, single-weight gold line, no
variable calligraphic tapering, rounded line caps and joins, stroke weight
a consistent 3 to 4px at this canvas resolution throughout. A soft outer
glow on the line using `goldGlow` (`#C9A227` at 14% opacity), gentle falloff,
no hard edge, no drop shadow, no reflections, no bevel. Any fill inside a
closed shape is `goldTint` (`#C9A227` at 12% opacity) at most, most of each
shape stays unfilled linework. Reference style: the restraint of an Aesop
or Cartier editorial line illustration, not a flat app icon glyph, not a
cartoon, not a sticker, not photorealistic, not 3D render.

---

## 1. App icon, needed now, every build needs a launcher icon

**Save as:** `app_icon.JPG.jpeg`
**Path:** `assets/icon/app_icon.JPG.jpeg`
**Size:** 1024×1024px, PNG, **no transparency** (flat background fills
the entire canvas, iOS rejects alpha-channel app icons), no rounded
corners baked in (the OS masks that automatically at each platform's
radius).

**Android-specific requirement:** Android's adaptive icon system can crop
up to roughly the outer 17% on every edge depending on the launcher's mask
shape (circle, squircle, rounded square all vary by device/OEM). Keep the
crescent mark itself inside the center ~66% of the canvas (roughly a
680×680px safe zone centered in the 1024×1024 frame) so it survives every
mask shape without getting clipped. The generous negative space the prompt
already asks for happens to line up with this well, just keep it in mind
if the result comes back mark-heavy.

**Prompt:**
> A minimal abstract app icon on a true flat black (#000000) square
> background, full-bleed, no border, no padding frame. At the center, a
> single elegant crescent shape formed from one continuous thin gold
> line (#C9A227), suggesting both a crescent moon and a drop of water in
> one fluid curved stroke, not a literal droplet, not a literal moon
> with craters, just one abstract curve that reads as both at once. The
> line has a soft, low-opacity outer glow. No text, no wordmark, no
> other shapes, no busy gradients beyond that one soft glow, no drop
> shadow, no reflections. Extremely minimal, luxury tech-brand feel,
> think the restraint of an Aesop or Oura brand mark, not a playful
> consumer app icon. Centered composition, roughly 55-60% of the canvas
> left as empty negative space around the mark, mark itself well within
> the center two-thirds of the frame.

---

## 2. Onboarding, Screen 1 illustration ("Umbra reminds you to drink.")

**Save as:** `onboarding_01_calm.png`
**Path:** `assets/images/onboarding_01_calm.png`
**Size:** 1284×2778px, PNG. Full spec below is self-contained, paste it as
one prompt.

**Prompt:**
> Vertical background image, 1284×2778px, portrait, PNG, fully opaque, sRGB,
> flatten to 8-bit, no color banding.
>
> **Canvas:** base color `#000000` (true black) at the very top and bottom
> edges of the frame, softening to `#0E0E10` in a wide, gentle radial
> gradient centered around the illustration itself (see below), so the
> canvas never reads as one flat dead fill. Fine, uniform film-grain
> texture across the whole image, roughly 3 to 5 percent grain opacity, not
> a smooth digital gradient.
>
> **Safe zones:** keep all linework at least 90px in from the left and
> right edges. The bottom 1050px of the frame (the bottom ~38 percent) must
> stay within a few percent of pure black, `#000000`–`#0E0E10` only, no
> linework, no glow, nothing brighter enters that band, real headline and
> body text render directly on top of it.
>
> **Subject, placed in the upper 60 percent of the frame, centered
> horizontally, vertical center around 32 percent down from the top edge:**
> one tall glass of water, drawn as a single continuous unbroken line in
> `#C9A227` gold, consistent stroke weight of 3 to 4px throughout, rounded
> line caps and joins, no variable calligraphic tapering. The glass outline
> suggests form with only 2 or 3 clean interior line breaks (a rim line, a
> base line), not photorealistic detail. One short highlight stroke in
> `#F5F1E8` (warm ivory, at roughly 40 percent opacity) along one edge of
> the glass to suggest light catching it, this is the only near-white
> element in the image and it must be this warm ivory, never pure
> `#FFFFFF`. Interior of the glass may carry a very faint fill in `#C9A227`
> at 12 percent opacity at most (matching the app's own `goldTint` token),
> most of the shape stays unfilled line.
>
> Around the glass, one or two thin concentric ring outlines in `#C9A227`
> at roughly 20 percent opacity, off-center so they crop at the edge of the
> illustration's bounding area, echoing a countdown ring, quiet and mostly
> invisible rather than a bullseye. Directly behind the glass, one soft
> circular glow using `#C9A227` at 14 percent opacity (matching the app's
> `goldGlow` token), gentle falloff, no hard edge, no drop shadow, no
> bevel, no reflections on a surface below the glass.
>
> **Exclusions:** no condensation droplets, no ice cubes, no straw, no
> hand or arm holding the glass, no other objects, no text, no wordmark, no
> logo, no people, no faces.
>
> **Style reference:** single-weight continuous-line vector illustration,
> the restraint of an Aesop or Cartier editorial line drawing. Not a flat
> app-icon glyph, not a cartoon, not a sticker, not photorealistic, not a
> 3D render.

---

## 3. Onboarding, Screen 2 illustration ("Built for the night out.")

**Save as:** `onboarding_02_night.png`
**Path:** `assets/images/onboarding_02_night.png`
**Size:** 1284×2778px, PNG. Full spec below is self-contained, paste it as
one prompt.

**Prompt:**
> Vertical background image, 1284×2778px, portrait, PNG, fully opaque, sRGB,
> flatten to 8-bit, no color banding.
>
> **Canvas:** base color `#000000` (true black), with a subtle darker warm
> undertone toward `#0E0E10` behind the illustration area only. Fine,
> uniform film-grain texture across the whole image, roughly 3 to 5 percent
> grain opacity, not a smooth digital gradient.
>
> **Safe zones:** keep all linework at least 90px in from the left and
> right edges. The bottom 1050px of the frame (the bottom ~38 percent) must
> stay within a few percent of pure black, `#000000`–`#0E0E10` only, no
> linework, no glow, no bokeh, nothing brighter enters that band, real
> headline and body text render directly on top of it.
>
> **Subject, placed in the upper 60 to 65 percent of the frame, centered
> horizontally, vertical center around 34 percent down from the top edge:**
> two glasses standing side by side, close together, same continuous-line
> gold technique as the rest of the set. On one side, a coupe or martini
> glass; on the other, a plain glass of water; both drawn in `#C9A227`,
> identical stroke weight of 3 to 4px, rounded caps and joins, neither
> glass rendered heavier or more detailed than the other, so the pairing
> reads as "still hydrating, out for the night," not a cocktail
> advertisement. Interior fill on either glass, if any, is `#C9A227` at 12
> percent opacity at most.
>
> Low behind the two glasses, a very minimal one-line city skyline
> silhouette, a handful of simple rectangular building shapes at differing
> heights, drawn in `#8C6D1F` (muted gold) at roughly 50 percent opacity,
> sitting on a thin horizon line no higher than 45 percent down the frame.
> Scattered above and around the skyline, 4 to 6 small soft out-of-focus
> bokeh circles alternating `#C9A227` and `#8C6D1F`, each at 10 to 18
> percent opacity, gentle falloff, no hard edges, like distant bar or
> string lights, sparse rather than filling the frame.
>
> **Exclusions:** no people, no faces, no hands, no bottles, no drinking
> straws, no text, no logos, no lens-flare stars, no visible light-bulb
> shapes.
>
> **Mood:** moody, cinematic, restrained, expensive-feeling, not festive,
> not busy, not a party flyer. Most of the frame still reads as dark
> negative space around the small illustrated scene.
>
> **Style reference:** single-weight continuous-line vector illustration,
> the restraint of an Aesop or Cartier editorial line drawing. Not a flat
> app-icon glyph, not a cartoon, not a sticker, not photorealistic, not a
> 3D render.

---

## 4. Onboarding, Screen 3 illustration ("Wake up better.")

**Save as:** `onboarding_03_morning.png`
**Path:** `assets/images/onboarding_03_morning.png`
**Size:** 1284×2778px, PNG. Full spec below is self-contained, paste it as
one prompt.

**Prompt:**
> Vertical background image, 1284×2778px, portrait, PNG, fully opaque, sRGB,
> flatten to 8-bit, no color banding.
>
> **Canvas:** base color `#000000` (true black) at the top of the frame,
> softening very gradually toward a dark warm `#0E0E10` in the illustration
> area, no other color shift. Fine, uniform film-grain texture across the
> whole image, roughly 3 to 5 percent grain opacity, not a smooth digital
> gradient.
>
> **Safe zones:** keep all linework at least 90px in from the left and
> right edges. The bottom 1050px of the frame (the bottom ~38 percent) must
> stay within a few percent of pure black, `#000000`–`#0E0E10` only, no
> linework, no glow, nothing brighter enters that band, real headline and
> body text render directly on top of it.
>
> **Subject, placed in the upper 60 percent of the frame, centered
> horizontally, vertical center around 38 percent down from the top edge:**
> a rising sun: one clean semicircular arc (just the top half of a sun
> disc, the bottom half hidden below the horizon) sitting directly on a
> single thin horizon line. Both the arc and the horizon line are drawn in
> `#E8D9A0` (champagne gold, not the primary `#C9A227` gold used elsewhere
> in the set), a single continuous line, consistent stroke weight of 3 to
> 4px, rounded caps and joins, same restrained technique as the other two
> screens. Around the arc, 5 to 7 short, straight, evenly-spaced line rays
> extending outward above the horizon, each ray a simple short stroke at
> the same 3 to 4px weight, not tapered, not a starburst, restrained rather
> than decorative, think the plainest possible sunrise glyph rendered by
> hand, not a children's-book sun and no face. Interior of the arc, if
> filled at all, is `#E8D9A0` at 12 percent opacity at most.
>
> Behind the arc and horizon line, a soft, low, horizontal glow band in
> `#E8D9A0` at roughly 14 percent opacity (matching the app's `goldGlow`
> token), centered on the horizon, gentle falloff top and bottom, no hard
> edge.
>
> **Exclusions:** no glass, no drinkware of any kind (screens 1 and 2
> already carry that motif, this screen is about waking up, not drinking),
> no full sun circle, only the arc above the horizon, no landscape detail,
> no silhouette figures, no birds, no clouds, no text, no logos, no lens
> flare, no smiley face on the sun.
>
> **Mood:** a calm exhale, quiet relief, recovery, the morning after
> feeling settled rather than energetic, but it must still read
> unambiguously as sunrise/dawn at a glance, since the screen's whole point
> is "waking up." Restrained, not a travel-poster sunrise, not triumphant,
> not saturated or bright, just enough to be legible as dawn.
>
> **Style reference:** single-weight continuous-line vector illustration,
> the restraint of an Aesop or Cartier editorial line drawing. Not a flat
> app-icon glyph, not a cartoon, not a sticker, not photorealistic, not a
> 3D render.

---

## 5. Optional, splash lockup mark (only if you want to go further than the typographic splash already shipped)

**Not requested yet.** The current splash screen uses a typographic
"UMBRA" wordmark treatment and doesn't need an image to look finished. If
you generate asset #1 (the app icon) and like the crescent mark, tell me
and I'll ask for a transparent-background version of the same mark at
higher resolution to composite into the splash animation instead of pure
type, but that's a nice-to-have refinement, not a blocker.
