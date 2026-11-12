//! `zacos9-afterdark`: the headless side of the After Dark screen saver.
//!
//! Two jobs, chosen by the command line:
//!
//! * `--import PATH` — read a user-supplied After Dark disk image, a folder of
//!   `.rsrc` files, or a single module, and put the extracted library in
//!   `$XDG_DATA_HOME/after-dark/modules` (the same place the Rust player uses).
//! * `--module TITLE` — run one module and write its frames to stdout for
//!   `zacos9-screensaver` and the Screen Saver panel's preview.
//!
//! `--list` prints the playable module titles, one per line. `--library-dir`
//! prints where they live. No original modules or fonts are shipped: the user
//! supplies the disk, and the import copies from it.
//!
//! ## The frame stream
//!
//! Each frame is one little-endian record on stdout:
//!
//! ```text
//! u32 magic (0x31464441, "ADF1")
//! u16 width
//! u16 height
//! u32 emulated ticks
//! u8  palette[256][3]
//! u8  pixels[width * height]   (palette indices, row-major)
//! ```
//!
//! stdout is reserved for frames while a module runs; diagnostics go to stderr.
//! The parent (`shell/screensaver/afterdark.c`) reads the records without
//! blocking its Wayland loop and scales them into the saver's overlay.

mod library;

use ad_host_v2::{Host, NO_CYCLE_LIMIT};
use ad_resource::{GmMessage, GmResult, ModuleSettings, ResourceFork};
use std::io::Write;
use std::path::{Path, PathBuf};
use std::time::{Duration, Instant};

const MAGIC: u32 = 0x3146_4441; // "ADF1" read little-endian.

fn usage() -> ! {
    eprintln!(
        "usage: zacos9-afterdark --module TITLE [--fps N]\n\
         \x20      zacos9-afterdark --import PATH\n\
         \x20      zacos9-afterdark --list\n\
         \x20      zacos9-afterdark --library-dir"
    );
    std::process::exit(2);
}

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let mut module = None;
    let mut import = None;
    let mut list = false;
    let mut library = false;
    let mut fps = 30u32;

    let mut i = 0;
    while i < args.len() {
        match args[i].as_str() {
            "--module" => {
                i += 1;
                module = args.get(i).cloned();
                if module.is_none() {
                    usage();
                }
            }
            "--import" => {
                i += 1;
                import = args.get(i).cloned();
                if import.is_none() {
                    usage();
                }
            }
            "--fps" => {
                i += 1;
                fps = args
                    .get(i)
                    .and_then(|s| s.parse::<u32>().ok())
                    .unwrap_or(30)
                    .clamp(1, 60);
            }
            "--list" => list = true,
            "--library-dir" => library = true,
            _ => usage(),
        }
        i += 1;
    }

    if library {
        match ad_runtime::library_dir() {
            Some(dir) => println!("{}", dir.display()),
            None => std::process::exit(1),
        }
        return;
    }
    if list {
        let Some(dir) = ad_runtime::library_dir() else {
            std::process::exit(1);
        };
        for entry in library::scan(&dir) {
            println!("{}", entry.title);
        }
        return;
    }
    if let Some(source) = import {
        match import_source(&source) {
            Ok(message) => println!("{message}"),
            Err(message) => {
                eprintln!("{message}");
                std::process::exit(1);
            }
        }
        return;
    }
    let Some(title) = module else {
        usage();
    };
    if let Err(message) = run(&title, fps) {
        eprintln!("{message}");
        std::process::exit(1);
    }
}

