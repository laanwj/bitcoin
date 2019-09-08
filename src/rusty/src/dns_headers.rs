use std::net::{IpAddr, Ipv6Addr, ToSocketAddrs};
use std::time::Duration;
use std::panic::catch_unwind;

use std::ffi::CStr;
use std::os::raw::c_char;

use crate::bridge::*;
use crate::await_ibd_complete_or_stalled;

fn map_addrs_to_header(ips: &mut [Ipv6Addr]) -> [u8; 80] {
    let mut header = [0u8; 80];
    if ips.len() != 6 { return header; }
    ips.sort_unstable_by(|a, b| {
        (&(a.octets()[2] & 0xf0)).cmp(&(b.octets()[2] & 0xf0))
    });

    let mut offs = 0; // in bytes * 2
    'for_ips: for ip in ips {
        for i in 1..14*2 {
            if i % 2 == 1 {
                header[offs/2] |= (ip.octets()[i/2 + 2] & 0x0f) >> 0;
            } else {
                header[offs/2] |= (ip.octets()[i/2 + 2] & 0xf0) >> 4;
            }
            if offs % 2 == 0 {
                header[offs/2] <<= 4;
            }
            offs += 1;
            if offs == 80*2 {
                break 'for_ips;
            }
        }
    }
    header
}

#[test]
fn test_map_addrs() {
    use std::str::FromStr;

    let mut ips = Vec::new();
    // The genesis header:
    ips.push(Ipv6Addr::from_str("2001:5a29:ab5f:49ff:ff00:1d1d:ac2b:7c00").unwrap());
    ips.push(Ipv6Addr::from_str("2001:41bc:3888:a513:23a9:fb8a:a4b1:e5e4").unwrap());
    ips.push(Ipv6Addr::from_str("2001:3a7b:12b2:7ac7:2c3e:6776:8f61:7fc8").unwrap());
    ips.push(Ipv6Addr::from_str("2001:10::").unwrap());
    ips.push(Ipv6Addr::from_str("2001:2000::3:ba3e:dfd7").unwrap());
    ips.push(Ipv6Addr::from_str("2001:1000::").unwrap());

    assert_eq!(&map_addrs_to_header(&mut ips)[..],
        &[0x1, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x3b, 0xa3, 0xed, 0xfd, 0x7a, 0x7b, 0x12, 0xb2, 0x7a, 0xc7, 0x2c, 0x3e, 0x67, 0x76, 0x8f, 0x61, 0x7f, 0xc8, 0x1b, 0xc3, 0x88, 0x8a, 0x51, 0x32, 0x3a, 0x9f, 0xb8, 0xaa, 0x4b, 0x1e, 0x5e, 0x4a, 0x29, 0xab, 0x5f, 0x49, 0xff, 0xff, 0x0, 0x1d, 0x1d, 0xac, 0x2b, 0x7c][..]);
}

#[no_mangle]
pub extern "C" fn init_fetch_dns_headers(domain: *const c_char) -> bool {
    let domain_str: String = match unsafe { CStr::from_ptr(domain) }.to_str() {
        Ok(r) => r.to_string(),
        Err(_) => return false,
    };
    std::thread::spawn(move || {
        // Always catch panics so that even if we have some bug in our parser we don't take the
        // rest of Bitcoin Core down with us:
        let _ = catch_unwind(move || {
            await_ibd_complete_or_stalled();
            let mut height = BlockIndex::tip().height();
            'dns_lookup: while unsafe { !rusty_ShutdownRequested() } {
                let mut ips: Vec<_> = match (format!("{}.{}.{}", height, height / 10000, domain_str).as_str(), 0u16).to_socket_addrs() {
                    Ok(ips) => ips,
                    Err(_) => {
                        std::thread::sleep(Duration::from_secs(5));
                        continue 'dns_lookup;
                    },
                }.filter_map(|a| match a.ip() {
                    IpAddr::V6(a) => Some(a),
                    _ => None,
                }).collect();
                if ips.len() != 6 {
                    std::thread::sleep(Duration::from_secs(5));
                    continue 'dns_lookup;
                }

                match connect_headers_flat_bytes(&map_addrs_to_header(&mut ips)) {
                    Some(_) => {
                        height += 1;
                    },
                    None => {
                        // We couldn't connect the header, step back and try again
                        if height > 0 {
                            height -= 1;
                        } else {
                            std::thread::sleep(Duration::from_secs(5));
                        }
                    },
                }
            }
        });
    });
    true
}
