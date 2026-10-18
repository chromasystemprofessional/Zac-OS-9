#!/usr/bin/env python3
"""Extension Icons: pick the icon each startup extension shows.

A developer tool, never built or installed. It lists every extension in
lib/extensions.c (the startup parade, the boot splash and Extensions
Manager all draw from it) with its icon, and changes an extension's icon to
another built-in one, to an imported PNG, or to pixel art drawn here.

Changes go into the source: lib/extensions.c names the icon, and a new
picture becomes "ext-custom-KEY" in assets/icons/platinum-icons.picon
(tools/icons/build_icons.py, which this runs, compiles it into
lib/icons_data.h and lib/icons_custom.h). Rebuild to see it at startup.

usage:
  extension_icons.py                   the window
  extension_icons.py --list            every extension and its icon
  extension_icons.py --set KEY KIND    a built-in icon (e.g. ext-wifi)
  extension_icons.py --import KEY PNG  a picture, scaled to the palette
  extension_icons.py --revert KEY      the icon in the last commit
"""
import os
import re
import subprocess
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
sys.dont_write_bytecode = True  # no __pycache__ in tools/icons
sys.path.insert(0, os.path.join(ROOT, "tools", "icons"))
import build_icons  # noqa: E402

EXT_C = os.path.join(ROOT, "lib", "extensions.c")
PICON = build_icons.SRC
PARADE = os.environ.get("ZACOS9_PARADE_FILE", "/run/zacos9/parade")
CUSTOM = build_icons.CUSTOM_PREFIX
CUSTOM_SECTION = "; Extensions' own icons (tools/extension-icons/extension_icons.py)."
DESKTOP = (0x66, 0x66, 0xCC)  # the default desktop color, as build_icons' specimen

# { "key", "Name", PL_ICON_KIND,
ENTRY = re.compile(r'(\{\s*"([^"]+)",\s*"([^"]+)",\s*)(PL_ICON_\w+)(\s*,)')


def kind_of(enum):
    """PL_ICON_EXT_WIFI -> ext-wifi."""
    return enum[len("PL_ICON_"):].lower().replace("_", "-")


def custom_kind(key):
    return CUSTOM + key


class Repo:
    """The extensions, the icon set, and changes to both."""

    def __init__(self):
        self.load()

    def load(self):
        self.palette, self.icons = build_icons.parse()
        self.kinds = build_icons.all_kinds(self.icons)
        with open(EXT_C, encoding="utf-8") as f:
            self.extensions = [{"key": m.group(2), "name": m.group(3), "kind": kind_of(m.group(4))}
                               for m in ENTRY.finditer(f.read())]
        self.committed = {}
        try:
            head = subprocess.run(["git", "-C", ROOT, "show", "HEAD:lib/extensions.c"],
                                  capture_output=True, text=True, check=True).stdout
            self.committed = {m.group(2): kind_of(m.group(4)) for m in ENTRY.finditer(head)}
        except (OSError, subprocess.CalledProcessError):
            pass
        self.parade = []
        try:
            with open(PARADE, encoding="utf-8") as f:
                self.parade = [line.strip() for line in f if line.strip()]
        except OSError:
            pass

    def extension(self, key):
        for e in self.extensions:
            if e["key"] == key:
                return e
        raise KeyError(f"no extension {key!r} in lib/extensions.c")

    def rows(self, kind, size):
        return self.icons[(kind, size)]

    # ---- changes ------------------------------------------------------------

    def set_builtin(self, key, kind):
        if kind not in self.kinds:
            raise KeyError(f"no icon {kind!r}")
        self._apply(key, kind, None)

    def set_custom(self, key, rows32, rows16):
        for rows, size in ((rows32, 32), (rows16, 16)):
            if len(rows) != size or any(len(r) != size for r in rows):
                raise ValueError(f"a {size} px icon needs {size} rows of {size}")
            if any(c not in self.palette for r in rows for c in r):
                raise ValueError("pixels outside the icon palette")
        self._apply(key, custom_kind(key), (rows32, rows16))

    def revert(self, key):
        kind = self.committed.get(key)
        if not kind:
            raise KeyError(f"{key!r} isn't in the last commit")
        if kind.startswith(CUSTOM):
            # The committed picture is in the committed .picon.
            head = subprocess.run(["git", "-C", ROOT, "show",
                                   "HEAD:assets/icons/platinum-icons.picon"],
                                  capture_output=True, text=True, check=True).stdout
            blocks = parse_blocks(head.splitlines())
            self._apply(key, kind, (blocks[(kind, 32)], blocks[(kind, 16)]))
        else:
            self._apply(key, kind, None)

    def _apply(self, key, kind, art):
        """Point KEY at KIND, store ART as KIND if given, drop custom icons
        nothing uses any more, and regenerate; all or nothing."""
        self.extension(key)
        with open(EXT_C, encoding="utf-8") as f:
            ext_before = f.read()
        with open(PICON, encoding="utf-8") as f:
            picon_before = f.read()

        enum = build_icons.enum_name(kind)
        ext_after = ENTRY.sub(lambda m: m.group(1) + enum + m.group(5) if m.group(2) == key
                              else m.group(0), ext_before)
        used = {kind_of(m.group(4)) for m in ENTRY.finditer(ext_after)}
        lines = picon_before.splitlines()
        if art:
            lines = remove_icon(lines, kind)
            lines = add_icon(lines, kind, art)
        for name in {n for n, _ in parse_blocks(lines)}:
            if name.startswith(CUSTOM) and name not in used:
                lines = remove_icon(lines, name)
        picon_after = "\n".join(lines) + "\n"

        try:
            write(PICON, picon_after)
            write(EXT_C, ext_after)
            regenerate()
        except Exception:
            write(PICON, picon_before)
            write(EXT_C, ext_before)
            regenerate()
            raise
        finally:
            self.load()


