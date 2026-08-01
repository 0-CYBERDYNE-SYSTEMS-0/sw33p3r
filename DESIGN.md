# Room Sweep audit surface design system

## 0. Research Log

- Existing reference: `ROOM_SWEEP_GUIDE.html` was inspected and its dark field-manual language, LCD amber, and sensing-path accents were retained for the new audit surface.
- External technical references: official Flipper Zero frequency documentation and the TI CC1101 datasheet are linked in the report for hardware-feasibility claims.
- Skipped lanes: lazyweb and image drafts — this is a self-contained engineering audit, not a brand or marketing surface; external imagery would reduce portability and evidence clarity.

## 1. Atmosphere & Identity

The audit is a quiet instrument panel: dense enough for engineering evidence, calm enough to expose uncertainty. Its signature is a split between signal-path color and evidence status: green, cyan, blue, and amber describe the sensing path, while explicit source/host/live badges describe how much of each claim is known.

## 2. Color

### Palette

| Role | Token | Value | Usage |
|---|---|---|---|
| Surface/primary | `--bg` | `#091215` | Page background |
| Surface/secondary | `--surface` | `#101d22` | Cards and panels |
| Surface/elevated | `--surface-raised` | `#16262c` | Focused cards and diagrams |
| Text/primary | `--text` | `#e7f0ed` | Headings and primary copy |
| Text/secondary | `--muted` | `#9ab0ab` | Supporting copy |
| Text/tertiary | `--dim` | `#7c9a94` | Metadata and inactive labels |
| Border/default | `--line` | `#22383f` | Panel outlines and dividers |
| Border/strong | `--line-strong` | `#34535c` | Active outlines and tables |
| RF accent | `--rf` | `#62d392` | Sub-GHz path and working states |
| Wi-Fi accent | `--wifi` | `#4fd5ce` | UART/Wi-Fi path |
| BLE accent | `--ble` | `#91baff` | BLE path |
| GPS accent | `--gps` | `#ffcf87` | GPS path and evidence |
| Warning | `--warn` | `#ffb45b` | Medium risk and inference |
| Error/block | `--alert` | `#ff7166` | Blockers and contradictions |
| Verified | `--verified` | `#8de3b0` | Host/source pass badges |

### Rules

- Functional accents are assigned by sensing path; they are not decorative gradients.
- Evidence status is always written as text in addition to color.
- Use tonal shifts and one-pixel outlines for depth. Avoid pure black and unlabelled color-only status.

## 3. Typography

### Scale

| Level | Size | Weight | Usage |
|---|---:|---:|---|
| Display | `clamp(2.5rem, 7vw, 5.5rem)` | 700 | Audit title |
| H1 | `2rem` | 700 | Major section headings |
| H2 | `1.25rem` | 700 | Card and mode headings |
| Body | `1rem` | 400 | Default copy |
| Body/sm | `.875rem` | 400 | Table and supporting copy |
| Caption | `.75rem` | 600 | Evidence labels and metadata |

### Font stack

- Primary: `system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif`
- Mono: `ui-monospace, SFMono-Regular, Menlo, Consolas, monospace`

Numbers, hashes, source locations, and protocol strings use the mono stack with tabular figures.

## 4. Spacing & Layout

### Base unit

All intentional spacing uses 4px increments: `--space-1: 4px`, `--space-2: 8px`, `--space-3: 12px`, `--space-4: 16px`, `--space-6: 24px`, `--space-8: 32px`, `--space-12: 48px`, and `--space-16: 64px`.

### Grid

- Maximum content width: 1240px.
- Desktop layout: 12-column equivalent using responsive CSS Grid with 24px gutters.
- Breakpoints: 720px and 1040px. At narrow widths, diagrams and tables become one readable column with horizontal overflow only for genuinely tabular data.
- Primary reading measure: 68ch.

## 5. Components

### Evidence badge

- **Structure**: `span.badge` with visible label and a semantic class modifier.
- **Variants**: source, host, live, inferred, blocked.
- **States**: static; no color-only meaning.
- **Accessibility**: text label remains present at all contrast modes.

### Mode navigator and panel

- **Structure**: `nav[role=tablist]` plus `section[role=tabpanel]`.
- **Variants**: RF, Wi-Fi, BLE, GPS, Info, Settings.
- **States**: selected, unselected, focus-visible, hidden.
- **Accessibility**: arrow-key navigation, `aria-selected`, `aria-controls`, and visible focus.
- **Motion**: opacity/transform only, 180ms; disabled under reduced motion.

### Meter

- **Structure**: label/value header, track, fill, threshold marker, and textual interpretation.
- **Variants**: RSSI, count, fix/satellite health, unavailable.
- **States**: populated, empty, stale, not implemented.
- **Accessibility**: value and state are written in text; fill is supplemental.

### Finding card

- **Structure**: severity badge, concise finding, evidence, and next action.
- **Variants**: high, medium, low, verified.
- **States**: visible, filtered, expanded evidence.
- **Accessibility**: filter controls use `aria-pressed`; source evidence uses native `details` disclosure.

### Architecture node

- **Structure**: named node, boundary label, responsibilities, and directional wire.
- **Variants**: Flipper, CC1101, UART, BFFB.
- **States**: static; wire animation is disabled for reduced motion.
- **Accessibility**: the diagram is followed by a text data-flow table.

## 6. Motion & Interaction

- Tab/filter transitions use 180ms `opacity` and `transform` only.
- The architecture trace animation communicates data movement and is decorative only when the text flow is unavailable; it stops under `prefers-reduced-motion`.
- Every button has hover, active, and focus-visible states.

## 7. Depth & Surface

The surface uses a mixed strategy: tonal shifts establish page/card hierarchy, while `--line` outlines preserve dense technical grouping. Shadows are reserved for the hero instrument and are tinted toward the page background.

## 8. Accessibility Constraints & Accepted Debt

### Constraints

- WCAG 2.2 AA target.
- Body contrast target 4.5:1; large text 3:1 minimum.
- Full keyboard reachability and visible focus indicators.
- Semantic landmarks, labelled tab panels, and reduced-motion support.
- No critical information is conveyed by color alone.

### Accepted Debt

| Item | Location | Why accepted | Exit |
|---|---|---|---|
| Browser/device visual QA is not a claim about the Flipper UI | Audit report status block | This artifact documents source and host evidence; the current session has no verified Flipper runtime surface | Run device-gated QA when the SDK and device are visible |
