# 0.6.18 capture evidence

The supplied complete capture identifies itself as `LCS Android 0.6.17
source=e456abb5da79`. It contains 1,333 GE draws, 398 submitted GPU batches,
1,034 CPU-decoded texture images, and 783 GPU texture-image readbacks. Its
manifest reports `truncated: false`. Counts of texture images include individual
mip levels and repeated versions; they are not counts of unique texture objects.

## Reflection map receives no intended drawing

The game constructs a 64 × 64 RGB565 reflection map at address `0x04044000`.
Raw GE draw 1069 copies the scene into that map, followed by highlight draws
1070–1073. The captured GPU target allocation, framebuffer stride, GE region,
and scissor all agree with this 64 × 64 destination.

| Operation | Original GE destination X | Captured GPU destination X | Destination bounds |
| --- | ---: | ---: | ---: |
| Scene copy: draw 1069, batches 263–273 | 0–64 | 66.953857–114.215393 | 0–64 |
| Highlights: draws 1070–1073, batch 274 | −3–125 | 64.738472–159.261551 | 0–64 |

All these submitted primitives lie entirely to the right of the target. Even
the scene copy, which should cover the complete map, cannot produce a pixel.

The coordinates exactly match the Android through-mode ultrawide correction
in `render_ge_primitive`: `x = 256 + (x - 256) * 0.7384615`. It applies the
display's 512-pixel center to a small offscreen texture. The reflection passes
must keep their original coordinates; a display/HUD adjustment must not move
an offscreen texture-rendering pass.

The capture's 62 raw through-mode draws divide into 57 display draws (stride
512, RGBA8888, region 0–511 × 0–319) and these five reflection draws (stride
64, RGB565, region 0–63 × 0–63). Testing the destination layout separates the
offscreen passes from display overlays without relying on which backbuffer is
currently displayed.

There are 23 GPU batches that subsequently sample this reflection framebuffer.
All report a matching RGB565 source format and 64-pixel source stride, with an
actual 64 × 64 allocation. The prior allocation/crop mismatch is not present in
these consumers. A backend-only rectangle test does not reproduce this new
failure: the regression must exercise `render_ge_primitive`, where the unwanted
coordinate transformation occurs.

`gles_offscreen_hud_regression` now exercises that complete path with synthetic
scene-copy and T4 highlight textures, checking GPU pixels and submitted bounds.
It passes with the correction; the 0.6.17 baseline exits 31 with the same
66.9538–114.215 X bounds found in the capture. Controls retain display HUD
correction, double buffering, fullscreen coverage, narrow scissor behavior, and
the exclusion of depth-tested world effects from HUD correction.

## Color checks and limits

SHA-256 comparison of the image bytes finds that all 783 captured GPU texture
images match a captured CPU-decoded texture image exactly. The highlight texture
at `0x097a81f0` is grayscale, with RGB values 0–235 and alpha 255. The separate
T4 environment texture at `0x097e7480` is also grayscale, with RGB values 0–122
and alpha 255. None of the vertices in the 23 reflection-framebuffer consumer
batches has green greater than red; their colors are neutral or warm. This
capture provides no evidence for a general palette or texture-upload color
correction.

The saved final frame has predominantly gray vehicle reflections; the stronger
green artifacts are visible in the user's separate screenshots. This evidence
proves that the reflection-map generation passes are clipped incorrectly in the
captured frame. It does not by itself prove that every reported green flicker
has the same cause, or replace testing the corrected build on the phone.

## Missing world edges

All 1,271 captured 3D draws have projection entries `P00 = 1.4281311035` and
`P11 = 2.5388793945`, whose ratio is approximately 16:9. The physical surface is
1536 × 709, with aspect 2.16643. In 0.6.17, the guest camera/frustum hooks use
the internal render resolution, while a later GE correction widens submitted
geometry. Objects rejected by the narrower guest frustum cannot be recovered
by changing their vertices later.

The camera change uses the physical display aspect in the guest hooks and
separates the fallback geometry correction from HUD correction. BBOX tests,
early position rejection, and vertex output retain a consistent geometry
scale. The native surface-change handler already updates physical dimensions
before starting the native thread.

`android_camera_aspect_regression` exercises the actual camera configuration
and renderer with two edge triangles and a 512 × 320 viewport. It also checks
internal 1×/2× resolution and offscreen-target independence, 16:9, missing or
portrait surfaces, explicit aspect settings, and disabled hooks. The 0.6.17
baseline fails with exit 20: camera aspect 1.77778 instead of 2.16643. A wider,
correct guest frustum can submit additional edge objects; no FPS improvement
is inferred from this change.

No game textures, geometry, screenshots, or other raw captured game assets are
included with this evidence summary.