def write(path, text):
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)


def regenerate():
    r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "icons", "build_icons.py")],
                       capture_output=True, text=True)
    if r.returncode:
        raise RuntimeError("build_icons.py failed:\n" + (r.stderr or r.stdout))


def parse_blocks(lines):
    """(name, size) -> rows, for every icon in .picon lines."""
    blocks, i = {}, 0
    while i < len(lines):
        w = lines[i].split()
        if len(w) == 3 and w[0] == "icon":
            size = int(w[2])
            blocks[(w[1], size)] = lines[i + 1:i + 1 + size]
            i += 1 + size
        else:
            i += 1
    return blocks


def remove_icon(lines, name):
    """The lines without icon NAME (both sizes, and a blank line after each)."""
    out, i = [], 0
    while i < len(lines):
        w = lines[i].split()
        if len(w) == 3 and w[0] == "icon" and w[1] == name:
            i += 1 + int(w[2])
            if i < len(lines) and not lines[i].strip():
                i += 1
            continue
        out.append(lines[i])
        i += 1
    while out and not out[-1].strip():
        out.pop()
    if out and out[-1] == CUSTOM_SECTION:
        out.pop()  # the section's last icon went
        while out and not out[-1].strip():
            out.pop()
    return out


def add_icon(lines, name, art):
    out = list(lines)
    while out and not out[-1].strip():
        out.pop()
    if CUSTOM_SECTION not in out:
        out += ["", CUSTOM_SECTION]
    for rows, size in zip(art, (32, 16)):
        out += ["", f"icon {name} {size}"] + list(rows)
    return out


# ---- pictures -----------------------------------------------------------------

def rgb(v):
    return ((v >> 16) & 255, (v >> 8) & 255, v & 255)


def image_from_rows(rows, palette, bg=None, scale=1):
    """A PIL image of icon rows: transparent, or on BG."""
    from PIL import Image
    size = len(rows)
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0) if bg is None else bg + (255,))
    px = img.load()
    for y, r in enumerate(rows):
        for x, c in enumerate(r):
            v = palette.get(c)
            if v is not None:
                px[x, y] = rgb(v) + (255,)
    return img.resize((size * scale, size * scale), Image.NEAREST) if scale != 1 else img


