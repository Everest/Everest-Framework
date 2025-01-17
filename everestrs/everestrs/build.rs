use std::env;
use std::path::{Path, PathBuf};

struct Libraries {
    everestrs_sys: PathBuf,
    framework: PathBuf,
}

fn find_everest_workspace_root() -> PathBuf {
    if let Ok(everest_framework_dir) = env::var("EVEREST_RS_FRAMEWORK_SOURCE_LOCATION") {
        return PathBuf::from(everest_framework_dir);
    }

    let mut cur_dir =
        PathBuf::from(env::var("CARGO_MANIFEST_DIR").expect("always set in build.rs execution"));

    // A poor heuristic: We traverse the directories upwards until we find a directory called
    // everest-framework and hope that this is the EVerest workspace.
    while cur_dir.parent().is_some() {
        cur_dir = cur_dir.parent().unwrap().to_path_buf();
        if cur_dir.join("everest-framework").is_dir() {
            return cur_dir;
        }
    }
    panic!("everstrs is not build within an EVerest workspace.");
}

/// Returns the Libraries path if this is a standalone build of everest-framework or None if it is
/// not.
fn find_libs_in_everest_framework(root: &Path) -> Option<Libraries> {
    let (everestrs_sys, framework) =
        if let Ok(everest_framework_dir) = env::var("EVEREST_RS_FRAMEWORK_BINARY_LOCATION") {
            let everest_framework_path = PathBuf::from(everest_framework_dir);
            (
                everest_framework_path.join("everestrs/libeverestrs_sys.a"),
                everest_framework_path.join("lib/libframework.so"),
            )
        } else {
            (
                root.join("everest-framework/build/everestrs/libeverestrs_sys.a"),
                root.join("everest-framework/build/lib/libframework.so"),
            )
        };
    if everestrs_sys.exists() && framework.exists() {
        Some(Libraries {
            everestrs_sys,
            framework,
        })
    } else {
        None
    }
}

fn find_libs_in_dir(lib_dir: &Path) -> Option<Libraries> {
    let everestrs_sys = lib_dir.join("libeverestrs_sys.a");
    let framework = lib_dir.join("libframework.so");
    if everestrs_sys.exists() && framework.exists() {
        Some(Libraries {
            everestrs_sys,
            framework,
        })
    } else {
        None
    }
}

/// Returns the Libraries path if this is an EVerest workspace where make install was run in
/// everest-core/build or None if not.
fn find_libs_in_everest_core_build_dist(root: &Path) -> Option<Libraries> {
    find_libs_in_dir(&root.join("everest-core/build/dist/lib"))
}

/// Takes a path to a library like `libframework.so` and returns the name for the linker, aka
/// `framework`
fn libname_from_path(p: &Path) -> String {
    p.file_stem()
        .and_then(|os_str| os_str.to_str())
        .expect("'p' must be valid UTF-8 and have a .so extension.")
        .strip_prefix("lib")
        .expect("'p' should start with `lib`")
        .to_string()
}

fn print_link_options(p: &Path) {
    println!(
        "cargo:rustc-link-search=native={}",
        p.parent().unwrap().to_string_lossy()
    );
    println!("cargo:rustc-link-lib={}", libname_from_path(p));
    // If the c++ libraries are build with `-fprofile-arcs -ftest-coverage`
    // compiler flags we've to link against the `gcov` lib as well.
    if env::var("CARGO_FEATURE_LINK_GCOV").is_ok() {
        println!("cargo:rustc-link-lib=gcov");
    }
}

fn find_libs_in_everest_workspace() -> Option<Libraries> {
    let root = find_everest_workspace_root();
    let libs = find_libs_in_everest_core_build_dist(&root);
    if libs.is_some() {
        return libs;
    }
    find_libs_in_everest_framework(&root)
}

fn main() {
    // See https://doc.rust-lang.org/cargo/reference/features.html#build-scripts
    // for details.
    if env::var("CARGO_FEATURE_BUILD_BAZEL").is_ok() {
        println!("Skipping due to bazel");
        return;
    }

    let libs = match env::var("EVEREST_LIB_DIR") {
        Ok(p) => find_libs_in_dir(&Path::new(&p)),
        Err(_) => find_libs_in_everest_workspace(),
    };

    let libs = libs
        .expect("Could not find libframework.so and libeverestrs_sys. Either set EVEREST_LIB_DIR to a path
        that contains them or run the build again with everestrs being inside an everest workspace.");

    print_link_options(&libs.everestrs_sys);
    print_link_options(&libs.framework);
    println!("cargo:rustc-link-lib=boost_log");
    println!("cargo:rustc-link-lib=boost_log_setup");
}
