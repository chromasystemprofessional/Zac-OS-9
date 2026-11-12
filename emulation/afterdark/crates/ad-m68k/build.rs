//! Build the vendored Musashi core.
//!
//! Musashi does not ship its opcode dispatch tables: a C program (`m68kmake`)
//! reads `m68k_in.c` and emits `m68kops.c` / `m68kops.h`. The tables are a pure
//! function of `m68k_in.c`, so the upstream project generates them once and
//! commits them under `vendor/generated/`; the normal build compiles target C
//! only.
//!
//! This copy of the crate drives the host C compiler directly instead of using
//! the `cc` crate, so the ZacOS tree has no crates.io dependencies at all. The
//! upstream `AD_M68K_REGENERATE` path is not vendored: the committed tables are
//! used as they are. What this tree is, and the hash of every byte of it, is in
//! `vendor/PROVENANCE.md`.

use std::path::PathBuf;
use std::process::Command;

/// C sources that make up the core, relative to `vendor/`.
const SOURCES: &[&str] = &[
    "m68kcpu.c",
    "m68kdasm.c",
    "generated/m68kops.c",
    "softfloat/softfloat.c",
];

/// Inputs that must trigger a rebuild when they change.
const BUILD_INPUTS: &[&str] = &[
    "m68k.h",
    "m68kconf.h",
    "m68kcpu.c",
    "m68kcpu.h",
    "m68kdasm.c",
    "m68kfpu.c",
    "m68kmmu.h",
    "m68k_in.c",
    "m68kmake.c",
    "softfloat/softfloat.c",
    "softfloat/softfloat.h",
    "softfloat/softfloat-macros",
    "softfloat/softfloat-specialize",
    "softfloat/milieu.h",
    "softfloat/mamesf.h",
    "generated/m68kops.c",
    "generated/m68kops.h",
];

fn main() {
    let vendor = PathBuf::from(std::env::var("CARGO_MANIFEST_DIR").expect("CARGO_MANIFEST_DIR"))
        .join("vendor");
    let out = PathBuf::from(std::env::var("OUT_DIR").expect("OUT_DIR"));

    for f in BUILD_INPUTS {
        println!("cargo:rerun-if-changed={}", vendor.join(f).display());
    }
    println!("cargo:rerun-if-changed=build.rs");
    println!("cargo:rerun-if-env-changed=CC");
    println!("cargo:rerun-if-env-changed=AR");

    let cc = std::env::var("CC").unwrap_or_else(|_| "cc".to_owned());
    let ar = std::env::var("AR").unwrap_or_else(|_| "ar".to_owned());
    let generated = vendor.join("generated");

    let mut objects = Vec::with_capacity(SOURCES.len());
    for source in SOURCES {
        // Keep object names flat: `generated/m68kops.c` must not try to make a
        // `generated` folder inside OUT_DIR.
        let name = source.replace('/', "_").replace(".c", ".o");
        let object = out.join(name);
        let status = Command::new(&cc)
            .args(["-O2", "-fPIC", "-w"])
            .arg("-I")
            .arg(&vendor)
            .arg("-I")
            .arg(&generated)
            .arg("-c")
            .arg(vendor.join(source))
            .arg("-o")
            .arg(&object)
            .status()
            .unwrap_or_else(|e| panic!("cannot run {cc}: {e}"));
        assert!(status.success(), "compiling {} failed", source);
        objects.push(object);
    }

    let library = out.join("libmusashi.a");
    let status = Command::new(&ar)
        .arg("crsD")
        .arg(&library)
        .args(&objects)
        .status()
        .unwrap_or_else(|e| panic!("cannot run {ar}: {e}"));
    assert!(status.success(), "archiving libmusashi.a failed");

    println!("cargo:rustc-link-search=native={}", out.display());
    println!("cargo:rustc-link-lib=static=musashi");
}
