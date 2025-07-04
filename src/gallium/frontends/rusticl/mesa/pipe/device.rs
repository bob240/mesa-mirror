use crate::pipe::screen::*;

use mesa_rust_gen::*;
use mesa_rust_util::ptr::ThreadSafeCPtr;
use mesa_rust_util::string::c_string_to_string;

use std::collections::HashMap;
use std::ffi::CStr;
use std::{env, ptr};

#[derive(PartialEq)]
pub(super) struct PipeLoaderDevice {
    ldev: ThreadSafeCPtr<pipe_loader_device>,
}

impl PipeLoaderDevice {
    fn new(ldev: *mut pipe_loader_device) -> Option<Self> {
        Some(Self {
            // SAFETY: `pipe_loader_device` is considered to be thread-safe
            ldev: unsafe { ThreadSafeCPtr::new(ldev)? },
        })
    }

    fn load_screen(self) -> Option<PipeScreen> {
        let s = unsafe { pipe_loader_create_screen(self.ldev.as_ptr(), false) };
        PipeScreen::new(self, s)
    }

    pub fn driver_name(&self) -> &CStr {
        // SAFETY: ldev is a valid memory allocation
        let ldev = unsafe { self.ldev.as_ref() };

        // SAFETY: The driver name is a valid C string pointer
        unsafe { CStr::from_ptr(ldev.driver_name) }
    }

    pub fn device_type(&self) -> pipe_loader_device_type {
        unsafe { self.ldev.as_ref().type_ }
    }
}

impl Drop for PipeLoaderDevice {
    fn drop(&mut self) {
        unsafe {
            pipe_loader_release(&mut self.ldev.as_ptr(), 1);
        }
    }
}

fn load_devs() -> impl Iterator<Item = PipeLoaderDevice> {
    let n = unsafe { pipe_loader_probe(ptr::null_mut(), 0, true) };
    let mut devices: Vec<*mut pipe_loader_device> = vec![ptr::null_mut(); n as usize];
    unsafe {
        pipe_loader_probe(devices.as_mut_ptr(), n, true);
    }

    devices.into_iter().filter_map(PipeLoaderDevice::new)
}

fn get_enabled_devs() -> HashMap<String, u32> {
    let mut res = HashMap::new();

    // we require the type here as this list can be empty depending on the build options
    let default_devs: &[&str] = &[
        #[cfg(any(rusticl_enable_asahi, rusticl_enable_auto))]
        "asahi",
        #[cfg(rusticl_enable_freedreno)]
        "freedreno",
        #[cfg(rusticl_enable_radeonsi)]
        "radeonsi",
    ];

    // I wished we could use different iterators, but that's not really working out.
    let enabled_devs = env::var("RUSTICL_ENABLE").unwrap_or(default_devs.join(","));
    let mut last_driver = None;
    for driver_str in enabled_devs.split(',') {
        if driver_str.is_empty() {
            continue;
        }

        // if the string parses to a number, just updated the device bitset
        if let Ok(dev_id) = driver_str.parse::<u8>() {
            if let Some(last_driver) = last_driver {
                *res.get_mut(last_driver).unwrap() |= 1 << dev_id;
            }
            continue;
        } else {
            let driver_str: Vec<_> = driver_str.split(':').collect();
            let mut devices = 0;

            if driver_str.len() == 1 {
                devices = !0;
            } else if let Ok(dev_id) = driver_str[1].parse::<u8>() {
                devices |= 1 << dev_id;
            }

            let driver_str = match driver_str[0] {
                "llvmpipe" | "lp" => "swrast",
                "freedreno" => "msm",
                a => a,
            };

            res.insert(driver_str.to_owned(), devices);
            last_driver = Some(driver_str);
        }
    }

    if res.contains_key("panfrost") {
        res.insert("panthor".to_owned(), res["panfrost"]);
    }

    res
}

pub fn load_screens() -> impl Iterator<Item = PipeScreen> {
    let devs = load_devs();
    let mut enabled_devs = get_enabled_devs();

    devs.filter(move |dev| {
        let driver_name = unsafe { c_string_to_string(dev.ldev.as_ref().driver_name) };

        if let Some(enabled_devs) = enabled_devs.get_mut(&driver_name) {
            let res = (*enabled_devs & 1) == 1;
            *enabled_devs >>= 1;

            res
        } else {
            false
        }
    })
    .filter_map(PipeLoaderDevice::load_screen)
}