/// Import a file the user chose: a disk image or folder first, and a
/// compressed archive through `unar` (a ZacOS runtime dependency) if that is
/// what it turns out to be.
fn import_source(source: &str) -> Result<String, String> {
    let path = Path::new(source);
    match ad_runtime::install_from(path) {
        Ok(installed) => return Ok(imported_message(&installed)),
        Err(first) => {
            if !is_archive(path) {
                return Err(first);
            }
            let temp = std::env::temp_dir()
                .join(format!("zacos9-afterdark-import-{}", std::process::id()));
            let _ = std::fs::remove_dir_all(&temp);
            std::fs::create_dir_all(&temp)
                .map_err(|e| format!("Could not make {}: {e}", temp.display()))?;
            let output = std::process::Command::new("unar")
                .args(["-q", "-o"])
                .arg(&temp)
                .arg(path)
                .output();
            match output {
                Ok(out) if out.status.success() => {}
                Ok(out) => {
                    let said = String::from_utf8_lossy(&out.stderr).trim().to_owned();
                    let _ = std::fs::remove_dir_all(&temp);
                    return Err(if said.is_empty() {
                        format!("{first}\n\nunar could not unpack this archive.")
                    } else {
                        format!("{first}\n\nunar said: {said}")
                    });
                }
                Err(e) => {
                    let _ = std::fs::remove_dir_all(&temp);
                    return Err(format!(
                        "{first}\n\nThis is an archive; the importer needs unar to unpack it ({e})."
                    ));
                }
            }
            // The archive may hold a disk image, a flat folder, or one or
            // more nested folders. Try the shapes in that order.
            let mut result = ad_runtime::install_from(&temp);
            if result.is_err() {
                if let Some(image) = find_disk_image(&temp, 0) {
                    result = ad_runtime::install_from(&image);
                }
            }
            if result.is_err() {
                let flat = temp.join("flat");
                if std::fs::create_dir_all(&flat).is_ok() {
                    flatten_rsrc(&temp, &flat, 0);
                    result = ad_runtime::install_from(&flat);
                }
            }
            let _ = std::fs::remove_dir_all(&temp);
            result
                .map(|installed| imported_message(&installed))
                .map_err(|e| format!("{e}\n\n{first}"))
        }
    }
}

fn imported_message(installed: &ad_runtime::Installed) -> String {
    format!(
        "Imported {} module(s), {} font file(s), {} file(s) in all.",
        installed.modules, installed.fonts, installed.total
    )
}

/// A compressed download `unar` can open. Disk images are deliberately not
/// here: they go straight to the HFS reader, and `.bin` can be either.
fn is_archive(path: &Path) -> bool {
    matches!(
        path.extension()
            .and_then(|e| e.to_str())
            .map(str::to_ascii_lowercase)
            .as_deref(),
        Some("zip" | "sit" | "sitx" | "sea" | "cpt" | "hqx")
    )
}

/// The first disk image under `root` (depth-limited).
fn find_disk_image(root: &Path, depth: usize) -> Option<PathBuf> {
    if depth > 4 {
        return None;
    }
    let entries = std::fs::read_dir(root).ok()?;
    let mut folders = Vec::new();
    for entry in entries.flatten() {
        let path = entry.path();
        if path.is_dir() {
            folders.push(path);
        } else if matches!(
            path.extension()
                .and_then(|e| e.to_str())
                .map(str::to_ascii_lowercase)
                .as_deref(),
            Some("img" | "dsk" | "iso" | "toast" | "cdr" | "image" | "bin")
        ) {
            return Some(path);
        }
    }
    for folder in folders {
        if let Some(found) = find_disk_image(&folder, depth + 1) {
            return Some(found);
        }
    }
    None
}

/// Copy every `.rsrc` under `root` into one flat folder. StuffIt archives
/// often put the modules and the fonts in separate subfolders; the importer
/// reads one folder and wants both together.
fn flatten_rsrc(root: &Path, dest: &Path, depth: usize) {
    if depth > 4 {
        return;
    }
    let Ok(entries) = std::fs::read_dir(root) else {
        return;
    };
    for entry in entries.flatten() {
        let path = entry.path();
        if path.is_dir() {
            flatten_rsrc(&path, dest, depth + 1);
        } else if path.extension().is_some_and(|e| e == "rsrc") {
            if let Some(name) = path.file_name() {
                let target = dest.join(name);
                if !target.exists() {
                    let _ = std::fs::copy(&path, &target);
                }
            }
        }
    }
}

