# Learn ZacOS (the tutorial)

A help book, `share/zacos9/help/tutorial/`, shown by the Help Center
(`shell/help/`) like ZacOS Help. It has three paths through shared lessons:

| Path | For | `?for=` | Status |
|---|---|---|---|
| I used a Mac before | Mac OS 7–9 users | `mac` | written: `mac*.html`, four pages |
| New to computers | newcomers | `new` | to write |
| Puppy School | children | `kids` | to write |

`index.html` asks where to start. Until the other two paths exist, it sends
newcomers to Mouse Practice and then ZacOS Help's Desktop basics.

The Help menu (the Sniffer's, `shell/menubar/menus.c`) has **Learn ZacOS**
(`zacos9-help tutorial`) and **Mouse Practice** after ZacOS Help, where Mac
OS 9 had its tutorials. Both stay in Puppy Mode.

## Mouse Practice

`zacos9-mousepractice` (`shell/mousepractice/`) has four steps: point at the
bone (3 times), click it (5, smaller each time), double-click the doghouse
(3), and drag the ball to Zac (3). Each try that misses says why. After three
single clicks on the doghouse, it points to the Mouse control panel's
double-click speed. The two wordings are the tutorial's `new` and `kids`.
Puppy School (Practice ▸ Puppy School, on to begin with for a Puppy Mode
account or with `--kids`) also keeps the targets big.

`practice.cpp` is the game without a window. `main.cpp` feeds it the
pointer's events and draws its state, so `mouse-practice` (the test) plays
it through by hand.

## The lesson format

A lesson is written once. Wording that differs between paths is marked
with a class:

```html
<p>Every path sees this.</p>
<p class="for-new">The small square at the top left of a window is the close box. Click it once to close the window.</p>
<p class="for-kids">See the little square in the corner? Click it and the window goes away!</p>
<div class="for-new for-kids"> … a step Mac users skip … </div>
```

- An element whose class has `for-PATH` words is shown only on those paths.
  Untagged elements are shown on every path. Any element works, and nesting
  is fine.
- Close every tagged element. One left open loses only its tag, and its
  text then shows everywhere.
- The path is the page URL's `?for=` (`mac.html?for=mac`). The Help Center
  carries it to every local link the page makes, so lesson pages link
  plainly (`href="two.html"`). Only the entry links on `index.html`, and
  links that switch path, name it.
- A page opened without a path (from search, or a link without `?for=`)
  shows the `new` wording.

The filter is `tutorialHtml()` in `shell/help/helpbook.cpp`, applied in
`HelpWindow::Browser::loadResource`. Search indexes the page unfiltered, so
every path's words are found.

## Rules for the content

- Use ZacOS names (`docs/mac-os-9-gap-plan.md`, glossary). Mac OS names
  appear only to say what something used to be called, as the Mac path's
  name table does.
- Describe only what ships. Check claims against the code or ZacOS Help
  before writing them.
- Every page needs a `<title>`, a `<meta name="description">` and a link
  back to `index.html`, and must be linked from somewhere. The
  `help-center` test checks all of this, plus every link.

## Next steps (planned)

1. ~~Mouse practice~~ (done).
2. The `new` and `kids` paths, with "Show me" links that do things through
   `zacos9-finder-ctl` (a `zacos9-do:` link scheme with an allowlist).
3. Coach marks: a ring drawn around the real control (compositor overlay;
   rectangles from the menu bar and the Sniffer).
4. Setup Assistant's last page offers the tutorial; Puppy School's
   certificate.
