# Code quality review — Sub-GHz/RF tab and shared controls

**Status:** BLOCK

**Recommendation:** REQUEST_CHANGES

**Review method:** Read-only source and history audit. Inspected the clean working-tree diff, the latest feature diff (`7918f7b^..7918f7b`), `room_sweep.c`, `room_sweep.h`, `application.fam`, and the relevant `MISSION.md`, `fix_plan.md`, and `ROOM_SWEEP_GUIDE.html` sections. No device, GUI, browser, network, build, or live-service action was used.

**Evidence limits:** Current `git diff` is empty; the reviewed RF/TX change is the latest committed feature diff. `tests/` contains only `test_nmea.c`; `rg` found no RF/RSSI/Sub-GHz/TX coverage there. The guide's deployment/build assertions were treated as untrusted documentation, not verification.

## What source verifies

1. **Frequency list and scan behavior**
   - RX uses a fixed 16-element array: 303.875, 315, 330, 345, 390, 418, 433.075, 433.420, 433.920, 434.420, 434.775, 420, 450, 868.350, 915, and 925 MHz ([room_sweep.h:22-39](../../room_sweep.h#L22-L39)).
   - The RF thread loops through those exact array entries, sets frequency/path, enters RX, samples, then idles ([room_sweep.c:209-220](../../room_sweep.c#L209-L220)). It repeats while the app runs ([room_sweep.c:196-209](../../room_sweep.c#L196-L209)).
   - There is no code path to tune an arbitrary RX frequency, scan intervals between presets, pause RX scanning, or select an RX preset. The settings menu's four presets control **TX** only ([room_sweep.c:90-96](../../room_sweep.c#L90-L96), [room_sweep.c:550-579](../../room_sweep.c#L550-L579), [room_sweep.c:741-759](../../room_sweep.c#L741-L759)). The guide corroborates the fixed RX list, but this is source-verified independently ([ROOM_SWEEP_GUIDE.html:736-743](../../ROOM_SWEEP_GUIDE.html#L736-L743)).

2. **RSSI sampling, threshold, and meter**
   - Each channel uses eight samples, delayed 5 ms each, and stores their arithmetic mean ([room_sweep.h:22-23](../../room_sweep.h#L22-L23), [room_sweep.c:213-225](../../room_sweep.c#L213-L225)). Excluding tuning/API overhead, one pass has at least 640 ms of sampling latency (16 x 8 x 5 ms); that timing is an inference from the code, not a measured runtime rate.
   - The alert threshold is -75 dBm ([room_sweep.h:41-42](../../room_sweep.h#L41-L42)). A mean strictly greater than it sets `rf_alert` and updates the full-pass peak ([room_sweep.c:227-236](../../room_sweep.c#L227-L236)).
   - The meter maps -100 to -30 dBm into a 42-pixel chart, clamps height, draws above-threshold channels solid, and draws weaker channels hollow/short ([room_sweep.c:346-380](../../room_sweep.c#L346-L380)). The dotted threshold line is drawn first at the same mapping ([room_sweep.c:352-358](../../room_sweep.c#L352-L358)).

3. **RF rendering and visible indication**
   - The RF screen shows a full-pass peak dBm value, 16 bars, only every fourth label (304, 390, 434, 450), and an inverse `SIGNAL!` overlay when any sampled channel exceeds threshold ([room_sweep.c:331-405](../../room_sweep.c#L331-L405)).
   - LED indication is source-verified: off below -85, green -85..-75, yellow -75..-65, red -65..-55, and alternating red/reset at or above -55 dBm ([room_sweep.c:238-258](../../room_sweep.c#L238-L258)). Whether the device LED visibly behaves this way is unverified without hardware.
   - A global RX/TX label is drawn after each tab: `RX` while `tx_active` is false and an inverse `TX` badge while it is true ([room_sweep.c:589-630](../../room_sweep.c#L589-L630)).

4. **Controls and TX safety behavior**
   - Left/right cycle tabs; Up/Down toggle stored sound/vibration flags; short Back opens settings, long Back exits ([room_sweep.c:762-805](../../room_sweep.c#L762-L805)).
   - On the RF tab, the first OK shows an authorization/frequency/three-second warning; another OK starts async OOK carrier TX, and Back/left/right cancel only the warning overlay ([room_sweep.c:638-665](../../room_sweep.c#L638-L665), [room_sweep.c:806-833](../../room_sweep.c#L806-L833)).
   - TX is bounded in code to 3,000 ms and always idles the radio afterward ([room_sweep.c:276-302](../../room_sweep.c#L276-L302)). The RF thread attempts to idle/yield whenever `tx_active` is set ([room_sweep.c:196-203](../../room_sweep.c#L196-L203)). This is an implementation intent; no hardware or concurrency test was available to prove mutual exclusion.

## Findings

### CRITICAL

None.

### HIGH

1. **The feature violates the mission's passive-RX-only safety constraint and leaves the user-visible contract dangerously wrong.** `MISSION.md` expressly limits Sub-GHz to passive RX and says no transmission ([MISSION.md:15-16](../../MISSION.md#L15-L16)). The code starts a continuous OOK carrier on a user-selected frequency ([room_sweep.c:268-302](../../room_sweep.c#L268-L302)) after a two-OK flow ([room_sweep.c:806-820](../../room_sweep.c#L806-L820)). The shipped Info tab says “Passive RX. No transmit.” ([room_sweep.c:524-528](../../room_sweep.c#L524-L528)); the guide says “never transmits” in its description and legal section ([ROOM_SWEEP_GUIDE.html:382-384](../../ROOM_SWEEP_GUIDE.html#L382-L384), [ROOM_SWEEP_GUIDE.html:775-776](../../ROOM_SWEEP_GUIDE.html#L775-L776)), and says RF OK does nothing ([ROOM_SWEEP_GUIDE.html:653-657](../../ROOM_SWEEP_GUIDE.html#L653-L657)). This is source-verified and blocks approval: either TX must be removed to honor the mission, or the authorized scope, legal/safety copy, Info tab, controls reference, and validated test plan must be explicitly changed before release.

2. **The planned safety gate is weaker and broader than specified.** The plan requires an explicit OK-*hold* confirmation and a single fixed frequency ([fix_plan.md:18-22](../../fix_plan.md#L18-L22)). The event loop accepts either `InputTypeShort` or `InputTypeLong` ([room_sweep.c:739-740](../../room_sweep.c#L739-L740)), and a second ordinary OK starts TX ([room_sweep.c:806-817](../../room_sweep.c#L806-L817)). Settings offer four selectable TX frequencies ([room_sweep.c:90-96](../../room_sweep.c#L90-L96), [room_sweep.c:754-756](../../room_sweep.c#L754-L756)). The bounded timeout and warning are worthwhile mitigations, but they do not meet the stated hold/fixed-frequency safety requirements.

### MEDIUM

1. **The “shared” Sound and Vibration controls are operationally misleading.** UI/menu actions toggle `sound_on`/`vibro_on` ([room_sweep.c:550-579](../../room_sweep.c#L550-L579), [room_sweep.c:796-804](../../room_sweep.c#L796-L804)), but feedback deliberately makes no sound or vibration ([room_sweep.c:305-323](../../room_sweep.c#L305-L323)). This misses the mission's requirement for visual, audio, and haptic indication and independently toggleable controls ([MISSION.md:10-11](../../MISSION.md#L10-L11), [MISSION.md:23-27](../../MISSION.md#L23-L27)); the open P0 acknowledges the defect ([fix_plan.md:5-9](../../fix_plan.md#L5-L9)).

2. **The common RX/TX status indicator obscures the rightmost RF bars.** The RF graph paints bars at `x = i * 8` for all 16 channels, placing bars 13–15 at x=104–127 ([room_sweep.c:346-381](../../room_sweep.c#L346-L381)). After that draw, the global RX text at x=106/y=24 or TX box x=106..125/y=16..25 is painted over the chart ([room_sweep.c:589-630](../../room_sweep.c#L589-L630)). This directly undermines the mission's “better visibility”/simple UI requirement ([MISSION.md:7-10](../../MISSION.md#L7-L10)).

3. **Cross-thread state used for the peak and alert rendering has no demonstrated synchronization.** Per-channel RSSI writes and reads use the mutex ([room_sweep.c:222-225](../../room_sweep.c#L222-L225), [room_sweep.c:360-382](../../room_sweep.c#L360-L382)), but `peak_rssi` and `rf_alert` are written outside it ([room_sweep.c:234-236](../../room_sweep.c#L234-L236)) and read by render/feedback without it ([room_sweep.c:337-338](../../room_sweep.c#L337-L338), [room_sweep.c:395-405](../../room_sweep.c#L395-L405), [room_sweep.c:315-322](../../room_sweep.c#L315-L322)). `volatile` alone does not establish a C synchronization contract. The risk of stale/torn UI state is inferred from this source pattern; it was not runtime-tested.

4. **The TX thread is allocated once but never reset/recreated after it returns.** First confirmation allocates only when `tx_thread == NULL`; later confirmations unconditionally call `furi_thread_start` on the same stored pointer ([room_sweep.c:814-817](../../room_sweep.c#L814-L817)). The thread returns after every transmit and only becomes NULL during app teardown ([room_sweep.c:280-302](../../room_sweep.c#L280-L302), [room_sweep.c:848-853](../../room_sweep.c#L848-L853)). The exact repeat-start behavior is framework-dependent and therefore unverified here, but the application code provides no completed-thread lifecycle handling or regression coverage; repeated TX is a material correctness risk.

5. **No test covers the RF behavior changed by the reviewed commit.** The latest feature commit changed 351 lines in `room_sweep.c`; the only discovered test file is `tests/test_nmea.c`, and repository search found no RF/RSSI/Sub-GHz/TX references. Thus none of the frequency hop sequence, threshold boundary, render geometry, two-step TX gate, maximum TX duration, or repeat-TX lifecycle has an observable automated regression lock. No test was run because none is relevant to this surface and the requested audit forbids device testing.

6. **The changed production unit is too broad for safe maintenance.** `room_sweep.c` is 598 nonblank/non-comment lines and owns UART/ISR, RF scan, TX, feedback, all tab rendering, settings, input handling, and lifecycle. The review's programming/remove-ai-slops perspective flags the recent 311-line expansion of this already-large single compilation unit as unnecessary coupling and a regression risk, especially around shared radio/UI state. This is a maintainability finding, not a claim that a specific runtime failure has occurred.

### LOW

1. **Threshold boundary behavior is inconsistent.** Detection, solid bars, and `SIGNAL!` use `> -75` ([room_sweep.c:227](../../room_sweep.c#L227), [room_sweep.c:370-380](../../room_sweep.c#L370-L380)), while LED yellow begins at `>= -75` ([room_sweep.c:252-255](../../room_sweep.c#L252-L255)). An exact -75 dBm mean can show yellow LED without a solid bar or banner. Whether exact equality occurs is runtime-dependent, but the semantic mismatch is source-verified.

2. **The RF chart cannot identify most individual presets at a glance.** Only four of sixteen bars are labelled ([room_sweep.c:387-392](../../room_sweep.c#L387-L392)). The guide documents this ([ROOM_SWEEP_GUIDE.html:569-574](../../ROOM_SWEEP_GUIDE.html#L569-L574)), so it is not a documentation defect; it remains a UX limitation for a detector meant to identify a signal's band.

3. **Two TX fields are dead state in the reviewed source.** `tx_warning_shown` and `tx_start_tick` are initialized/written ([room_sweep.c:695-701](../../room_sweep.c#L695-L701), [room_sweep.c:811-813](../../room_sweep.c#L811-L813)) but have no read. This is low-risk AI-slop/dead-code residue.

## Required skill-perspective check

**Ran:** Yes. I loaded and applied `omo:remove-ai-slops` and `omo:programming` before judging test relevance and maintainability. Their language-specific rules do not target C, so this review used their shared criteria rather than inventing non-applicable C tooling.

- **remove-ai-slops:** Violated by untested changed behavior, dead TX state, and the enlarged multi-responsibility production unit. No deletion-only, prompt-prose, tautological, or implementation-constant tests were found; the problem is missing RF/TX behavior coverage, not useless tests.
- **programming:** Violated by the oversized/multi-responsibility unit and a safety-critical input/worker lifecycle without a behavioral regression seam. No typed-language escape hatch is applicable to this C source; no needless parsing/normalization was introduced in this RF path.

## Assessment

The RF scan, RSSI average/threshold/meter, LED bands, two-step overlay, and three-second TX cap are implemented in source. They are not hardware-verified by this review. Approval is blocked by the explicit passive-RX contract violation and the weaker-than-planned TX safety gate; the misleading shared controls, chart occlusion, synchronization/lifecycle risks, and lack of relevant tests should be addressed in the same correction cycle.
