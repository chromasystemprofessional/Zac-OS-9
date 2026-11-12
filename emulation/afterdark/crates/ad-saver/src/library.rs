//! Finding the playable modules in the imported library.
//!
//! A file is a module when its resource fork contains an `ADgm` code resource.
//! That is the only reliable test: the After Dark disk holds fonts, control
//! panels, the Finder and a dozen other forks alongside the savers, and nothing
//! in a filename distinguishes them.
//!
//! The skip lists are upstream's (`ad-player/src/library.rs`), derived from the
//! compatibility survey. Offering a module that always refuses, or that draws
//! nothing at all, would just be a black screen in the saver.

use ad_resource::ResourceFork;
use std::path::Path;

/// Modules whose own `Initialize` always refuses with "This module requires
/// After Dark 3.0 or later." They hunt the disk for the real After Dark 3.0
/// engine, which this runtime does not host.
const NEEDS_AFTER_DARK_3: &[&str] = &[
    "Bad Dog",
    "Bugs",
    "Bungee Roulette",
    "Chameleon",
    "Clocks",
    "Coming Soon",
    "Fish Pro",
    "Flying Toasters Pro",
    "Flying Toilets",
    "FrankenScreen",
    "Message Mayhem",
    "Mike's So-called Life",
    "Mime Hunt",
    "Mowin' Boris",
    "Nirvana",
    "Phlegm Boy",
    "Rat Race",
    "Shock Clocks",
    "Toxic Swamp",
    "Voyeur",
    "You Bet Your Head",
];

/// Modules the survey says do not play: they refuse, need an unimplemented
/// Toolbox trap, or complete their lifecycle without drawing a pixel.
const DOES_NOT_PLAY: &[&str] = &[
    "Artist",
    "Blackboard",
    "DOS Shell",
    "DrawMorph",
    "Gravity",
    "Marbles",
    "Meadow",
    "Messages",
    "MonitorSD",
    "Movies 'Til Dawn",
    "MultiModule",
    "Nocturnes",
    "Nonsense",
    "PICS Player",
    "Photon",
    "Picture Frame",
    "Punchout",
    "Puzzle",
    "Randomizer",
    "Say What_",
    "Slide Show",
    "Spotlight",
    "Terraform",
];

/// One module the saver can offer.
#[derive(Debug, Clone)]
pub struct Entry {
    /// The filename without `.rsrc`, which is what the Screen Saver panel shows
    /// and what `screensaver-afterdark` stores.
    pub title: String,
}

/// Scan `dir` for modules, sorted by name, leaving out the ones this runtime
/// cannot play.
#[must_use]
pub fn scan(dir: &Path) -> Vec<Entry> {
    let Ok(read) = std::fs::read_dir(dir) else {
        return Vec::new();
    };
    let mut out = Vec::new();
    for item in read.flatten() {
        let path = item.path();
        if path.extension().is_none_or(|e| e != "rsrc") {
            continue;
        }
        let Ok(bytes) = std::fs::read(&path) else {
            continue;
        };
        let Ok(fork) = ResourceFork::parse(&bytes) else {
            continue;
        };
        if fork.all().iter().all(|r| &r.res_type != b"ADgm") {
            continue;
        }
        let title = path
            .file_stem()
            .unwrap_or_default()
            .to_string_lossy()
            .into_owned();
        if NEEDS_AFTER_DARK_3.contains(&title.as_str()) || DOES_NOT_PLAY.contains(&title.as_str()) {
            continue;
        }
        out.push(Entry { title });
    }
    out.sort_by_key(|a| a.title.to_lowercase());
    out
}