/// The module file for `title`: a path as given, or a title in the library.
fn module_path(title: &str) -> Result<PathBuf, String> {
    let given = PathBuf::from(title);
    if given.components().count() > 1 || given.is_absolute() {
        if given.is_file() {
            return Ok(given);
        }
        return Err(format!("{}: no such module", given.display()));
    }
    let dir = ad_runtime::library_dir().ok_or("there is nowhere to keep modules")?;
    let path = dir.join(format!("{title}.rsrc"));
    if path.is_file() {
        Ok(path)
    } else {
        Err(format!("{title}: not in {}", dir.display()))
    }
}

fn run(title: &str, fps: u32) -> Result<(), String> {
    let path = module_path(title)?;
    let bytes = std::fs::read(&path).map_err(|e| format!("{}: {e}", path.display()))?;
    let fork = ResourceFork::parse(&bytes).map_err(|e| format!("{}: {e}", path.display()))?;
    let settings = ModuleSettings::from_fork(&fork);
    let controls = settings.control_values();

    // The module is the only thing in this process; a game legitimately never
    // returns from DrawFrame, and the present hook below still shows its frames.
    // The parent kills us when the screens wake, so there is no watchdog here.
    let mut host = Host::load(fork, controls).map_err(|e| e.to_string())?;
    host.cycle_budget = NO_CYCLE_LIMIT;

    // Fonts are not part of a module. They came off the user's disk with the
    // modules, and `_DrawString` draws nothing without them.
    let dir = path.parent().unwrap_or(Path::new("."));
    for font in ad_runtime::font_forks(dir) {
        host.add_font_fork(&font);
    }

    // The same lifecycle the player and the compatibility lab use.
    match host.call(GmMessage::Initialize) {
        Ok(GmResult::Ok) => {}
        Ok(other) => return Err(format!("Initialize -> {other:?}")),
        Err(e) => return Err(format!("Initialize failed: {e}")),
    }
    match host.call(GmMessage::Blank) {
        Ok(GmResult::Ok) => {}
        Ok(other) => return Err(format!("Blank -> {other:?}")),
        Err(e) => return Err(format!("Blank failed: {e}")),
    }

    let every = (60 / fps.max(1)).max(1);
    let mut out = std::io::stdout().lock();
    host.set_present_hook(
        every,
        Box::new(move |fb, ticks| {
            if write_frame(&mut out, fb, ticks).is_err() {
                // The saver has gone or stopped reading. There is nothing left
                // to show, and unwinding out of the emulator's call is not
                // something this process can do cleanly.
                std::process::exit(0);
            }
        }),
    );

    // One emulated tick is 1/60 s. Present at `fps` through the hook; keep the
    // module's own clock at 60 so animations run at their intended speed.
    let tick = Duration::from_secs_f64(1.0 / 60.0);
    loop {
        let start = Instant::now();
        match host.draw_frame() {
            Ok(GmResult::Ok) => {}
            Ok(other) => return Err(format!("DrawFrame -> {other:?}")),
            Err(e) => return Err(format!("DrawFrame failed: {e}")),
        }
        let spent = start.elapsed();
        if spent < tick {
            std::thread::sleep(tick - spent);
        }
    }
}

fn write_frame(
    out: &mut impl Write,
    fb: &ad_toolbox::quickdraw::Framebuffer,
    ticks: u32,
) -> std::io::Result<()> {
    let (w, h) = (fb.width, fb.height);
    if usize::from(w) * usize::from(h) != fb.pixels.len() {
        return Err(std::io::Error::new(
            std::io::ErrorKind::InvalidData,
            "framebuffer size does not match its pixels",
        ));
    }
    let mut header = [0u8; 12];
    header[0..4].copy_from_slice(&MAGIC.to_le_bytes());
    header[4..6].copy_from_slice(&w.to_le_bytes());
    header[6..8].copy_from_slice(&h.to_le_bytes());
    header[8..12].copy_from_slice(&ticks.to_le_bytes());
    out.write_all(&header)?;

    let mut palette = [0u8; 256 * 3];
    for (i, rgb) in fb.palette.iter().take(256).enumerate() {
        palette[i * 3..i * 3 + 3].copy_from_slice(rgb);
    }
    out.write_all(&palette)?;
    out.write_all(&fb.pixels)?;
    out.flush()
}