def rows_from_image(img, size, palette):
    """Fit IMG into SIZE x SIZE (centred, proportions kept) and map every
    pixel to the nearest palette color; under half opaque is transparent."""
    from PIL import Image
    img = img.convert("RGBA")
    if img.size != (size, size):
        scale = size / max(img.size)
        w, h = max(1, round(img.width * scale)), max(1, round(img.height * scale))
        filt = Image.NEAREST if scale >= 1 and scale == int(scale) else Image.LANCZOS
        fitted = img.resize((w, h), filt)
        img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
        img.paste(fitted, ((size - w) // 2, (size - h) // 2))
    # Named black and white first: the set's outlines use K and W.
    colors = sorted(((c, rgb(v)) for c, v in palette.items() if v is not None),
                    key=lambda cv: cv[0] not in "KW")
    px = img.load()
    rows = []
    for y in range(size):
        row = ""
        for x in range(size):
            r, g, b, a = px[x, y]
            if a < 128:
                row += "."
                continue
            row += min(colors, key=lambda cv: (cv[1][0] - r) ** 2 + (cv[1][1] - g) ** 2
                       + (cv[1][2] - b) ** 2)[0]
        rows.append(row)
    return rows


# ---- command line ---------------------------------------------------------------

def cli(argv):
    repo = Repo()
    if argv[0] == "--list":
        order = {k: i + 1 for i, k in enumerate(repo.parade)}
        for e in repo.extensions:
            n = order.get(e["key"])
            mark = "" if repo.committed.get(e["key"]) in (None, e["kind"]) else "  (changed)"
            print(f"{e['key']:24} {e['kind']:28} {'#%d' % n if n else '-':>4}  {e['name']}{mark}")
    elif argv[0] == "--set" and len(argv) == 3:
        repo.set_builtin(argv[1], argv[2])
    elif argv[0] == "--import" and len(argv) == 3:
        from PIL import Image
        img = Image.open(argv[2])
        repo.set_custom(argv[1], rows_from_image(img, 32, repo.palette),
                        rows_from_image(img, 16, repo.palette))
    elif argv[0] == "--revert" and len(argv) == 2:
        repo.revert(argv[1])
    else:
        sys.exit(__doc__)


# ---- the window -------------------------------------------------------------------

def gui():
    import tkinter as tk
    from tkinter import filedialog, messagebox, ttk
    from PIL import Image, ImageTk

    repo = Repo()
    root = tk.Tk()
    root.title("Extension Icons (ZacOS 9 developer tool)")
    style = ttk.Style(root)
    style.configure("Treeview", rowheight=36)

    photos = {}  # keep PhotoImages alive

    def photo(tag, rows, bg=None, scale=1):
        img = ImageTk.PhotoImage(image_from_rows(rows, repo.palette, bg, scale))
        photos[tag] = img
        return img

    # The working icon: the picker's choice, before Apply.
    work = {"key": None, "builtin": None, "rows": {32: None, 16: None}}

    # ---- the list | the picker, split ----
    split = ttk.PanedWindow(root, orient="horizontal")
    split.pack(fill="both", expand=True)
    left = ttk.Frame(split, padding=6)
    split.add(left, weight=1)
    tree = ttk.Treeview(left, columns=("name", "key", "icon", "parade"), height=10)
    for col, text, width in (("#0", "", 48), ("name", "Extension", 170), ("key", "Key", 120),
                             ("icon", "Icon", 140), ("parade", "Boot", 44)):
        tree.heading(col, text=text)
        tree.column(col, width=width, stretch=col == "name")
    scroll = ttk.Scrollbar(left, orient="vertical", command=tree.yview)
    tree.configure(yscrollcommand=scroll.set)
    tree.grid(row=0, column=0, sticky="nsew")
    scroll.grid(row=0, column=1, sticky="ns")
    left.rowconfigure(0, weight=1)
    left.columnconfigure(0, weight=1)

    parade_label = ttk.Label(left, text="This boot's parade (lib/extensions.c icons, in load order):")
    parade_label.grid(row=1, column=0, columnspan=2, sticky="w", pady=(8, 2))
    parade = tk.Canvas(left, height=44, background="#%02x%02x%02x" % DESKTOP,
                       highlightthickness=0)
    parade.grid(row=2, column=0, columnspan=2, sticky="ew")
    parade_scroll = ttk.Scrollbar(left, orient="horizontal", command=parade.xview)
    parade.configure(xscrollcommand=parade_scroll.set)
    parade_scroll.grid(row=3, column=0, columnspan=2, sticky="ew")

    def fill_list():
        selected = tree.selection()
        tree.delete(*tree.get_children())
        order = {k: i + 1 for i, k in enumerate(repo.parade)}
        for e in repo.extensions:
            changed = repo.committed.get(e["key"]) not in (None, e["kind"])
            n = order.get(e["key"])
            tree.insert("", "end", iid=e["key"],
                        image=photo("list-" + e["key"], repo.rows(e["kind"], 32), (255, 255, 255)),
                        values=(e["name"], e["key"], e["kind"] + ("  *" if changed else ""),
                                f"#{n}" if n else "—"))
        if selected and tree.exists(selected[0]):
            tree.selection_set(selected[0])
            tree.see(selected[0])
        parade.delete("all")
        kinds = {e["key"]: e["kind"] for e in repo.extensions}
        keys = [k for k in repo.parade if k in kinds] or [e["key"] for e in repo.extensions]
        if not repo.parade:
            parade_label.configure(text=f"No parade record at {PARADE}: every extension, "
                                        "in catalog order:")
        for i, k in enumerate(keys):
            parade.create_image(6 + i * 36, 6, anchor="nw",
                                image=photo("parade-" + k, repo.rows(kinds[k], 32)))
        parade.configure(scrollregion=(0, 0, 12 + len(keys) * 36, 44))

    # ---- the picker ----
    right = ttk.Frame(split, padding=6)
    split.add(right, weight=0)
    title = ttk.Label(right, text="Choose an extension", font=("TkDefaultFont", 11, "bold"))
    title.grid(row=0, column=0, sticky="w")
    subtitle = ttk.Label(right, text="")
    subtitle.grid(row=1, column=0, sticky="w")

    preview = tk.Canvas(right, width=330, height=108, highlightthickness=0, background="#dddddd")
    preview.grid(row=2, column=0, sticky="w", pady=6)

    tabs = ttk.Notebook(right)
    tabs.grid(row=3, column=0, sticky="nsew")
    right.rowconfigure(3, weight=1)

    # Built-in icons.
    builtin = ttk.Frame(tabs, padding=6)
    tabs.add(builtin, text="Built-in")
    builtin_name = ttk.Label(builtin, text="")
    per_row = 8
    for i, kind in enumerate(repo.kinds):
        b = tk.Button(builtin, image=photo("pick-" + kind, repo.rows(kind, 32), (255, 255, 255)),
                      relief="flat", command=lambda k=kind: choose_builtin(k))
        b.grid(row=i // per_row, column=i % per_row, padx=2, pady=2)
        b.bind("<Enter>", lambda _e, k=kind: builtin_name.configure(text=k))
    builtin_name.grid(row=len(repo.kinds) // per_row + 1, column=0, columnspan=per_row,
                      sticky="w", pady=(6, 0))

    # Import a PNG.
    importer = ttk.Frame(tabs, padding=6)
    tabs.add(importer, text="Import PNG")
    ttk.Label(importer, wraplength=320, justify="left",
              text="Any picture: it is fitted into 32 x 32 and 16 x 16, proportions kept, "
                   "and every pixel becomes the nearest icon palette color (under half "
                   "opaque: transparent). Touch it up in the Pixel Editor after.").grid(
        row=0, column=0, sticky="w")
    import_status = ttk.Label(importer, text="")
    import_status.grid(row=2, column=0, sticky="w", pady=6)

    def import_png():
        path = filedialog.askopenfilename(
            title="Import an icon picture",
            filetypes=[("Pictures", "*.png *.gif *.bmp *.jpg *.jpeg *.webp"), ("All files", "*")])
        if not path:
            return
        try:
            img = Image.open(path)
            set_rows(rows_from_image(img, 32, repo.palette), rows_from_image(img, 16, repo.palette))
        except Exception as ex:  # noqa: BLE001 - any unreadable picture
            messagebox.showerror("Import PNG", f"Couldn't read {path}:\n{ex}")
            return
        import_status.configure(text=f"Imported {os.path.basename(path)} "
                                     f"({img.width} x {img.height})")
    ttk.Button(importer, text="Choose a Picture…", command=import_png).grid(
        row=1, column=0, sticky="w", pady=(8, 0))

    # Pixel editor.
    editor = ttk.Frame(tabs, padding=6)
    tabs.add(editor, text="Pixel Editor")
    size_var = tk.IntVar(value=32)
    zoom = {32: 11, 16: 22}
    grid = tk.Canvas(editor, width=32 * 11, height=32 * 11, highlightthickness=1,
                     highlightbackground="#888888", background="white")
    grid.grid(row=0, column=0, rowspan=4, sticky="nw")
    paint = {"char": "K"}

    swatches = ttk.Frame(editor)
    swatches.grid(row=0, column=1, sticky="nw", padx=(8, 0))
    chars = [c for c in repo.palette]
    current_swatch = ttk.Label(swatches, text="")

    def pick_char(c):
        paint["char"] = c
        current_swatch.configure(text="Painting: " + ("transparent" if c == "." else
                                 f"'{c}' #{repo.palette[c]:06X}"))
    for i, c in enumerate(chars):
        v = repo.palette[c]
        color = "#%06x" % v if v is not None else "white"
        sw = tk.Label(swatches, width=2, background=color, relief="raised",
                      text="×" if v is None else "", borderwidth=1)
        sw.grid(row=i // 6, column=i % 6, padx=1, pady=1)
        sw.bind("<Button-1>", lambda _e, ch=c: pick_char(ch))
    current_swatch.grid(row=len(chars) // 6 + 1, column=0, columnspan=6, sticky="w", pady=4)
    pick_char("K")

    sizes = ttk.Frame(editor)
    sizes.grid(row=1, column=1, sticky="nw", padx=(8, 0), pady=6)
    for s in (32, 16):
        ttk.Radiobutton(sizes, text=f"{s} x {s}", value=s, variable=size_var,
                        command=lambda: draw_grid()).pack(anchor="w")

    def shrink():
        if work["rows"][32]:
            img = image_from_rows(work["rows"][32], repo.palette)
            set_rows(work["rows"][32], rows_from_image(img, 16, repo.palette))
    def clear():
        s = size_var.get()
        rows = dict(work["rows"])
        rows[s] = ["." * s] * s
        set_rows(rows[32], rows[16])
    tools_frame = ttk.Frame(editor)
    tools_frame.grid(row=2, column=1, sticky="nw", padx=(8, 0))
    ttk.Button(tools_frame, text="Make 16 from 32", command=shrink).pack(anchor="w", fill="x")
    ttk.Button(tools_frame, text="Clear", command=clear).pack(anchor="w", fill="x", pady=(4, 0))
    ttk.Label(editor, wraplength=150, justify="left",
              text="Left button paints, right button erases.").grid(
        row=3, column=1, sticky="nw", padx=(8, 0), pady=6)

    def draw_grid():
        grid.delete("all")
        s = size_var.get()
        z = zoom[s]
        rows = work["rows"][s]
        if not rows:
            return
        for y, r in enumerate(rows):
            for x, c in enumerate(r):
                v = repo.palette.get(c)
                if v is None:
                    fill = "#ffffff" if (x + y) % 2 else "#eeeeee"
                else:
                    fill = "#%06x" % v
                grid.create_rectangle(x * z, y * z, x * z + z, y * z + z, fill=fill,
                                      outline="#dddddd" if v is None else fill)

    def paint_at(event, char):
        s = size_var.get()
        if not work["rows"][s]:
            return
        z = zoom[s]
        x, y = event.x // z, event.y // z
        if not (0 <= x < s and 0 <= y < s):
            return
        row = work["rows"][s][y]
        if row[x] == char:
            return
        rows = {k: list(v) for k, v in work["rows"].items()}
        rows[s][y] = row[:x] + char + row[x + 1:]
        set_rows(rows[32], rows[16])
    grid.bind("<Button-1>", lambda e: paint_at(e, paint["char"]))
    grid.bind("<B1-Motion>", lambda e: paint_at(e, paint["char"]))
    grid.bind("<Button-3>", lambda e: paint_at(e, "."))
    grid.bind("<B3-Motion>", lambda e: paint_at(e, "."))

    # ---- the working icon ----
    def show_work():
        preview.delete("all")
        r32, r16 = work["rows"][32], work["rows"][16]
        if not r32:
            return
        x = 6
        for bg in ((255, 255, 255), (0xDD, 0xDD, 0xDD), DESKTOP):
            preview.create_image(x, 6, anchor="nw", image=photo(f"pv-{bg}", r32, bg, 1))
            preview.create_image(x, 44, anchor="nw", image=photo(f"pv16-{bg}", r16, bg, 1))
            x += 40
        preview.create_image(x + 4, 6, anchor="nw", image=photo("pv-big", r32, (255, 255, 255), 3))
        preview.create_image(x + 108, 6, anchor="nw",
                             image=photo("pv16-big", r16, (255, 255, 255), 3))
        what = f"built-in {work['builtin']}" if work["builtin"] else "new picture"
        state.configure(text=f"Choice: {what}  (not applied yet)" if dirty() else "")

    def set_rows(r32, r16, builtin=None):
        work["rows"] = {32: list(r32), 16: list(r16)}
        work["builtin"] = builtin
        draw_grid()
        show_work()

    def choose_builtin(kind):
        if work["key"]:
            set_rows(repo.rows(kind, 32), repo.rows(kind, 16), kind)

    def dirty():
        if not work["key"]:
            return False
        e = repo.extension(work["key"])
        if work["builtin"]:
            return work["builtin"] != e["kind"]
        return (work["rows"][32] != repo.rows(e["kind"], 32) or
                work["rows"][16] != repo.rows(e["kind"], 16))

    def select(_event=None):
        sel = tree.selection()
        if not sel:
            return
        e = repo.extension(sel[0])
        work["key"] = e["key"]
        committed = repo.committed.get(e["key"])
        title.configure(text=e["name"])
        subtitle.configure(text=f"{e['key']}   icon: {e['kind']}" +
                           (f"   (last commit: {committed})" if committed and committed != e["kind"]
                            else ""))
        set_rows(repo.rows(e["kind"], 32), repo.rows(e["kind"], 16), e["kind"])
    tree.bind("<<TreeviewSelect>>", select)

    # ---- apply ----
    actions = ttk.Frame(right)
    actions.grid(row=4, column=0, sticky="ew", pady=(8, 0))
    state = ttk.Label(actions, text="")
    state.pack(side="left")

    def run(change):
        try:
            change()
        except Exception as ex:  # noqa: BLE001 - shown, and the files are put back
            messagebox.showerror("Extension Icons", str(ex))
            return
        fill_list()
        select()
        state.configure(text="Written to the source. Rebuild (scripts/build.sh) to see it at startup.")

    def apply():
        key = work["key"]
        if not key or not dirty():
            return
        if work["builtin"]:
            run(lambda: repo.set_builtin(key, work["builtin"]))
        else:
            run(lambda: repo.set_custom(key, work["rows"][32], work["rows"][16]))

    def revert():
        key = work["key"]
        if key and messagebox.askyesno("Revert", f"Put {key} back to its icon in the last commit?"):
            run(lambda: repo.revert(key))

    ttk.Button(actions, text="Apply to Source", command=apply).pack(side="right")
    ttk.Button(actions, text="Revert to Last Commit", command=revert).pack(side="right", padx=6)

    root.minsize(720, 480)
    fill_list()
    if repo.extensions:
        first = repo.parade[0] if repo.parade and tree.exists(repo.parade[0]) else \
            repo.extensions[0]["key"]
        tree.selection_set(first)
    root.mainloop()


if __name__ == "__main__":
    if len(sys.argv) > 1:
        cli(sys.argv[1:])
    else:
        gui()
