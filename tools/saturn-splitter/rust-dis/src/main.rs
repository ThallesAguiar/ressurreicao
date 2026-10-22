// why is 14c0 made into data?

mod cfg;

use regex::Regex;
use serde::de::Deserializer;
use serde_derive::Deserialize;
use serde_yaml;
use std::collections::BTreeMap;
use std::collections::BTreeSet;
use std::collections::HashMap;
use std::collections::HashSet;
use std::fs::File;
use std::io::BufReader;
use std::io::{self, BufRead, Read};
use std::path::Path;
use std::sync::atomic::{AtomicBool, Ordering};

static VERBOSE: AtomicBool = AtomicBool::new(false);

fn verbose() -> bool {
    VERBOSE.load(Ordering::Relaxed)
}

struct DataLabel {
    size: u32,
    label: String,
    source: u32,
    is_function: bool,
}

struct JumpTableEntry {
    table_label: String,
    target_label: String,
    width: u32,
}

fn fetch_instruction(
    file_contents: &Vec<u8>,
    virtual_address: u64,
    virtual_address_base: u64,
) -> u32 {
    let physical_address = virtual_address - virtual_address_base;
    let instr: u32 = ((file_contents[physical_address as usize] as u32) << 8)
        | file_contents[physical_address as usize + 1] as u32;
    return instr;
}

fn match_ni_f(
    _v_addr: u32,
    op: u32,
    _mode: bool,
    string: &mut String,
    _data_labels: &HashMap<u32, DataLabel>,
    _branch_labels: &HashMap<u32, String>,
) {
    match op & 0xf000 {
        0x7000 => string.push_str(&format!("add #0x{:02X}, r{}", op & 0xff, (op >> 8) & 0xf)),
        0xe000 => string.push_str(&format!("mov #{}, r{}", op & 0xff, (op >> 8) & 0xf)),
        // unknown
        _ => string.push_str(&format!(".word 0x{:04X} /* unknown instruction */", op)),
    }
}

fn match_i_f(
    v_addr: u32,
    op: u32,
    mode: bool,
    string: &mut String,
    data_labels: &HashMap<u32, DataLabel>,
    branch_labels: &HashMap<u32, String>,
) {
    match op & 0xff00 {
        0xcd00 => string.push_str(&format!("and.b #0x{:02X}, @(r0, gbr)", op & 0xff)),
        0xcf00 => string.push_str(&format!("or.b #0x{:02X}, @(r0, gbr)", op & 0xff)),
        0xcc00 => string.push_str(&format!("tst.b #0x{:02X}, @(r0, gbr)", op & 0xff)),
        0xce00 => string.push_str(&format!("xor.b #0x{:02X}, @(r0, gbr)", op & 0xff)),
        0xc900 => string.push_str(&format!("and #0x{:02X}, r0", op & 0xff)),
        0x8800 => string.push_str(&format!("cmp/eq #0x{:02X}, r0", op & 0xff)),
        0xcb00 => string.push_str(&format!("or #0x{:02X}, r0", op & 0xff)),
        0xc800 => string.push_str(&format!("tst #0x{:02X}, r0", op & 0xff)),
        0xca00 => string.push_str(&format!("xor #0x{:02X}, r0", op & 0xff)),
        0xc300 => string.push_str(&format!("trapa #0x{:X}", op & 0xff)),
        _ => match_ni_f(v_addr, op, mode, string, data_labels, branch_labels),
    }
}

fn match_nd8_f(
    v_addr: u32,
    op: u32,
    mode: bool,
    string: &mut String,
    data_labels: &HashMap<u32, DataLabel>,
    branch_labels: &HashMap<u32, String>,
) {
    match op & 0xf000 {
        0x9000 => {
            // "mov.w @(0x%03X, pc), r%d"
            let thing = (op & 0xff) * 2 + 4 + v_addr;

            if data_labels.contains_key(&thing) {
                if let Some(value) = data_labels.get(&thing) {
                    // Use the label
                    string.push_str(&format!("mov.w {},r{}", value.label, (op >> 8) & 0xf));
                }
            } else {
                // use an address
                string.push_str(&format!(
                    "mov.w @(0x{:03X},pc),r{}",
                    (op & 0xff) * 2 + 4,
                    (op >> 8) & 0xf
                ));
            }
        }
        0xd000 => {
            // "mov.l @(0x%03X, pc), r%d"
            let v_addr_aligned = (v_addr & 0xfffffffc) == 0;
            // this post explains part of issue. https://dcemulation.org/phpBB/viewtopic.php?style=41&t=105661
            let mut target_a = (op & 0xff) * 4 + 4;
            let target_b = ((op & 0xff) * 4 + 4 + v_addr) & 0xfffffffc;
            let test = (op & 0xff) * 4 + 4 + v_addr;

            // gas alignment issue.
            if (test & 3) == 2 {
                // subtract 2 from target_a
                target_a -= 2;

                let thing = test - 2;

                if data_labels.contains_key(&thing) {
                    if let Some(value) = data_labels.get(&thing) {
                        // Use the label
                        string.push_str(&format!("mov.l {},r{}", value.label, (op >> 8) & 0xf));
                    }
                } else {
                    // use an address
                    string.push_str(&format!(
                        "mov.l @(0x{:03X},pc),r{}",
                        target_a,
                        (op >> 8) & 0xf
                    ));
                }
            } else {
                if data_labels.contains_key(&test) {
                    if let Some(value) = data_labels.get(&test) {
                        // Use the label
                        string.push_str(&format!("mov.l {},r{}", value.label, (op >> 8) & 0xf));
                    }
                } else {
                    // use an address
                    string.push_str(&format!(
                        "mov.l @(0x{:03X},pc),r{}",
                        target_a,
                        (op >> 8) & 0xf
                    ));
                }
            }
        }
        _ => match_i_f(v_addr, op, mode, string, data_labels, branch_labels),
    }
}

fn match_d12_f(
    v_addr: u32,
    op: u32,
    mode: bool,
    string: &mut String,
    data_labels: &HashMap<u32, DataLabel>,
    branch_labels: &HashMap<u32, String>,
) {
    match op & 0xf000 {
        0xa000 => {
            if (op & 0x800) == 0x800 {
                let addr = ((op & 0xfff) + 0xfffff000)
                    .wrapping_mul(2)
                    .wrapping_add(v_addr)
                    .wrapping_add(4);
                if branch_labels.contains_key(&addr) {
                    if let Some(value) = branch_labels.get(&addr) {
                        // Use the label
                        string.push_str(&format!("bra {}", value));
                    }
                } else {
                    // use an address
                    string.push_str(&format!("bra 0x{:08X}", addr));
                }
            } else {
                let addr = (op & 0xfff) * 2 + v_addr + 4;
                if branch_labels.contains_key(&addr) {
                    if let Some(value) = branch_labels.get(&addr) {
                        // Use the label
                        string.push_str(&format!("bra {}", value));
                    }
                } else {
                    // use an address
                    string.push_str(&format!("bra 0x{:08X}", addr));
                }
            }
        }
        0xb000 => {
            if (op & 0x800) == 0x800 {
                let addr = ((op & 0xfff) + 0xfffff000).wrapping_mul(2) + v_addr + 4;
                if branch_labels.contains_key(&addr) {
                    if let Some(value) = branch_labels.get(&addr) {
                        // Use the label
                        string.push_str(&format!("bsr {}", value));
                    }
                } else {
                    // use an address
                    string.push_str(&format!("bsr 0x{:08X}", addr));
                }
            } else {
                let addr = (op & 0xfff) * 2 + v_addr + 4;
                if branch_labels.contains_key(&addr) {
                    if let Some(value) = branch_labels.get(&addr) {
                        // Use the label
                        string.push_str(&format!("bsr {}", value));
                    }
                } else {
                    // use an address
                    string.push_str(&format!("bsr 0x{:08X}", addr));
                }
            }
        }
        _ => match_nd8_f(v_addr, op, mode, string, data_labels, branch_labels),
    }
}

fn match_d_f(
    v_addr: u32,
    op: u32,
    mode: bool,
    string: &mut String,
    data_labels: &HashMap<u32, DataLabel>,
    branch_labels: &HashMap<u32, String>,
) {
    match op & 0xff00 {
        0xc000 => string.push_str(&format!("mov.b r0,@(0x{:03X},gbr)", (op & 0xff) * 1)),
        0xc100 => string.push_str(&format!("mov.w r0,@(0x{:03X},gbr)", (op & 0xff) * 2)),
        0xc200 => string.push_str(&format!("mov.l r0,@(0x{:03X},gbr)", (op & 0xff) * 4)),
        0xc400 => string.push_str(&format!("mov.b @(0x{:03X},gbr),r0", (op & 0xff) * 1)),
        0xc500 => string.push_str(&format!("mov.w @(0x{:03X},gbr),r0", (op & 0xff) * 2)),
        0xc600 => string.push_str(&format!("mov.l @(0x{:03X},gbr),r0", (op & 0xff) * 4)),

        0xc700 => {
            let addr = ((v_addr + 4) & 0xfffffffc) + ((op & 0xff) * 4);
            if let Some(label) = branch_labels.get(&addr) {
                string.push_str(&format!("mova {},r0", label));
            } else {
                string.push_str(&format!("mova 0x{:08X},r0", addr));
            }
        }

        0x8b00 => {
            if (op & 0x80) == 0x80 {
                let addr = (((op & 0xff) + 0xffffff00).wrapping_mul(2))
                    .wrapping_add(v_addr)
                    .wrapping_add(4);

                if branch_labels.contains_key(&addr) {
                    if let Some(value) = branch_labels.get(&addr) {
                        // Use the label
                        string.push_str(&format!("bf {}", value));
                    }
                } else {
                    // use an address
                    string.push_str(&format!("bf 0x{:08X}", addr));
                }
            } else {
                let addr = ((op & 0xff) * 2) + v_addr + 4;

                if branch_labels.contains_key(&addr) {
                    if let Some(value) = branch_labels.get(&addr) {
                        // Use the label
                        string.push_str(&format!("bf {}", value));
                    }
                } else {
                    // use an address
                    string.push_str(&format!("bf 0x{:08X}", addr));
                }
            }
        }
        0x8f00 => {
            if (op & 0x80) == 0x80 {
                let addr = (((op & 0xff) + 0xffffff00).wrapping_mul(2))
                    .wrapping_add(v_addr)
                    .wrapping_add(4);
                if branch_labels.contains_key(&addr) {
                    if let Some(value) = branch_labels.get(&addr) {
                        // Use the label
                        string.push_str(&format!("bf.s {}", value));
                    }
                } else {
                    // use an address
                    string.push_str(&format!("bf.s 0x{:08X}", addr));
                }
            } else {
                let addr = ((op & 0xff) * 2) + v_addr + 4;
                if branch_labels.contains_key(&addr) {
                    if let Some(value) = branch_labels.get(&addr) {
                        // Use the label
                        string.push_str(&format!("bf.s {}", value));
                    }
                } else {
                    // use an address
                    string.push_str(&format!("bf.s 0x{:08X}", addr));
                }
            }
        }
        0x8900 => {
            if (op & 0x80) == 0x80 {
                let addr = (((op & 0xff) + 0xffffff00).wrapping_mul(2))
                    .wrapping_add(v_addr)
                    .wrapping_add(4);

                if branch_labels.contains_key(&addr) {
                    if let Some(value) = branch_labels.get(&addr) {
                        // Use the label
                        string.push_str(&format!("bt {}", value));
                    }
                } else {
                    // use an address
                    string.push_str(&format!("bt 0x{:08X}", addr));
                }
            } else {
                let addr = ((op & 0xff) * 2) + v_addr + 4;
                if branch_labels.contains_key(&addr) {
                    if let Some(value) = branch_labels.get(&addr) {
                        // Use the label
                        string.push_str(&format!("bt {}", value));
                    }
                } else {
                    // use an address
                    string.push_str(&format!("bt 0x{:08X}", addr));
                }
            }
        }
        0x8d00 => {
            if (op & 0x80) == 0x80 {
                let addr = (((op & 0xff) + 0xffffff00).wrapping_mul(2))
                    .wrapping_add(v_addr)
                    .wrapping_add(4);
                if branch_labels.contains_key(&addr) {
                    if let Some(value) = branch_labels.get(&addr) {
                        // Use the label
                        string.push_str(&format!("bt.s {}", value));
                    }
                } else {
                    // use an address
                    string.push_str(&format!("bt.s 0x{:08X}", addr));
                }
            } else {
                let addr = ((op & 0xff) * 2) + v_addr + 4;

                if branch_labels.contains_key(&addr) {
                    if let Some(value) = branch_labels.get(&addr) {
                        // Use the label
                        string.push_str(&format!("bt.s {}", value));
                    }
                } else {
                    // use an address
                    string.push_str(&format!("bt.s 0x{:08X}", addr));
                }
            }
        }
        _ => match_d12_f(v_addr, op, mode, string, data_labels, branch_labels),
    }
}

fn match_nmd_f(
    v_addr: u32,
    op: u32,
    mode: bool,
    string: &mut String,
    data_labels: &HashMap<u32, DataLabel>,
    branch_labels: &HashMap<u32, String>,
) {
    match op & 0xf000 {
        0x1000 => string.push_str(&format!(
            "mov.l r{},@(0x{:03X},r{})",
            (op >> 4) & 0xf,
            (op & 0xf) * 4,
            (op >> 8) & 0xf
        )),
        0x5000 => string.push_str(&format!(
            "mov.l @(0x{:03X},r{}),r{}",
            (op & 0xf) * 4,
            (op >> 4) & 0xf,
            (op >> 8) & 0xf
        )),
        _ => match_d_f(v_addr, op, mode, string, data_labels, branch_labels),
    }
}
fn match_ff00(
    v_addr: u32,
    op: u32,
    mode: bool,
    string: &mut String,
    data_labels: &HashMap<u32, DataLabel>,
    branch_labels: &HashMap<u32, String>,
) {
    match op & 0xff00 {
        0x8400 => {
            if (op & 0x100) == 0x100 {
                string.push_str(&format!(
                    "mov.b @(0x{:03X},r{}),r0",
                    (op & 0xf) * 2,
                    (op >> 4) & 0xf
                ))
            } else {
                string.push_str(&format!(
                    "mov.b @(0x{:03X},r{}),r0",
                    op & 0xf,
                    (op >> 4) & 0xf
                ))
            }
        }
        0x8500 => {
            if (op & 0x100) == 0x100 {
                string.push_str(&format!(
                    "mov.w @(0x{:03X},r{}),r0",
                    (op & 0xf) * 2,
                    (op >> 4) & 0xf
                ))
            } else {
                string.push_str(&format!(
                    "mov.w @(0x{:03X},r{}),r0",
                    op & 0xf,
                    (op >> 4) & 0xf
                ))
            }
        }
        0x8000 => {
            if (op & 0x100) == 0x100 {
                string.push_str(&format!(
                    "mov.b r0,@(0x{:03X},r{})",
                    (op & 0xf) * 2,
                    (op >> 4) & 0xf
                ))
            } else {
                string.push_str(&format!(
                    "mov.b r0,@(0x{:03X},r{})",
                    op & 0xf,
                    (op >> 4) & 0xf
                ))
            }
        }
        0x8100 => {
            if (op & 0x100) == 0x100 {
                string.push_str(&format!(
                    "mov.w r0,@(0x{:03X},r{})",
                    (op & 0xf) * 2,
                    (op >> 4) & 0xf
                ))
            } else {
                string.push_str(&format!(
                    "mov.w r0,@(0x{:03X},r{})",
                    op & 0xf,
                    (op >> 4) & 0xf
                ))
            }
        }
        _ => match_nmd_f(v_addr, op, mode, string, data_labels, branch_labels),
    }
}

fn match_f00f(
    v_addr: u32,
    op: u32,
    mode: bool,
    string: &mut String,
    data_labels: &HashMap<u32, DataLabel>,
    branch_labels: &HashMap<u32, String>,
) {
    match op & 0xf00f {
        0x300c => string.push_str(&format!("add r{}, r{}", (op >> 4) & 0xf, (op >> 8) & 0xf)),
        0x300e => string.push_str(&format!("addc r{}, r{}", (op >> 4) & 0xf, (op >> 8) & 0xf)),
        0x300f => string.push_str(&format!("addv r{}, r{}", (op >> 4) & 0xf, (op >> 8) & 0xf)),
        0x2009 => string.push_str(&format!("and r{}, r{}", (op >> 4) & 0xf, (op >> 8) & 0xf)),
        0x3000 => string.push_str(&format!(
            "cmp/eq r{}, r{}",
            (op >> 4) & 0xf,
            (op >> 8) & 0xf
        )),
        0x3002 => string.push_str(&format!(
            "cmp/hs r{}, r{}",
            (op >> 4) & 0xf,
            (op >> 8) & 0xf
        )),
        0x3003 => string.push_str(&format!(
            "cmp/ge r{}, r{}",
            (op >> 4) & 0xf,
            (op >> 8) & 0xf
        )),
        0x3006 => string.push_str(&format!(
            "cmp/hi r{}, r{}",
            (op >> 4) & 0xf,
            (op >> 8) & 0xf
        )),
        0x3007 => string.push_str(&format!(
            "cmp/gt r{}, r{}",
            (op >> 4) & 0xf,
            (op >> 8) & 0xf
        )),
        0x200c => string.push_str(&format!(
            "cmp/str r{}, r{}",
            (op >> 4) & 0xf,
            (op >> 8) & 0xf
        )),
        0x3004 => string.push_str(&format!("div1 r{}, r{}", (op >> 4) & 0xf, (op >> 8) & 0xf)),
        0x2007 => string.push_str(&format!("div0s r{}, r{}", (op >> 4) & 0xf, (op >> 8) & 0xf)),
        0x300d => string.push_str(&format!(
            "dmuls.l r{}, r{}",
            (op >> 4) & 0xf,
            (op >> 8) & 0xf
        )),
        0x3005 => string.push_str(&format!(
            "dmulu.l r{}, r{}",
            (op >> 4) & 0xf,
            (op >> 8) & 0xf
        )),
        0x600e => string.push_str(&format!(
            "exts.b r{}, r{}",
            (op >> 4) & 0xf,
            (op >> 8) & 0xf
        )),
        0x600f => string.push_str(&format!(
            "exts.w r{}, r{}",
            (op >> 4) & 0xf,
            (op >> 8) & 0xf
        )),
        0x600c => string.push_str(&format!(
            "extu.b r{}, r{}",
            (op >> 4) & 0xf,
            (op >> 8) & 0xf
        )),
        0x600d => string.push_str(&format!(
            "extu.w r{}, r{}",
            (op >> 4) & 0xf,
            (op >> 8) & 0xf
        )),
        0x6003 => string.push_str(&format!("mov r{}, r{}", (op >> 4) & 0xf, (op >> 8) & 0xf)),
        0x0007 => string.push_str(&format!("mul.l r{}, r{}", (op >> 4) & 0xf, (op >> 8) & 0xf)),
        0x200f => string.push_str(&format!("muls r{}, r{}", (op >> 4) & 0xf, (op >> 8) & 0xf)),
        0x200e => string.push_str(&format!("mulu r{}, r{}", (op >> 4) & 0xf, (op >> 8) & 0xf)),
        0x600b => string.push_str(&format!("neg r{}, r{}", (op >> 4) & 0xf, (op >> 8) & 0xf)),
        0x600a => string.push_str(&format!("negc r{}, r{}", (op >> 4) & 0xf, (op >> 8) & 0xf)),
        0x6007 => string.push_str(&format!("not r{}, r{}", (op >> 4) & 0xf, (op >> 8) & 0xf)),
        0x200b => string.push_str(&format!("or r{}, r{}", (op >> 4) & 0xf, (op >> 8) & 0xf)),
        0x3008 => string.push_str(&format!("sub r{}, r{}", (op >> 4) & 0xf, (op >> 8) & 0xf)),
        0x300a => string.push_str(&format!("subc r{}, r{}", (op >> 4) & 0xf, (op >> 8) & 0xf)),
        0x300b => string.push_str(&format!("subv r{}, r{}", (op >> 4) & 0xf, (op >> 8) & 0xf)),
        0x6008 => string.push_str(&format!(
            "swap.b r{}, r{}",
            (op >> 4) & 0xf,
            (op >> 8) & 0xf
        )),
        0x6009 => string.push_str(&format!(
            "swap.w r{}, r{}",
            (op >> 4) & 0xf,
            (op >> 8) & 0xf
        )),
        0x2008 => string.push_str(&format!("tst r{}, r{}", (op >> 4) & 0xf, (op >> 8) & 0xf)),
        0x200a => string.push_str(&format!("xor r{}, r{}", (op >> 4) & 0xf, (op >> 8) & 0xf)),
        0x200d => string.push_str(&format!("xtrct r{}, r{}", (op >> 4) & 0xf, (op >> 8) & 0xf)),
        0x2000 => string.push_str(&format!("mov.b r{},@r{}", (op >> 4) & 0xf, (op >> 8) & 0xf)),
        0x2001 => string.push_str(&format!("mov.w r{},@r{}", (op >> 4) & 0xf, (op >> 8) & 0xf)),
        0x2002 => string.push_str(&format!("mov.l r{},@r{}", (op >> 4) & 0xf, (op >> 8) & 0xf)),
        0x6000 => string.push_str(&format!("mov.b @r{},r{}", (op >> 4) & 0xf, (op >> 8) & 0xf)),
        0x6001 => string.push_str(&format!("mov.w @r{},r{}", (op >> 4) & 0xf, (op >> 8) & 0xf)),
        0x6002 => string.push_str(&format!("mov.l @r{},r{}", (op >> 4) & 0xf, (op >> 8) & 0xf)),
        0x000f => string.push_str(&format!(
            "mac.l @r{}+, @r{}+",
            (op >> 4) & 0xf,
            (op >> 8) & 0xf,
        )),
        0x400f => string.push_str(&format!(
            "mac.w @r{}+, @r{}+",
            (op >> 4) & 0xf,
            (op >> 8) & 0xf,
        )),
        0x6004 => string.push_str(&format!(
            "mov.b @r{}+,r{}",
            (op >> 4) & 0xf,
            (op >> 8) & 0xf
        )),
        0x6005 => string.push_str(&format!(
            "mov.w @r{}+,r{}",
            (op >> 4) & 0xf,
            (op >> 8) & 0xf
        )),
        0x6006 => string.push_str(&format!(
            "mov.l @r{}+,r{}",
            (op >> 4) & 0xf,
            (op >> 8) & 0xf
        )),
        0x2004 => string.push_str(&format!(
            "mov.b r{},@-r{}",
            (op >> 4) & 0xf,
            (op >> 8) & 0xf
        )),
        0x2005 => string.push_str(&format!(
            "mov.w r{},@-r{}",
            (op >> 4) & 0xf,
            (op >> 8) & 0xf
        )),
        0x2006 => string.push_str(&format!(
            "mov.l r{},@-r{}",
            (op >> 4) & 0xf,
            (op >> 8) & 0xf
        )),
        0x0004 => string.push_str(&format!(
            "mov.b r{},@(r0,r{})",
            (op >> 4) & 0xf,
            (op >> 8) & 0xf
        )),
        0x0005 => string.push_str(&format!(
            "mov.w r{},@(r0,r{})",
            (op >> 4) & 0xf,
            (op >> 8) & 0xf
        )),
        0x0006 => string.push_str(&format!(
            "mov.l r{},@(r0,r{})",
            (op >> 4) & 0xf,
            (op >> 8) & 0xf
        )),
        0x000c => string.push_str(&format!(
            "mov.b @(r0,r{}),r{}",
            (op >> 4) & 0xf,
            (op >> 8) & 0xf
        )),
        0x000d => string.push_str(&format!(
            "mov.w @(r0,r{}),r{}",
            (op >> 4) & 0xf,
            (op >> 8) & 0xf
        )),
        0x000e => string.push_str(&format!(
            "mov.l @(r0,r{}),r{}",
            (op >> 4) & 0xf,
            (op >> 8) & 0xf
        )),
        _ => match_ff00(v_addr, op, mode, string, data_labels, branch_labels),
    }
}
fn match_f0ff(
    v_addr: u32,
    op: u32,
    mode: bool,
    string: &mut String,
    data_labels: &HashMap<u32, DataLabel>,
    branch_labels: &HashMap<u32, String>,
) {
    match op & 0xf0ff {
        0x4015 => string.push_str(&format!("cmp/pl r{}", (op >> 8) & 0xf)),
        0x4011 => string.push_str(&format!("cmp/pz r{}", (op >> 8) & 0xf)),
        0x4010 => string.push_str(&format!("dt r{}", (op >> 8) & 0xf)),
        0x0029 => string.push_str(&format!("movt r{}", (op >> 8) & 0xf)),
        0x4004 => string.push_str(&format!("rotl r{}", (op >> 8) & 0xf)),
        0x4005 => string.push_str(&format!("rotr r{}", (op >> 8) & 0xf)),
        0x4024 => string.push_str(&format!("rotcl r{}", (op >> 8) & 0xf)),
        0x4025 => string.push_str(&format!("rotcr r{}", (op >> 8) & 0xf)),
        0x4020 => string.push_str(&format!("shal r{}", (op >> 8) & 0xf)),
        0x4021 => string.push_str(&format!("shar r{}", (op >> 8) & 0xf)),
        0x4000 => string.push_str(&format!("shll r{}", (op >> 8) & 0xf)),
        0x4001 => string.push_str(&format!("shlr r{}", (op >> 8) & 0xf)),
        0x4008 => string.push_str(&format!("shll2 r{}", (op >> 8) & 0xf)),
        0x4009 => string.push_str(&format!("shlr2 r{}", (op >> 8) & 0xf)),
        0x4018 => string.push_str(&format!("shll8 r{}", (op >> 8) & 0xf)),
        0x4019 => string.push_str(&format!("shlr8 r{}", (op >> 8) & 0xf)),
        0x4028 => string.push_str(&format!("shll16 r{}", (op >> 8) & 0xf)),
        0x4029 => string.push_str(&format!("shlr16 r{}", (op >> 8) & 0xf)),
        0x0002 => string.push_str(&format!("stc sr, r{}", (op >> 8) & 0xf)),
        0x0012 => string.push_str(&format!("stc gbr, r{}", (op >> 8) & 0xf)),
        0x0022 => string.push_str(&format!("stc vbr, r{}", (op >> 8) & 0xf)),
        0x000a => string.push_str(&format!("sts mach, r{}", (op >> 8) & 0xf)),
        0x001a => string.push_str(&format!("sts macl, r{}", (op >> 8) & 0xf)),
        0x402a => string.push_str(&format!("sts pr, r{}", (op >> 8) & 0xf)),
        0x401b => string.push_str(&format!("tas.b r{}", (op >> 8) & 0xf)),
        0x4003 => string.push_str(&format!("stc.l sr, @-r{}", (op >> 8) & 0xf)),
        0x4013 => string.push_str(&format!("stc.l gbr, @-r{}", (op >> 8) & 0xf)),
        0x4023 => string.push_str(&format!("stc.l vbr, @-r{}", (op >> 8) & 0xf)),
        0x4002 => string.push_str(&format!("sts.l mach, @-r{}", (op >> 8) & 0xf)),
        0x4012 => string.push_str(&format!("sts.l macl, @-r{}", (op >> 8) & 0xf)),
        0x4022 => string.push_str(&format!("sts.l pr, @-r{}", (op >> 8) & 0xf)),
        0x400e => string.push_str(&format!("ldc r{}, sr", (op >> 8) & 0xf)),
        0x401e => string.push_str(&format!("ldc r{}, gbr", (op >> 8) & 0xf)),
        0x402e => string.push_str(&format!("ldc r{}, vbr", (op >> 8) & 0xf)),
        0x400a => string.push_str(&format!("lds r{}, mach", (op >> 8) & 0xf)),
        0x401a => string.push_str(&format!("lds r{}, macl", (op >> 8) & 0xf)),
        0x402a => string.push_str(&format!("lds r{}, pr", (op >> 8) & 0xf)),
        0x402b => string.push_str(&format!("jmp @r{}", (op >> 8) & 0xf)),
        0x400b => string.push_str(&format!("jsr @r{}", (op >> 8) & 0xf)),
        0x4007 => string.push_str(&format!("ldc.l @r{}+, sr", (op >> 8) & 0xf)),
        0x4017 => string.push_str(&format!("ldc.l @r{}+, gbr", (op >> 8) & 0xf)),
        0x4027 => string.push_str(&format!("ldc.l @r{}+, vbr", (op >> 8) & 0xf)),
        0x4006 => string.push_str(&format!("lds.l @r{}+, mach", (op >> 8) & 0xf)),
        0x4016 => string.push_str(&format!("lds.l @r{}+, macl", (op >> 8) & 0xf)),
        0x4026 => string.push_str(&format!("lds.l @r{}+, pr", (op >> 8) & 0xf)),
        0x0023 => string.push_str(&format!("braf r{}", (op >> 8) & 0xf)),
        0x0003 => string.push_str(&format!("bsrf r{}", (op >> 8) & 0xf)),
        _ => {
            match_f00f(v_addr, op, mode, string, data_labels, branch_labels);
        }
    }
}

fn sh2_disasm(
    v_addr: u32,
    op: u32,
    mode: bool,
    string: &mut String,
    data_labels: &HashMap<u32, DataLabel>,
    branch_labels: &HashMap<u32, String>,
) {
    match op & 0xffff {
        0x0008 => string.push_str("clrt"),
        0x0028 => string.push_str("clrmac"),
        0x0019 => string.push_str("div0u"),
        0x0009 => string.push_str("nop"),
        0x002b => string.push_str("rte"),
        0x000b => string.push_str("rts"),
        0x0018 => string.push_str("sett"),
        0x001b => string.push_str("sleep"),
        _ => {
            match_f0ff(v_addr, op, mode, string, data_labels, branch_labels);
        }
    }
}

fn read_file_to_vec(filename: &str) -> io::Result<Vec<u8>> {
    let mut file = File::open(filename)?;
    let mut buffer = Vec::new();
    file.read_to_end(&mut buffer)?;
    Ok(buffer)
}

#[derive(Clone, Copy)]
struct FunctionRange {
    phys_start: u32,
    phys_end: u32,
    is_data: bool,
}

fn candidate_has_matching_saves(vec: &[u8], func_start: u32, rts: u32) -> bool {
    let mut saved_gprs = [false; 16];
    let mut restored_gprs = [false; 16];
    let mut saved_pr = false;
    let mut restored_pr = false;

    for pc in (func_start..=rts).step_by(2) {
        let instr = (vec[pc as usize] as u32) << 8 | vec[pc as usize + 1] as u32;
        if instr & 0xff0f == 0x2f06 {
            saved_gprs[((instr >> 4) & 0xf) as usize] = true;
        } else if instr & 0xf0ff == 0x60f6 {
            restored_gprs[((instr >> 8) & 0xf) as usize] = true;
        } else if instr == 0x4f22 {
            saved_pr = true;
        } else if instr == 0x4f26 {
            restored_pr = true;
        }
    }

    (!restored_pr || saved_pr)
        && restored_gprs
            .iter()
            .enumerate()
            .all(|(reg, restored)| !restored || saved_gprs[reg])
}

fn find_funcs(
    vec: &Vec<u8>,
    section_start: u64,
    section_end: u64,
    ranges: &mut Vec<FunctionRange>,
) {
    let mut literal_rts = HashSet::<u32>::new();
    for i in (section_start..section_end).step_by(2) {
        let op = (vec[i as usize] as u32) << 8 | vec[i as usize + 1] as u32;
        if op & 0xf000 != 0xd000 {
            continue;
        }
        let target = (i as u32 + 4 + (op & 0xff) * 4) & !3;
        if target >= section_start as u32 && target + 4 <= section_end as u32 {
            let high = (vec[target as usize] as u32) << 8 | vec[target as usize + 1] as u32;
            let low = (vec[target as usize + 2] as u32) << 8 | vec[target as usize + 3] as u32;
            if high & 0xf000 == 0xf000 && low == 0x000b {
                literal_rts.insert(target + 2);
            }
        }
    }

    // first, find every location of an rts.
    let mut rts_pos: Vec<u32> = Vec::new();
    for i in (section_start..section_end).step_by(2) {
        let instr = (vec[i as usize] as u32) << 8 | vec[i as usize + 1] as u32;
        if instr == 0x000b && !literal_rts.contains(&(i as u32)) {
            rts_pos.push(i as u32);
        }
    }

    for i in 0..rts_pos.len() {
        let prev_rts = if i > 0 { rts_pos[i - 1] } else { 0 };
        let has_literal_rts = literal_rts.iter().any(|&offset| {
            offset > prev_rts
                && offset < rts_pos[i]
                && ((vec[offset as usize] as u32) << 8 | vec[offset as usize + 1] as u32) == 0x000b
        });
        let mut func_start = 0;
        let mut longest_preamble = 0;
        let mut pc = rts_pos[i] - 2;
        // Scan back to the previous return and select the longest contiguous
        // register-save sequence. A function can push a temporary value in its
        // body, so the closest push to the return is not necessarily its prologue.
        while pc >= prev_rts && pc > 0 {
            let instr = (vec[pc as usize] as u32) << 8 | vec[(pc + 1) as usize] as u32;

            if instr & 0xFF0F == 0x2F06 {
                let mut run_start = pc;
                let mut run_len = 1;
                while run_start >= prev_rts + 2 && run_start > 2 {
                    let previous = run_start - 2;
                    let previous_instr =
                        (vec[previous as usize] as u32) << 8 | vec[(previous + 1) as usize] as u32;
                    if previous_instr & 0xFF06 != 0x2F06 {
                        break;
                    }
                    run_start = previous;
                    run_len += 1;
                }
                if run_len > longest_preamble {
                    longest_preamble = run_len;
                    func_start = run_start;
                }
                if !has_literal_rts {
                    break;
                }
                pc = run_start;
            }

            pc -= 2;
        }

        if func_start != 0 && candidate_has_matching_saves(vec, func_start, rts_pos[i]) {
            let range = FunctionRange {
                phys_start: func_start,
                phys_end: rts_pos[i] + 2,
                is_data: false,
            };
            ranges.push(range);
        }
    }
}

fn infunc(i: u32, ranges: &Vec<FunctionRange>) -> (bool, u32) {
    for j in 0..ranges.len() {
        let current_func = &ranges[j];
        if i >= current_func.phys_start && i <= current_func.phys_end {
            return (true, current_func.phys_start);
        }
    }
    (false, 0)
}

fn infunc_extended(i: u32, ranges: &Vec<FunctionRange>) -> (bool, u32) {
    for j in 0..ranges.len() {
        let current_func = &ranges[j];
        if i >= current_func.phys_start && i <= current_func.phys_end {
            // trivially in func
            return (true, current_func.phys_start);
        }
    }

    if ranges.len() > 0 {
        // check after funcs
        for j in 0..ranges.len() - 1 {
            let current_func = &ranges[j];
            let next_func = &ranges[j + 1];
            if i >= current_func.phys_start && i < next_func.phys_start {
                // in func rodata
                return (true, current_func.phys_start);
            }
        }
    }

    if ranges.len() > 0 {
        // if this is the last func go to end
        let last_func = &ranges[ranges.len() - 1];
        if i >= last_func.phys_start {
            return (true, last_func.phys_start);
        }
    }

    (false, 0)
}

fn is_beyond_last_func(i: u32, ranges: &Vec<FunctionRange>) -> (bool, u32) {
    let last_func = &ranges[ranges.len() - 1];

    if i > last_func.phys_end {
        return (true, last_func.phys_start);
    }

    (false, 0)
}

fn add_label(addr: u32, branch_labels: &mut HashMap<u32, String>) {
    let label = format!(".L{:08X}", addr);
    branch_labels.entry(addr).or_insert(label);
}

/// `(table virtual address, table byte size, entry target virtual addresses)`
/// for one resolved `jmp @rN` dispatch, keyed by the dispatch instruction's
/// own physical address. This is the same information `jump_table_entries`
/// already encodes (as label strings, for disassembly text), reshaped so the
/// CFG walker in cfg.rs can use it as a `resolve_jump_table` oracle without
/// re-deriving any of the bound/index-register tracing above.
pub type JumpTableDispatch = (u32, u32, Vec<u32>);

fn find_jump_tables(
    file_contents: &Vec<u8>,
    section_start: u64,
    section_end: u64,
    virtual_base_addr: u64,
    ranges: &Vec<FunctionRange>,
    branch_labels: &mut HashMap<u32, String>,
    jump_table_entries: &mut HashMap<u32, JumpTableEntry>,
    dispatch_targets: &mut HashMap<u32, JumpTableDispatch>,
) {
    for i in (section_start..section_end).step_by(2) {
        let ii = i as usize;
        let op = ((file_contents[ii] as u32) << 8) | file_contents[ii + 1] as u32;
        let Some((function_index, function)) = ranges
            .iter()
            .enumerate()
            .find(|(_, range)| i as u32 >= range.phys_start && i as u32 <= range.phys_end)
        else {
            continue;
        };
        let ownership_end = ranges
            .get(function_index + 1)
            .map(|next| next.phys_start.saturating_sub(1))
            .unwrap_or(section_end.saturating_sub(1) as u32);
        if op & 0xff00 != 0xc700 || i < section_start + 4 || i + 8 >= section_end {
            continue;
        }

        let next = |offset: usize| -> u32 {
            ((file_contents[ii + offset] as u32) << 8) | file_contents[ii + offset + 1] as u32
        };
        let table_load = next(2);
        let load_register = (table_load >> 8) & 0xf;
        let entry_width = match table_load & 0xf00f {
            0x000c if ((table_load >> 4) & 0xf) == load_register => 1,
            0x000d if ((table_load >> 4) & 0xf) == load_register => 2,
            _ => continue,
        };
        let add_offset = next(4);
        if add_offset & 0xff0f != 0x300c || ((add_offset >> 4) & 0xf) != load_register {
            continue;
        }
        let dispatch_op = next(6);
        let ordinary_delay_slot = |address: u32, delay_op: u32| {
            matches!(
                cfg::classify_effect(address.wrapping_add(virtual_base_addr as u32), delay_op),
                cfg::Effect::Fallthrough | cfg::Effect::PcLiteralLoad { .. }
            )
        };
        let direct_dispatch = dispatch_op == 0x402b
            && ordinary_delay_slot(i as u32 + 8, next(8));
        let branch_target_pos = if dispatch_op & 0xf000 == 0xa000 && next(8) == 0x0009 {
            let branch_pos = i as u32 + 6;
            let displacement = ((dispatch_op & 0x0fff) as i16) << 4 >> 4;
            let target = branch_pos
                .wrapping_add(4)
                .wrapping_add((i32::from(displacement) * 2) as u32);
            let target_pos = target as usize;
            let is_jmp = target_pos + 3 < file_contents.len()
                && (((file_contents[target_pos] as u32) << 8)
                    | file_contents[target_pos + 1] as u32)
                    == 0x402b
                && ordinary_delay_slot(
                    target + 2,
                    ((file_contents[target_pos + 2] as u32) << 8)
                        | file_contents[target_pos + 3] as u32,
                );
            is_jmp.then_some(target as u32)
        } else {
            None
        };
        let branched_dispatch = branch_target_pos.is_some();
        if !direct_dispatch && !branched_dispatch {
            continue;
        }
        // Physical address of the `jmp @rN` instruction itself, in either
        // encoding shape - used to key `dispatch_targets` so the CFG walker
        // (cfg.rs) can resolve a `jmp @rN` it's reached without duplicating
        // any of this pattern-matching itself.
        let dispatch_addr: u32 = if direct_dispatch {
            i as u32 + 6
        } else {
            branch_target_pos.unwrap()
        };

        // The compiler normally copies the checked index into the table index
        // register in the delay slot of the preceding bf.s, then branches around
        // the dispatch for out-of-range values:
        //
        //     cmp/hi bound,index
        //     bf.s dispatch
        //      mov index,table_index
        //     bra default
        //      nop
        // dispatch:
        //     add table_index,table_index
        //     mova table,r0
        //
        // Do not assume that the mov is exactly two instructions before mova.
        // Start with the register doubled immediately before mova and trace a
        // nearby mov back to the register used by cmp/hi.
        let mut index_registers = Vec::new();
        if entry_width == 1 {
            // Byte offsets are used directly; the table load's destination is
            // the checked index register (or a nearby copy of it).
            index_registers.push(load_register);
        } else {
            let doubled = (i.saturating_sub(8)..i)
                .step_by(2)
                .collect::<Vec<_>>()
                .into_iter()
                .rev()
                .map(|address| {
                    let pos = address as usize;
                    ((file_contents[pos] as u32) << 8) | file_contents[pos + 1] as u32
                })
                .find(|candidate| {
                    candidate & 0xf00f == 0x300c
                        && ((candidate >> 8) & 0xf) == ((candidate >> 4) & 0xf)
                });
            if let Some(doubled) = doubled {
                index_registers.push((doubled >> 8) & 0xf);
            } else {
                let previous =
                    ((file_contents[ii - 2] as u32) << 8) | file_contents[ii - 1] as u32;
                // Some dispatches copy an index which an earlier instruction
                // has already doubled. Record both sides of the copy.
                if previous & 0xf00f != 0x6003 || ((previous >> 8) & 0xf) != load_register {
                    continue;
                }
                let destination = (previous >> 8) & 0xf;
                let source = (previous >> 4) & 0xf;
                index_registers.push(destination);
                if source != destination {
                    index_registers.push(source);
                }
            }
        }

        let table_index_register = index_registers[0];
        let trace_start = i.saturating_sub(32).max(section_start);
        for address in (trace_start..i)
            .step_by(2)
            .collect::<Vec<_>>()
            .into_iter()
            .rev()
        {
            let pos = address as usize;
            let move_op = ((file_contents[pos] as u32) << 8) | file_contents[pos + 1] as u32;
            if move_op & 0xf00f == 0x6003 && ((move_op >> 8) & 0xf) == table_index_register {
                let source_register = (move_op >> 4) & 0xf;
                if !index_registers.contains(&source_register) {
                    index_registers.push(source_register);
                }
            }
        }

        let mut max_index = None;
        let scan_start = u64::from(function.phys_start).max(section_start);
        for address in (scan_start..i)
            .step_by(2)
            .collect::<Vec<_>>()
            .into_iter()
            .rev()
        {
            let pos = address as usize;
            let compare = ((file_contents[pos] as u32) << 8) | file_contents[pos + 1] as u32;
            if compare & 0xf00f != 0x3006 || !index_registers.contains(&((compare >> 8) & 0xf)) {
                continue;
            }
            let bound_register = (compare >> 4) & 0xf;
            let immediate_start = address.saturating_sub(24).max(scan_start);
            for immediate_address in (immediate_start..address)
                .step_by(2)
                .collect::<Vec<_>>()
                .into_iter()
                .rev()
            {
                let immediate_pos = immediate_address as usize;
                let candidate = ((file_contents[immediate_pos] as u32) << 8)
                    | file_contents[immediate_pos + 1] as u32;
                let major = candidate >> 12;
                let writes_high_register = matches!(major, 3 | 5 | 6 | 7 | 9 | 0xd | 0xe)
                    && ((candidate >> 8) & 0xf) == bound_register;
                if !writes_high_register {
                    continue;
                }
                if major == 0xe {
                    max_index = Some(candidate & 0xff);
                }
                // Only the nearest definition is relevant. If it was not an
                // immediate load, fall back to inferring the table length.
                break;
            }
            break;
        }
        let virtual_addr = i as u32 + virtual_base_addr as u32;
        let table_addr = ((virtual_addr + 4) & 0xfffffffc) + ((op & 0xff) * 4);
        let Some(table_offset) = table_addr.checked_sub(virtual_base_addr as u32) else {
            continue;
        };
        if table_offset < function.phys_start || table_offset > ownership_end {
            continue;
        }

        let read_target = |index: u32| -> Option<u32> {
            let entry_offset = (table_offset + index * entry_width) as usize;
            let signed_offset = if entry_width == 1 {
                *file_contents.get(entry_offset)? as i8 as i32
            } else {
                let high = *file_contents.get(entry_offset)? as u16;
                let low = *file_contents.get(entry_offset + 1)? as u16;
                ((high << 8) | low) as i16 as i32
            };
            let target = table_addr.wrapping_add(signed_offset as u32);
            let target_offset = target.checked_sub(virtual_base_addr as u32)?;
            if target_offset < function.phys_start || target_offset > ownership_end {
                return None;
            }
            Some(target)
        };

        let mut targets = Vec::new();
        if let Some(max_index) = max_index {
            for index in 0..=max_index {
                let Some(target) = read_target(index) else {
                    targets.clear();
                    break;
                };
                targets.push(target);
            }
        }

        // When the bound is held in a register loaded from memory, infer the
        // table length from the first case target. GCC places the cases directly
        // after the table, so the first address reached while scanning entries is
        // the exclusive end of the table.
        if targets.is_empty() {
            let mut first_target = u32::MAX;
            for index in 0..4096 {
                if table_addr + index * entry_width >= first_target {
                    break;
                }
                let Some(target) = read_target(index) else {
                    targets.clear();
                    break;
                };
                if target <= table_addr {
                    targets.clear();
                    break;
                }
                first_target = first_target.min(target);
                targets.push(target);
            }
        }
        if targets.is_empty() {
            continue;
        }

        let table_label = format!(".Ljtbl_{:08X}", table_addr);
        branch_labels.insert(table_addr, table_label.clone());
        dispatch_targets.insert(
            dispatch_addr,
            (
                table_offset,
                targets.len() as u32 * entry_width,
                targets
                    .iter()
                    .map(|target| target - virtual_base_addr as u32)
                    .collect(),
            ),
        );
        for (index, target) in targets.into_iter().enumerate() {
            add_label(target, branch_labels);
            let target_label = branch_labels.get(&target).unwrap().clone();
            jump_table_entries.insert(
                table_addr + index as u32 * entry_width,
                JumpTableEntry {
                    table_label: table_label.clone(),
                    target_label,
                    width: entry_width,
                },
            );
        }
    }
}

fn remove_jump_table_internal_labels(
    branch_labels: &mut HashMap<u32, String>,
    jump_table_entries: &HashMap<u32, JumpTableEntry>,
) {
    for (address, entry) in jump_table_entries {
        if branch_labels.get(address) != Some(&entry.table_label) {
            branch_labels.remove(address);
        }
    }
}

fn add_data_label(source: u32, addr: u32, size: u32, data_labels: &mut HashMap<u32, DataLabel>) {
    let the_label = format!(".Ldat_{:08X}", addr);
    let data_label = DataLabel {
        size,
        label: the_label,
        source: source,
        is_function: false,
    };
    data_labels.insert(addr, data_label);
}

fn find_branch_labels(v_addr: u32, op: u32, branch_labels: &mut HashMap<u32, String>) {
    let is_bf = (op & 0xff00) == 0x8b00;
    let is_bfs = (op & 0xff00) == 0x8f00;
    let is_bt = (op & 0xff00) == 0x8900;
    let is_bts = (op & 0xff00) == 0x8d00;

    let is_bra = (op & 0xf000) == 0xa000;
    let is_bsr = (op & 0xf000) == 0xb000;
    let is_mova = (op & 0xff00) == 0xc700;

    if is_mova {
        let addr = ((v_addr + 4) & 0xfffffffc) + ((op & 0xff) * 4);
        add_label(addr, branch_labels);
    } else if is_bf || is_bfs || is_bt || is_bts {
        // bf
        if op & 0x80 != 0 {
            /* sign extend */
            let addr = (((op & 0xff) + 0xffffff00).wrapping_mul(2))
                .wrapping_add(v_addr)
                .wrapping_add(4);
            add_label(addr, branch_labels);
        } else {
            let addr = ((op & 0xff) * 2) + v_addr + 4;
            add_label(addr, branch_labels);
        }
    } else if is_bra || is_bsr {
        if op & 0x800 != 0 {
            /* sign extend */
            let addr = ((op & 0xfff) + 0xfffff000)
                .wrapping_mul(2)
                .wrapping_add(v_addr)
                .wrapping_add(4);
            add_label(addr, branch_labels);
        } else {
            let addr = (op & 0xfff) * 2 + v_addr + 4;
            add_label(addr, branch_labels);
        }
    }
}

fn find_data_labels(v_addr: u32, op: u32, data_labels: &mut HashMap<u32, DataLabel>) {
    // is this already marked as data?
    if data_labels.contains_key(&v_addr.try_into().unwrap()) {
        // don't try to dissassemble as an instruction
        return;
    }

    // is this marked as the second word of long data?
    if data_labels.contains_key(&(v_addr - 2).try_into().unwrap()) {
        if let Some(value) = data_labels.get(&(v_addr - 2).try_into().unwrap()) {
            if value.size == 4 {
                return;
            }
        }
    }

    if (op & 0xf000) == 0x9000 {
        let addr = (op & 0xff) * 2 + 4 + v_addr;
        add_data_label(v_addr, addr, 2, data_labels);
    } else if (op & 0xf000) == 0xd000 {
        let target = ((op & 0xff) * 4 + 4 + v_addr) & 0xfffffffc;

        if v_addr == 0x6d94 && verbose() {
            println!("problem {:08X}", target);
            // return;
        }
        // TODO fixme this shouln't be marked as data
        if target == 0x14C0 {
            if verbose() {
                println!("problem {:08X}", v_addr);
            }
            return;
        }

        if target == 0x35c8 {
            if verbose() {
                println!("problem {:08X}", v_addr);
            }
            return;
        }
        add_data_label(v_addr, target, 4, data_labels);
    }
}

fn literal_feeds_call(file_contents: &Vec<u8>, source: u32, virtual_base_addr: u64) -> bool {
    let Some(source_offset) = source.checked_sub(virtual_base_addr as u32) else {
        return false;
    };
    cfg::literal_feeds_jsr(file_contents, source_offset)
}

fn literal_feeds_jump(file_contents: &Vec<u8>, source: u32, virtual_base_addr: u64) -> bool {
    let Some(source_offset) = source.checked_sub(virtual_base_addr as u32) else {
        return false;
    };
    cfg::literal_feeds_jmp(file_contents, source_offset)
}

#[derive(Debug, Deserialize)]
struct Options {
    target_path: String,
    asm_path: String,
    ld_scripts_path: String,
    syms_path: String,
    src_path: String,
    #[serde(default)]
    obj_path: String,
    #[serde(default)]
    check_layout: bool,
    decomp_empty_funcs: bool,
    /// Symbol files this target inherits, relative to `syms_path`, highest
    /// precedence first. Mirrors the `-T` scripts the link is given; see
    /// `inherited_symbol_files` in tools/builds/saturn.py.
    #[serde(default)]
    symbol_files: Vec<String>,
}

#[derive(Debug)]
struct Subsegment {
    start: u64,
    end: Option<u64>,
    segment_type: Option<String>,
    file: Option<String>,
    function_ranges: Vec<[u32; 2]>,
    data_ranges: Vec<[u32; 2]>,
}

impl<'de> serde::Deserialize<'de> for Subsegment {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: Deserializer<'de>,
    {
        #[derive(Deserialize)]
        #[serde(untagged)]
        enum SubsegmentSyntax {
            Mapping {
                start: u64,
                end: Option<u64>,
                #[serde(rename = "type")]
                segment_type: Option<String>,
                file: Option<String>,
                #[serde(default)]
                function_ranges: Vec<[u32; 2]>,
                #[serde(default)]
                data_ranges: Vec<[u32; 2]>,
            },
            Compact((u64, String, String)),
        }

        match <SubsegmentSyntax as serde::Deserialize>::deserialize(deserializer)? {
            SubsegmentSyntax::Mapping {
                start,
                end,
                segment_type,
                file,
                function_ranges,
                data_ranges,
            } => Ok(Self {
                start,
                end,
                segment_type,
                file,
                function_ranges,
                data_ranges,
            }),
            SubsegmentSyntax::Compact((start, segment_type, file)) => Ok(Self {
                start,
                end: None,
                segment_type: Some(segment_type),
                file: Some(file),
                function_ranges: Vec::new(),
                data_ranges: Vec::new(),
            }),
        }
    }
}

#[derive(Debug, Deserialize)]
struct Segment {
    name: String,
    #[serde(rename = "type")]
    segment_type: String,
    start: u64,
    subsegments: Option<Vec<Subsegment>>,
    vram: u64,
    subalign: Option<u64>,
}

#[derive(Debug, Deserialize)]
struct Config {
    options: Options,
    segments: Option<Vec<Segment>>,
}

fn parse_yaml2(filename: String) -> Config {
    // Read the YAML configuration file
    let mut file =
        File::open(filename.clone()).expect(&format!("Failed to open the file {}", filename));
    let mut contents = String::new();
    file.read_to_string(&mut contents)
        .expect("Failed to read the file.");

    // Parse the YAML into a Config struct
    let mut config: Config = serde_yaml::from_str(&contents).expect("Failed to parse YAML.");
    if let Some(ref mut segments) = config.segments {
        for segment in segments {
            if let Some(ref mut subsegments) = segment.subsegments {
                for i in 0..subsegments.len().saturating_sub(1) {
                    if subsegments[i].end.is_none() {
                        subsegments[i].end = Some(subsegments[i + 1].start)
                    }
                }
            }
        }
    }
    config
}

/// A symbol read from one of the `*_syms*.txt` linker scripts.
///
/// `size` comes from an optional trailing annotation and is what makes it safe
/// to render an interior pointer as `name+offset`: without a declared size the
/// nearest preceding symbol is only a guess, and folding an offset onto the
/// wrong object produces output that assembles and links but lies.
#[derive(Clone, Debug, PartialEq, Eq)]
struct Symbol {
    name: String,
    size: Option<u32>,
}

/// Address-ordered symbol table with first-wins insertion, so the caller
/// controls precedence purely by the order it loads files in.
#[derive(Default, Debug)]
struct SymbolTable {
    symbols: BTreeMap<u32, Symbol>,
}

impl SymbolTable {
    fn insert(&mut self, address: u32, symbol: Symbol) {
        let is_autogenerated = |name: &str| name.starts_with("_func_") || name.starts_with("_DAT_");
        match self.symbols.entry(address) {
            std::collections::btree_map::Entry::Vacant(entry) => {
                entry.insert(symbol);
            }
            std::collections::btree_map::Entry::Occupied(mut entry)
                if is_autogenerated(&entry.get().name) && !is_autogenerated(&symbol.name) =>
            {
                entry.insert(symbol);
            }
            std::collections::btree_map::Entry::Occupied(_) => {}
        }
    }

    fn get(&self, address: u32) -> Option<&Symbol> {
        self.symbols.get(&address)
    }

    /// Resolve `address` to `name`, or to `name+0xN` when it lands inside an
    /// object whose size was declared. Returns `None` when nothing covers it.
    fn lookup(&self, address: u32) -> Option<String> {
        self.lookup_with_interior(address, true)
    }

    /// As `lookup`, but `allow_interior` refuses to fold an offset onto a
    /// preceding symbol. Call targets use this: a literal loaded into a
    /// register that is then `jsr`ed is an entry point, so resolving it to
    /// something+offset would describe a call that does not happen.
    fn lookup_with_interior(&self, address: u32, allow_interior: bool) -> Option<String> {
        if let Some(symbol) = self.symbols.get(&address) {
            return Some(symbol.name.clone());
        }
        if !allow_interior {
            return None;
        }
        let (base, symbol) = self.symbols.range(..address).next_back()?;
        let size = symbol.size?;
        let offset = address - base;
        if offset < size {
            Some(format!("{}+0x{:X}", symbol.name, offset))
        } else {
            None
        }
    }
}

/// `size = 0x1000` style annotations live in a trailing C comment because these
/// files are fed to sh-elf-ld as linker scripts; `//` is a syntax error there.
fn parse_symbol_annotations(comment: &str) -> Option<u32> {
    let mut size = None;
    for field in comment.split(',') {
        let mut parts = field.splitn(2, '=');
        let key = parts.next()?.trim();
        let Some(raw) = parts.next() else {
            continue;
        };
        let raw = raw.trim();
        if key != "size" {
            continue;
        }
        let parsed = if let Some(hex) = raw
            .strip_prefix("0x")
            .or_else(|| raw.strip_prefix("0X"))
        {
            u32::from_str_radix(hex, 16)
        } else {
            raw.parse::<u32>()
        };
        match parsed {
            Ok(value) => size = Some(value),
            Err(_) => eprintln!("warning: unparsable size annotation: {}", field.trim()),
        }
    }
    size
}

fn symbol_line_pattern() -> Regex {
    Regex::new(
        r"^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(0x[0-9A-Fa-f]+)\s*;\s*(?:/\*(?P<note>[^*]*)\*/)?\s*$",
    )
    .unwrap()
}

fn read_user_symbols_into(filename: &str, symbols: &mut SymbolTable) {
    let Ok(contents) = std::fs::read_to_string(filename) else {
        return;
    };
    let pattern = symbol_line_pattern();
    for line in contents.lines() {
        let Some(captures) = pattern.captures(line) else {
            continue;
        };
        let name = captures.get(1).unwrap().as_str();
        let address = u32::from_str_radix(
            captures.get(2).unwrap().as_str().trim_start_matches("0x"),
            16,
        )
        .unwrap();
        let size = captures
            .name("note")
            .and_then(|note| parse_symbol_annotations(note.as_str()));
        symbols.insert(
            address,
            Symbol {
                name: name.to_string(),
                size,
            },
        );
    }
}

fn read_user_symbols(filename: &str) -> SymbolTable {
    let mut symbols = SymbolTable::default();
    read_user_symbols_into(filename, &mut symbols);
    symbols
}

/// Load every symbol file the linker will see for this target, in priority
/// order. Hand-written names must be listed before generated ones so that a
/// real name always beats a `_func_%08X` placeholder for the same address.
fn read_symbol_files(paths: &[String]) -> SymbolTable {
    let mut symbols = SymbolTable::default();
    for path in paths {
        read_user_symbols_into(path, &mut symbols);
    }
    symbols
}

/// Saturn work RAM. A literal in this window is an address; anything else is a
/// constant and stays hex.
const WORK_RAM_START: u32 = 0x06000000;
const WORK_RAM_END: u32 = 0x06100000;

fn is_work_ram(value: u32) -> bool {
    (WORK_RAM_START..WORK_RAM_END).contains(&value)
}

fn auto_symbol_name(value: u32) -> String {
    format!("_DAT_{:08X}", value)
}

/// Render a 32-bit literal. Unresolved work-RAM addresses get a generated
/// `_DAT_%08X` name recorded in `auto_symbols`, which the caller writes out as a
/// linker script so the reference resolves.
fn format_literal(
    value: u32,
    user_symbols: &SymbolTable,
    auto_symbols: &mut BTreeSet<u32>,
    allow_interior: bool,
) -> String {
    if let Some(symbol) = user_symbols.lookup_with_interior(value, allow_interior) {
        return symbol;
    }
    if is_work_ram(value) {
        auto_symbols.insert(value);
        return auto_symbol_name(value);
    }
    format!("0x{:08X}", value)
}
use std::io::Write;

fn emit_c_file(functions: &BTreeMap<u32, DisassembledFunc>, output_path: String) {
    let filename = format!("{}/output.c", output_path);
    let mut file = std::fs::File::create(filename).expect("Failed to create file.");
    writeln!(&mut file, "#include \"inc_asm.h\"").expect("Failed to write to file.");
    for pair in functions {
        let mut label = &format!("d_{:08X}", pair.1.addr);
        if !pair.1.data {
            let mut label = &format!("func_{:08X}", pair.1.addr);
        }
        writeln!(
            &mut file,
            "INCLUDE_ASM(\"{}\", {}, \"{}\");",
            pair.1.file, pair.1.name, label
        )
        .expect("Failed to write to file.");
    }
}

/// Write `contents` to `filename` only if that is not already what the file
/// holds.
///
/// The build tracks the files this tool emits by modification time, so
/// unconditionally recreating them makes every re-split look like every
/// function changed, forcing a full reassemble and relink of the overlay even
/// when the output is identical.
fn write_if_changed(filename: &str, contents: &str) {
    if std::fs::read_to_string(filename).ok().as_deref() == Some(contents) {
        return;
    }
    std::fs::write(filename, contents).expect("Failed to write file.");
}

fn emit_asm_file(filename: String, data: String) -> std::path::PathBuf {
    write_if_changed(&filename, &format!("{}\n", data));
    std::path::PathBuf::from(filename)
}

fn find_asm_files(path: &std::path::Path, files: &mut HashSet<std::path::PathBuf>) {
    if !path.exists() {
        return;
    }
    for entry in std::fs::read_dir(path).expect("Failed to read asm output directory.") {
        let entry = entry.expect("Failed to read asm output entry.");
        let file_type = entry
            .file_type()
            .expect("Failed to inspect asm output entry.");
        if file_type.is_dir() {
            find_asm_files(&entry.path(), files);
        } else if file_type.is_file() && entry.path().extension() == Some(std::ffi::OsStr::new("s"))
        {
            files.insert(entry.path());
        }
    }
}

fn remove_stale_asm_files(
    previous: &HashSet<std::path::PathBuf>,
    emitted: &HashSet<std::path::PathBuf>,
) {
    for stale_path in previous.difference(emitted) {
        std::fs::remove_file(stale_path).expect("Failed to remove stale asm output file.");
    }
}

use std::fmt;

struct DisassembledFunc {
    addr: u32,
    end: u32,
    text: String,
    data: bool,
    name: String,
    file: String,
}

fn check_data_labels(
    virtual_addr: u32,
    data_labels: &HashMap<u32, DataLabel>,
    skip_next: &mut bool,
    should_continue: &mut bool,
    file_contents: &Vec<u8>,
    start_address: u32,
    disassembled_funcs: &mut BTreeMap<u32, DisassembledFunc>,
    i: u32,
    instr: u16,
    user_symbols: &SymbolTable,
    auto_symbols: &mut BTreeSet<u32>,
    branch_labels: &HashMap<u32, String>,
    ranges: &Vec<FunctionRange>,
    virtual_base_addr: u64,
) {
    if data_labels.contains_key(&virtual_addr.try_into().unwrap()) {
        if let Some(value) = data_labels.get(&virtual_addr.try_into().unwrap()) {
            // Use the label

            if let Some(func) = disassembled_funcs.get_mut(&(start_address as u32)) {
                func.text.push_str(&format!(
                    "{}: /* source: {:08X} */\n",
                    value.label, value.source
                ));
            }
            if value.size == 2 {
                if let Some(func) = disassembled_funcs.get_mut(&(start_address as u32)) {
                    func.text.push_str(&format!(".word 0x{:04X}\n", instr));
                }
            } else if value.size == 4 {
                let data = ((file_contents[i as usize + 0] as u32) << 24)
                    | ((file_contents[i as usize + 1] as u32) << 16)
                    | ((file_contents[i as usize + 2] as u32) << 8)
                    | ((file_contents[i as usize + 3] as u32) << 0);
                // A literal that doesn't feed a call and targets an address inside
                // the same function it lives in is a far-jump target, not a
                // reference to another object: prefer the local `.L` label
                // (already generated for it if anything branches there directly)
                // over a generic _DAT_/symbol name, so m2c can prove the jump is
                // a local far branch.
                let local_label = if !value.is_function {
                    let phys_target = data.wrapping_sub(virtual_base_addr as u32);
                    let (_, target_func_start) = infunc_extended(phys_target, ranges);
                    if target_func_start == start_address {
                        branch_labels.get(&data).cloned()
                    } else {
                        None
                    }
                } else {
                    None
                };
                let literal = local_label.unwrap_or_else(|| {
                    format_literal(data, user_symbols, auto_symbols, !value.is_function)
                });
                if let Some(func) = disassembled_funcs.get_mut(&(start_address as u32)) {
                    func.text
                        .push_str(&format!("/* {:08X} */ .long {}\n", virtual_addr, literal));
                }

                // skip next instruction since we used it
                *skip_next = true;
            }
            *should_continue = true;
        }
    }
}

/// Function ranges for one code subsegment. Legacy YAML `function_ranges`
/// contribute entry-point hints only; their end values are intentionally not
/// trusted. Control-flow reachability determines ownership and extent.
///
/// Shared by the emit pass and the symbol pre-pass so the two can never
/// disagree about which functions exist.
fn collect_function_ranges(
    file_contents: &Vec<u8>,
    section_start: u64,
    section_end: u64,
    virtual_base_addr: u32,
    forced_function_ranges: &[[u32; 2]],
) -> Vec<FunctionRange> {
    let mut ranges = Vec::<FunctionRange>::new();
    find_funcs(&file_contents, section_start, section_end, &mut ranges);

    for &[entry, _legacy_end] in forced_function_ranges {
        assert!(entry >= section_start as u32 && entry < section_end as u32);
        if ranges.iter().any(|range| range.phys_start == entry) {
            continue;
        }
        // If the heuristic placed this true entry in the middle of a guessed
        // range, replace that owner with the explicit seed. The CFG pass below
        // will recover its real end and any continuation ranges it reaches.
        ranges.retain(|range| entry < range.phys_start || entry > range.phys_end);
        ranges.push(FunctionRange {
            phys_start: entry,
            phys_end: entry,
            is_data: false,
        });
    }
    ranges.sort_by_key(|range| range.phys_start);

    augment_ranges_with_confirmed_call_targets(
        file_contents,
        section_start,
        section_end,
        virtual_base_addr,
        &mut ranges,
    );

    ranges
}

/// Stage 3/4 of split-core.txt: seed and flood-fill any function whose only
/// evidence is a confirmed `jsr @rN` call target that `find_funcs`'s
/// prologue heuristic (and the YAML) missed entirely - typically a leaf
/// function that saves no registers, so there's no push run for the
/// heuristic to anchor on. Without this, such a target has no
/// `FunctionRange` at all, so `load_segment_symbols` never gives it a
/// `_func_%08X` name and any reference to it falls through to the generic
/// `_DAT_%08X` fallback in `format_literal` - this is the bug the rework
/// exists to fix.
///
/// It also reconciles every heuristic or YAML-seeded entry through the CFG
/// walker. Consequently, a stored range end is never ownership evidence:
/// reachable instructions establish the real extent, while another known
/// entry establishes a boundary unless ordinary control flow proves that
/// entry was merely a false-positive continuation. See split-core.txt.
fn augment_ranges_with_confirmed_call_targets(
    file_contents: &Vec<u8>,
    section_start: u64,
    section_end: u64,
    virtual_base_addr: u32,
    ranges: &mut Vec<FunctionRange>,
) {
    // Jump-table dispatch resolution for the walker's `resolve_jump_table`
    // oracle - reuses find_jump_tables's own already-tuned pattern matching
    // (see the Stage 2 scoping note in cfg.rs) rather than re-deriving it.
    // The initial range set supplies known entries and ownership windows.
    // Those windows bound validation, but their stale ends do not determine
    // function ownership; this output is only the oracle for the CFG walk.
    let mut branch_labels = HashMap::<u32, String>::new();
    let mut jump_table_entries = HashMap::<u32, JumpTableEntry>::new();
    let mut dispatch_targets = HashMap::<u32, JumpTableDispatch>::new();
    find_jump_tables(
        file_contents,
        section_start,
        section_end,
        virtual_base_addr as u64,
        ranges,
        &mut branch_labels,
        &mut jump_table_entries,
        &mut dispatch_targets,
    );

    let resolve_indirect = |addr: u32| {
        cfg::resolve_jsr_target(file_contents, virtual_base_addr, addr)
            .or_else(|| cfg::resolve_jmp_target(file_contents, virtual_base_addr, addr))
    };

    // Reconcile heuristic/YAML boundaries before looking for wholly missing
    // callees.  A range reached by ordinary control flow belongs to the
    // current function; a prologue-looking sequence at that address is not
    // sufficient evidence for a second top-level function.
    let mut range_index = 0;
    while range_index < ranges.len() {
        let seed = ranges.remove(range_index);
        if let Some(merged) = recover_range_absorbing_conflicts(
            file_contents,
            section_start,
            section_end,
            virtual_base_addr,
            seed,
            ranges,
            &resolve_indirect,
            &|addr| dispatch_targets.get(&addr).cloned(),
        ) {
            ranges.push(merged);
            ranges.sort_by_key(|range| range.phys_start);
        } else {
            ranges.push(seed);
            ranges.sort_by_key(|range| range.phys_start);
        }
        range_index += 1;
    }

    // Fixed point: a newly discovered leaf function can itself call another
    // one find_funcs missed. Bounded so malformed/adversarial input can't
    // spin forever; real call chains are nowhere near this deep.
    for _ in 0..8 {
        // Only look for jsr sites inside code the splitter already trusts as
        // code (find_funcs/YAML, plus anything a previous round of this same
        // loop confirmed) - mirrors how `literal_feeds_call` is only ever
        // consulted for data labels found inside an already-recognized
        // function (main.rs's `is_in_func` gate). Scanning raw, unclaimed
        // bytes for a `0x4?0b` bit pattern would risk treating incidental
        // data as a fake call site.
        let known_ranges = ranges.clone();
        let mut new_seeds = BTreeSet::<u32>::new();
        for range in &known_ranges {
            for i in (range.phys_start..=range.phys_end).step_by(2) {
                let ii = i as usize;
                if ii + 1 >= file_contents.len() {
                    continue;
                }
                let op = ((file_contents[ii] as u32) << 8) | file_contents[ii + 1] as u32;
                if op & 0xf0ff != 0x400b {
                    continue;
                }
                let Some(target) = resolve_indirect(i) else {
                    continue;
                };
                if target < section_start as u32 || target >= section_end as u32 {
                    continue;
                }
                if infunc(target, ranges).0 {
                    continue;
                }
                new_seeds.insert(target);
            }
        }
        if new_seeds.is_empty() {
            break;
        }

        // Newly call-confirmed entries were absent when the first dispatch
        // oracle was built. Give each one a provisional window up to the next
        // known entry so inline tables can be decoded before its first CFG
        // ownership walk; the walk, not this window, still decides the end.
        let mut probe_ranges = ranges.clone();
        for &entry in &new_seeds {
            probe_ranges.push(FunctionRange {
                phys_start: entry,
                phys_end: entry,
                is_data: false,
            });
        }
        probe_ranges.sort_by_key(|range| range.phys_start);
        for index in 0..probe_ranges.len() {
            if new_seeds.contains(&probe_ranges[index].phys_start) {
                probe_ranges[index].phys_end = probe_ranges
                    .get(index + 1)
                    .map(|next| next.phys_start.saturating_sub(1))
                    .unwrap_or(section_end.saturating_sub(1) as u32);
            }
        }
        find_jump_tables(
            file_contents,
            section_start,
            section_end,
            virtual_base_addr as u64,
            &probe_ranges,
            &mut branch_labels,
            &mut jump_table_entries,
            &mut dispatch_targets,
        );

        for entry in new_seeds {
            if infunc(entry, ranges).0 {
                // A previous seed earlier in this same round already
                // claimed it (e.g. two call sites resolving to functions
                // that turned out to overlap).
                continue;
            }
            let seed = FunctionRange {
                phys_start: entry,
                phys_end: entry,
                is_data: false,
            };
            let Some(recovered) = recover_range_absorbing_conflicts(
                file_contents,
                section_start,
                section_end,
                virtual_base_addr,
                seed,
                ranges,
                &resolve_indirect,
                &|addr| dispatch_targets.get(&addr).cloned(),
            ) else {
                continue;
            };
            ranges.push(recovered);
            ranges.sort_by_key(|range| range.phys_start);
        }
    }
}

/// Flood-fill `seed`, removing later ranges that its ordinary control-flow
/// edges prove are continuations.  Earlier owners are never absorbed: reaching
/// one from a later entry is ambiguous and must not let an alternate/interior
/// address steal the containing function.
fn recover_range_absorbing_conflicts(
    file_contents: &Vec<u8>,
    section_start: u64,
    section_end: u64,
    virtual_base_addr: u32,
    seed: FunctionRange,
    ranges: &mut Vec<FunctionRange>,
    resolve_call: &dyn Fn(u32) -> Option<u32>,
    resolve_jump_table: &dyn Fn(u32) -> Option<JumpTableDispatch>,
) -> Option<FunctionRange> {
    let original_ranges = ranges.clone();
    let mut merged_end = seed.phys_end;

    loop {
        let other_ranges: Vec<(u32, u32)> = ranges
            .iter()
            .map(|range| (range.phys_start, range.phys_end))
            .collect();
        let walk = cfg::flood_fill_function(
            file_contents,
            virtual_base_addr,
            section_start as u32,
            section_end as u32,
            seed.phys_start,
            &other_ranges,
            resolve_call,
            resolve_jump_table,
        );

        if walk.boundary_conflicts.is_empty() {
            let reached_end = walk
                .claimed_code
                .iter()
                .copied()
                .max()
                .unwrap_or(seed.phys_start);
            return Some(FunctionRange {
                phys_start: seed.phys_start,
                phys_end: merged_end.max(reached_end),
                is_data: false,
            });
        }

        let mut absorbed_any = false;
        for conflict in walk.boundary_conflicts {
            let Some(owner) = ranges
                .iter()
                .copied()
                .find(|range| conflict >= range.phys_start && conflict <= range.phys_end)
            else {
                *ranges = original_ranges;
                return None;
            };
            if owner.phys_start <= seed.phys_start {
                *ranges = original_ranges;
                return None;
            }
            merged_end = merged_end.max(owner.phys_end);
            ranges.retain(|range| range.phys_start != owner.phys_start);
            absorbed_any = true;
        }
        if !absorbed_any {
            *ranges = original_ranges;
            return None;
        }
    }
}

/// True when the splitter will emit a `_func_%08X` linker symbol for this
/// function. Eight-byte functions are dropped from the symbol file when
/// `decomp_empty_funcs` is set (see the emit loop in `handle_segments`), so
/// referring to one would leave an undefined symbol at link time.
fn emits_function_symbol(range: &FunctionRange, decomp_empty_funcs: bool) -> bool {
    !(decomp_empty_funcs && range.phys_end - range.phys_start == 8)
}

fn handle_code_section(
    file_contents: &Vec<u8>,
    section_start: u64,
    section_end: u64,
    virtual_base_addr: u64,
    user_symbols: &SymbolTable,
    auto_symbols: &mut BTreeSet<u32>,
    forced_function_ranges: &[[u32; 2]],
    forced_data_ranges: &[[u32; 2]],
) -> (BTreeMap<u32, DisassembledFunc>) {
    let len = file_contents.len();
    let ranges = collect_function_ranges(
        file_contents,
        section_start,
        section_end,
        virtual_base_addr as u32,
        forced_function_ranges,
    );

    for &[phys_start, phys_end] in forced_data_ranges {
        assert!(phys_start >= section_start as u32 && phys_end < section_end as u32);
        assert!(phys_start <= phys_end);
    }

    let is_forced_data = |address: u32| {
        forced_data_ranges
            .iter()
            .any(|range| address >= range[0] && address <= range[1])
    };

    if ranges.len() == 0 && verbose() {
        println!("no ranges");
    }

    let mut data_labels = HashMap::<u32, DataLabel>::new();
    let mut branch_labels = HashMap::<u32, String>::new();
    let mut jump_table_entries = HashMap::<u32, JumpTableEntry>::new();
    let mut dispatch_targets = HashMap::<u32, JumpTableDispatch>::new();

    find_jump_tables(
        file_contents,
        section_start,
        section_end,
        virtual_base_addr,
        &ranges,
        &mut branch_labels,
        &mut jump_table_entries,
        &mut dispatch_targets,
    );

    for i in (section_start..section_end).step_by(2) {
        if is_forced_data(i as u32) {
            continue;
        }
        let ii = i as usize;
        let instr: u32 = ((file_contents[ii] as u32) << 8) | file_contents[ii + 1] as u32;

        let (is_in_func, start_address) = infunc(i as u32, &ranges);

        if !is_in_func {
            continue;
        }

        find_branch_labels(
            TryInto::<u32>::try_into(i).unwrap() + virtual_base_addr as u32,
            instr,
            &mut branch_labels,
        );
        find_data_labels(
            TryInto::<u32>::try_into(i).unwrap() + virtual_base_addr as u32,
            instr,
            &mut data_labels,
        );
    }

    remove_jump_table_internal_labels(&mut branch_labels, &jump_table_entries);

    let mut local_far_jump_targets = Vec::new();
    for (&literal_address, label) in data_labels.iter_mut() {
        if label.size == 4 {
            label.is_function = literal_feeds_call(file_contents, label.source, virtual_base_addr);
            if !label.is_function
                && literal_feeds_jump(file_contents, label.source, virtual_base_addr)
            {
                let literal_offset = literal_address.checked_sub(virtual_base_addr as u32);
                if let Some(literal_offset) = literal_offset {
                    let pos = literal_offset as usize;
                    if pos + 3 < file_contents.len() {
                        let target = ((file_contents[pos] as u32) << 24)
                            | ((file_contents[pos + 1] as u32) << 16)
                            | ((file_contents[pos + 2] as u32) << 8)
                            | file_contents[pos + 3] as u32;
                        let source_offset = label.source - virtual_base_addr as u32;
                        if let Some(target_offset) = target.checked_sub(virtual_base_addr as u32) {
                            let (source_is_func, source_start) = infunc(source_offset, &ranges);
                            let (target_is_func, target_start) = infunc(target_offset, &ranges);
                            if source_is_func && target_is_func && source_start == target_start {
                                local_far_jump_targets.push(target);
                            }
                        }
                    }
                }
            }
        }
    }
    for target in local_far_jump_targets {
        add_label(target, &mut branch_labels);
    }
    let mut disassembled_funcs = BTreeMap::<u32, DisassembledFunc>::new();

    // create emtpy ones for all funcs
    for f in &ranges {
        let virtual_addr =
            TryInto::<u32>::try_into(f.phys_start).unwrap() + virtual_base_addr as u32;
        disassembled_funcs
            .entry(f.phys_start)
            .or_insert(DisassembledFunc {
                addr: f.phys_start,
                end: f.phys_end,
                text: "".to_string(),
                data: f.is_data,
                name: format!("f{:07X}", virtual_addr),
                file: "_".to_string(),
            });
    }

    let mut monolithic = String::new();

    let mut skip_next = false;

    for i in (section_start..section_end).step_by(2) {
        let ii = i as usize;
        let instr: u32 = ((file_contents[ii] as u32) << 8) | file_contents[ii + 1] as u32;
        let virtual_addr = TryInto::<u32>::try_into(i).unwrap() + virtual_base_addr as u32;
        if skip_next {
            skip_next = false;
            continue;
        }

        let (is_in_func, start_address) = infunc(i as u32, &ranges);

        // // the last function needs to emit data up until the next section
        let (is_in_func_extended, start_address_extended) = infunc_extended(i as u32, &ranges);

        if i as u32 == start_address {
            if let Some(func) = disassembled_funcs.get_mut(&(start_address as u32)) {
                let label = user_symbols
                    .get(virtual_addr)
                    .map(|symbol| symbol.name.as_str())
                    .filter(|name| name.starts_with("___"))
                    .map(str::to_string)
                    .unwrap_or_else(|| format!("func_{:08X}", virtual_addr));
                func.text.push_str(&format!("glabel {}\n", label));
            }
        }

        // check to emit data, use extended addr
        if let Some(func) = disassembled_funcs.get_mut(&(start_address_extended as u32)) {
            if func.data {
                func.text.push_str(&format!(
                    "/* 0x{:08X} */ .word 0x{:04X}\n",
                    virtual_addr, instr
                ));
                if verbose() {
                    println!("\ti is {:08X} func.data continue", i);
                }

                continue;
            }
        }

        if branch_labels.contains_key(&virtual_addr.try_into().unwrap()) {
            if let Some(value) = branch_labels.get(&virtual_addr.try_into().unwrap()) {
                // Use the label
                if let Some(func) = disassembled_funcs.get_mut(&(start_address as u32)) {
                    func.text.push_str(&format!("{}:\n", value));
                    monolithic.push_str(&format!("{}:\n", value));
                }
            }
        }

        if let Some(entry) = jump_table_entries.get(&virtual_addr) {
            if let Some(func) = disassembled_funcs.get_mut(&(start_address as u32)) {
                let directive = if entry.width == 1 { ".byte" } else { ".word" };
                func.text.push_str(&format!(
                    "/* 0x{:08X} */ {} {}-{}\n",
                    virtual_addr, directive, entry.target_label, entry.table_label
                ));
                // The outer disassembly loop advances by one SH word. Byte
                // tables have two independently symbolic entries per loop
                // iteration, so emit the odd-address entry here as well.
                if entry.width == 1 {
                    if let Some(next_entry) = jump_table_entries.get(&(virtual_addr + 1)) {
                        func.text.push_str(&format!(
                            "/* 0x{:08X} */ .byte {}-{}\n",
                            virtual_addr + 1,
                            next_entry.target_label,
                            next_entry.table_label
                        ));
                    }
                }
            }
            continue;
        }

        // data labels, extended addr
        let mut should_continue: bool = false;
        check_data_labels(
            virtual_addr,
            &data_labels,
            &mut skip_next,
            &mut should_continue,
            &file_contents,
            start_address_extended,
            &mut disassembled_funcs,
            i as u32,
            instr as u16,
            user_symbols,
            auto_symbols,
            &branch_labels,
            &ranges,
            virtual_base_addr,
        );

        if should_continue {
            continue;
        }

        if is_forced_data(i as u32) {
            if let Some(func) = disassembled_funcs.get_mut(&(start_address_extended as u32)) {
                func.text.push_str(&format!(
                    "/* 0x{:08X} */ .word 0x{:04X}\n",
                    virtual_addr, instr
                ));
            }
            continue;
        }

        // only disasm if we are in a func
        if is_in_func {
            let mut string = String::new();
            sh2_disasm(
                i as u32 + virtual_base_addr as u32,
                instr,
                true,
                &mut string,
                &mut data_labels,
                &mut branch_labels,
            );
            if let Some(func) = disassembled_funcs.get_mut(&(start_address as u32)) {
                func.text.push_str(&format!(
                    "/* 0x{:08X} 0x{:04X} */ {}\n",
                    virtual_addr, instr, string
                ));
            }
        } else {
            if let Some(func) = disassembled_funcs.get_mut(&(start_address_extended as u32)) {
                // emit uncaught data
                func.text.push_str(&format!(
                    "/* 0x{:08X} */ .word 0x{:04X}\n",
                    virtual_addr, instr
                ));
            }
        }
    }

    return disassembled_funcs;
}

#[derive(Default)]
struct ProcessedSection {
    is_code: bool,
    disassembled_funcs: BTreeMap<u32, DisassembledFunc>,
    data: String,
    addr: u64,
    vaddr: u64,
    vbase: u64,
}

fn write_c_file(
    config: &Config,
    path: &str,
    segment_name: &str,
    asm_path: &str,
    processed_sections: &Vec<ProcessedSection>,
) {
    let filename = format!("{}/{}.c", path, segment_name);
    let mut file = std::fs::File::create(filename).expect("Failed to create file.");
    writeln!(&mut file, "#include \"inc_asm.h\"").expect("Failed to write to file.");

    for processed_section in processed_sections {
        if !processed_section.is_code {
            let name = format!("d{:07X}", processed_section.vaddr);
            writeln!(
                &mut file,
                "INCLUDE_ASM(\"{}\", {}, d_{:08X});",
                asm_path, // TODO fix hardcode
                name,
                processed_section.vaddr
            )
            .expect("Failed to write to file.");
        } else {
            for pair in &processed_section.disassembled_funcs {
                // assume this is a empty function if the size is 8
                if (pair.1.end - pair.1.addr == 8) && config.options.decomp_empty_funcs {
                    writeln!(&mut file, "void {}() {{}}", pair.1.name)
                        .expect("Failed to write to file.");
                } else {
                    writeln!(
                        &mut file,
                        "INCLUDE_ASM(\"{}\", {}, func_{:08X});",
                        asm_path,
                        pair.1.name,
                        pair.1.addr + processed_section.vbase as u32
                    )
                    .expect("Failed to write to file.");
                }
            }
        }
    }
}

fn find_include_asm_in_c_file(filename: &str) -> io::Result<HashSet<String>> {
    let file = File::open(filename)?;
    let reader = BufReader::new(file);
    let re = Regex::new(r#"INCLUDE_ASM(?:_NO_ALIGN)?\("(.*?)", (.*?), (.*?)\)"#).unwrap();
    let mut result = HashSet::new();

    for res in reader.lines() {
        if let Some(caps) = re.captures(&res.unwrap().to_string()) {
            let asm_id = caps.get(2).unwrap().as_str().trim().to_string();
            let func_name = caps.get(3).unwrap().as_str().to_string();
            result.insert(asm_id);
            result.insert(func_name);
        }
    }

    Ok(result)
}

/// Every symbol file this segment may reference, in precedence order.
///
/// The disassembler's symbol scope has to match the linker's or cross-binary
/// references (a game overlay pointing at a zero-resident global) can never
/// resolve. The list comes from the YAML rather than being globbed, because
/// overlays share VRAM - alucard, maria and richter all load at 0x060A5000 -
/// and globbing would stamp a name from an unrelated overlay onto an address
/// in this one.
/// Byte offsets (from the anchor word at `___ashiftrt_r4_32`/`___ashiftrt_r4_31`)
/// of every entry point into GCC's SH `___ashiftrt_r4_<n>` helper ladder, paired
/// with the `<n>` suffix. Derived from `gcc/config/sh/lib1funcs.asm`
/// (`L_ashiftrt`): the routine is a fixed, compiler-generated instruction
/// sequence, so once the anchor is found the whole ladder's addresses follow
/// deterministically.
const ASHIFTRT_R4_LADDER: &[(u32, u32)] = &[
    (0, 32),
    (0, 31),
    (6, 30),
    (8, 29),
    (10, 28),
    (12, 27),
    (14, 26),
    (16, 25),
    (18, 24),
    (26, 23),
    (28, 22),
    (30, 21),
    (32, 20),
    (34, 19),
    (36, 18),
    (38, 17),
    (40, 16),
    (46, 15),
    (48, 14),
    (50, 13),
    (52, 12),
    (54, 11),
    (56, 10),
    (58, 9),
    (60, 8),
    (62, 7),
    (64, 6),
    (66, 5),
    (68, 4),
    (70, 3),
    (72, 2),
    (74, 1),
    (78, 0),
];

/// Scan `file_contents` for every occurrence of the `___ashiftrt_r4` helper
/// ladder and return the (address, symbol name) pair for each entry point.
///
/// The ladder has no per-entry prologue/epilogue `find_funcs` can key off of -
/// most entries are a single `shar r4` that falls through to the next one - so
/// the normal function-boundary heuristics never recover these as named
/// functions. m2c only special-cases calls through the exact
/// `___ashiftrt_r4_<n>` names (it needs to know these routines only touch r4),
/// so anything the splitter can't name this way shows up as a generic
/// `_DAT_%08X` call target and gets treated as clobbering r0-r7.
///
/// The routine's three-word entry `rotcl r4 / rts / subc r4,r4` (0x4424,
/// 0x000b, 0x344a) is distinctive enough to use as an anchor: it doesn't occur
/// elsewhere in compiler-generated shift/divide code, and every other entry
/// point's address is a fixed offset from it.
fn find_ashiftrt_r4_symbols(file_contents: &[u8], vram: u32) -> Vec<(u32, String)> {
    let mut symbols = Vec::new();
    if file_contents.len() < 6 {
        return symbols;
    }
    for i in (0..file_contents.len() - 6).step_by(2) {
        let w0 = (file_contents[i] as u32) << 8 | file_contents[i + 1] as u32;
        let w1 = (file_contents[i + 2] as u32) << 8 | file_contents[i + 3] as u32;
        let w2 = (file_contents[i + 4] as u32) << 8 | file_contents[i + 5] as u32;
        if w0 == 0x4424 && w1 == 0x000b && w2 == 0x344a {
            let anchor = i as u32 + vram;
            for &(offset, shift) in ASHIFTRT_R4_LADDER {
                symbols.push((anchor + offset, format!("___ashiftrt_r4_{}", shift)));
            }
        }
    }
    symbols
}

/// Exact machine code for GCC SH `libgcc1` runtime helpers that have a single
/// entry point, keyed by the `___name` m2c/the linker expects.
///
/// Unlike `___ashiftrt_r4`, these routines only need one symbol each, but
/// `find_funcs` still can't recover them: they're leaf routines with no
/// register save/restore, so there's no prologue for the push/pop heuristic to
/// key off of, and they end up folded into a neighboring function's trailing
/// data as raw `.word`s (and any call to them resolves to `_DAT_%08X`).
/// `___ashrsi3`, `___ashlsi3` and `___lshrsi3` in particular share an
/// identical opening (`mov #31,r0 / cmp/hi r0,r5 / bt ...`) since they're all
/// generated from the same dispatch-table template in lib1funcs.asm, so a
/// short anchor can't tell them apart - only their full body (including the
/// per-routine jump table and shift-ladder, which differ byte for byte)
/// disambiguates reliably. These are exactly the bytes `sh-elf-as` produces
/// from `gcc/config/sh/lib1funcs.asm`, extracted once and verified byte for
/// byte against this game's own copies before being hardcoded here.
const LIBGCC_HELPER_SIGNATURES: &[(&str, &[u8])] = &[
    ("___sdivsi3", ASHR_SDIVSI3),
    ("___udivsi3", ASHR_UDIVSI3),
    ("___ashlsi3", ASHR_ASHLSI3),
    ("___ashrsi3", ASHR_ASHRSI3),
    ("___lshrsi3", ASHR_LSHRSI3),
];

const ASHR_SDIVSI3: &[u8] = &[
    0x61, 0x43, 0x60, 0x53, 0x20, 0x08, 0x89, 0x48, 0xe2, 0x00, 0x21, 0x27, 0x33, 0x3a, 0x31, 0x2a,
    0x23, 0x07, 0x41, 0x24, 0x33, 0x04, 0x41, 0x24, 0x33, 0x04, 0x41, 0x24, 0x33, 0x04, 0x41, 0x24,
    0x33, 0x04, 0x41, 0x24, 0x33, 0x04, 0x41, 0x24, 0x33, 0x04, 0x41, 0x24, 0x33, 0x04, 0x41, 0x24,
    0x33, 0x04, 0x41, 0x24, 0x33, 0x04, 0x41, 0x24, 0x33, 0x04, 0x41, 0x24, 0x33, 0x04, 0x41, 0x24,
    0x33, 0x04, 0x41, 0x24, 0x33, 0x04, 0x41, 0x24, 0x33, 0x04, 0x41, 0x24, 0x33, 0x04, 0x41, 0x24,
    0x33, 0x04, 0x41, 0x24, 0x33, 0x04, 0x41, 0x24, 0x33, 0x04, 0x41, 0x24, 0x33, 0x04, 0x41, 0x24,
    0x33, 0x04, 0x41, 0x24, 0x33, 0x04, 0x41, 0x24, 0x33, 0x04, 0x41, 0x24, 0x33, 0x04, 0x41, 0x24,
    0x33, 0x04, 0x41, 0x24, 0x33, 0x04, 0x41, 0x24, 0x33, 0x04, 0x41, 0x24, 0x33, 0x04, 0x41, 0x24,
    0x33, 0x04, 0x41, 0x24, 0x33, 0x04, 0x41, 0x24, 0x33, 0x04, 0x41, 0x24, 0x33, 0x04, 0x41, 0x24,
    0x33, 0x04, 0x41, 0x24, 0x31, 0x2e, 0x00, 0x0b, 0x60, 0x13, 0x00, 0x0b, 0xe0, 0x00,
];

const ASHR_UDIVSI3: &[u8] = &[
    0xe0, 0x00, 0x00, 0x19, 0x44, 0x24, 0x30, 0x54, 0x44, 0x24, 0x30, 0x54, 0x44, 0x24, 0x30, 0x54,
    0x44, 0x24, 0x30, 0x54, 0x44, 0x24, 0x30, 0x54, 0x44, 0x24, 0x30, 0x54, 0x44, 0x24, 0x30, 0x54,
    0x44, 0x24, 0x30, 0x54, 0x44, 0x24, 0x30, 0x54, 0x44, 0x24, 0x30, 0x54, 0x44, 0x24, 0x30, 0x54,
    0x44, 0x24, 0x30, 0x54, 0x44, 0x24, 0x30, 0x54, 0x44, 0x24, 0x30, 0x54, 0x44, 0x24, 0x30, 0x54,
    0x44, 0x24, 0x30, 0x54, 0x44, 0x24, 0x30, 0x54, 0x44, 0x24, 0x30, 0x54, 0x44, 0x24, 0x30, 0x54,
    0x44, 0x24, 0x30, 0x54, 0x44, 0x24, 0x30, 0x54, 0x44, 0x24, 0x30, 0x54, 0x44, 0x24, 0x30, 0x54,
    0x44, 0x24, 0x30, 0x54, 0x44, 0x24, 0x30, 0x54, 0x44, 0x24, 0x30, 0x54, 0x44, 0x24, 0x30, 0x54,
    0x44, 0x24, 0x30, 0x54, 0x44, 0x24, 0x30, 0x54, 0x44, 0x24, 0x30, 0x54, 0x44, 0x24, 0x30, 0x54,
    0x44, 0x24, 0x30, 0x54, 0x44, 0x24, 0x00, 0x0b, 0x60, 0x43,
];

const ASHR_ASHLSI3: &[u8] = &[
    0xe0, 0x1f, 0x35, 0x06, 0x89, 0x40, 0xc7, 0x02, 0x05, 0x5c, 0x30, 0x5c, 0x40, 0x2b, 0x60, 0x43,
    0x7c, 0x2e, 0x24, 0x2c, 0x22, 0x2a, 0x20, 0x28, 0x38, 0x42, 0x36, 0x40, 0x34, 0x3e, 0x32, 0x3c,
    0x4e, 0x58, 0x4c, 0x56, 0x4a, 0x54, 0x48, 0x52, 0x64, 0x70, 0x62, 0x6e, 0x60, 0x6c, 0x5e, 0x6a,
    0x40, 0x08, 0x40, 0x08, 0x00, 0x0b, 0x40, 0x08, 0x40, 0x08, 0x40, 0x08, 0x40, 0x08, 0x00, 0x0b,
    0x40, 0x00, 0x40, 0x08, 0x40, 0x08, 0x40, 0x08, 0x00, 0x0b, 0x40, 0x18, 0x40, 0x08, 0x40, 0x08,
    0x40, 0x08, 0x40, 0x18, 0x00, 0x0b, 0x40, 0x00, 0x40, 0x08, 0x40, 0x08, 0x40, 0x08, 0x00, 0x0b,
    0x40, 0x28, 0x40, 0x08, 0x40, 0x08, 0x40, 0x08, 0x40, 0x28, 0x00, 0x0b, 0x40, 0x00, 0x40, 0x08,
    0x40, 0x08, 0x40, 0x08, 0x40, 0x28, 0x00, 0x0b, 0x40, 0x18, 0x40, 0x08, 0x40, 0x08, 0x40, 0x08,
    0x40, 0x28, 0x40, 0x18, 0x00, 0x0b, 0x40, 0x00, 0x00, 0x0b, 0xe0, 0x00, 0x00, 0x0b, 0x00, 0x09,
];

const ASHR_ASHRSI3: &[u8] = &[
    0xe0, 0x1f, 0x35, 0x06, 0x89, 0x14, 0xc7, 0x02, 0x05, 0x5c, 0x30, 0x5c, 0x40, 0x2b, 0x60, 0x43,
    0x6e, 0x6a, 0x68, 0x66, 0x64, 0x62, 0x60, 0x5e, 0x5c, 0x5a, 0x58, 0x56, 0x54, 0x52, 0x50, 0x4e,
    0x48, 0x46, 0x44, 0x42, 0x40, 0x3e, 0x3c, 0x3a, 0x32, 0x30, 0x2e, 0x2c, 0x2a, 0x28, 0x26, 0x20,
    0x40, 0x24, 0x00, 0x0b, 0x30, 0x0a, 0x40, 0x21, 0x40, 0x21, 0x40, 0x21, 0x40, 0x21, 0x40, 0x21,
    0x40, 0x21, 0x40, 0x29, 0x40, 0x19, 0x00, 0x0b, 0x60, 0x0e, 0x40, 0x21, 0x40, 0x21, 0x40, 0x21,
    0x40, 0x21, 0x40, 0x21, 0x40, 0x21, 0x40, 0x21, 0x40, 0x29, 0x00, 0x0b, 0x60, 0x0f, 0x40, 0x21,
    0x40, 0x21, 0x40, 0x21, 0x40, 0x21, 0x40, 0x21, 0x40, 0x21, 0x40, 0x21, 0x40, 0x21, 0x40, 0x21,
    0x40, 0x21, 0x40, 0x21, 0x40, 0x21, 0x40, 0x21, 0x40, 0x21, 0x00, 0x0b, 0x40, 0x21, 0x00, 0x0b,
    0x00, 0x09,
];

const ASHR_LSHRSI3: &[u8] = &[
    0xe0, 0x1f, 0x35, 0x06, 0x89, 0x40, 0xc7, 0x02, 0x05, 0x5c, 0x30, 0x5c, 0x40, 0x2b, 0x60, 0x43,
    0x7c, 0x2e, 0x24, 0x2c, 0x22, 0x2a, 0x20, 0x28, 0x38, 0x42, 0x36, 0x40, 0x34, 0x3e, 0x32, 0x3c,
    0x4e, 0x58, 0x4c, 0x56, 0x4a, 0x54, 0x48, 0x52, 0x64, 0x70, 0x62, 0x6e, 0x60, 0x6c, 0x5e, 0x6a,
    0x40, 0x09, 0x40, 0x09, 0x00, 0x0b, 0x40, 0x09, 0x40, 0x09, 0x40, 0x09, 0x40, 0x09, 0x00, 0x0b,
    0x40, 0x01, 0x40, 0x09, 0x40, 0x09, 0x40, 0x09, 0x00, 0x0b, 0x40, 0x19, 0x40, 0x09, 0x40, 0x09,
    0x40, 0x09, 0x40, 0x19, 0x00, 0x0b, 0x40, 0x01, 0x40, 0x09, 0x40, 0x09, 0x40, 0x09, 0x00, 0x0b,
    0x40, 0x29, 0x40, 0x09, 0x40, 0x09, 0x40, 0x09, 0x40, 0x29, 0x00, 0x0b, 0x40, 0x01, 0x40, 0x09,
    0x40, 0x09, 0x40, 0x09, 0x40, 0x29, 0x00, 0x0b, 0x40, 0x19, 0x40, 0x09, 0x40, 0x09, 0x40, 0x09,
    0x40, 0x29, 0x40, 0x19, 0x00, 0x0b, 0x40, 0x01, 0x00, 0x0b, 0xe0, 0x00, 0x00, 0x0b, 0x00, 0x09,
];

/// Scan `file_contents` for every exact occurrence of each single-entry
/// libgcc helper's full-body signature and return (address, symbol name)
/// pairs.
fn find_libgcc_helper_symbols(file_contents: &[u8], vram: u32) -> Vec<(u32, String)> {
    let mut symbols = Vec::new();
    for &(name, signature) in LIBGCC_HELPER_SIGNATURES {
        if file_contents.len() < signature.len() {
            continue;
        }
        for i in (0..=file_contents.len() - signature.len()).step_by(2) {
            if &file_contents[i..i + signature.len()] == signature {
                symbols.push((i as u32 + vram, name.to_string()));
            }
        }
    }
    symbols
}

fn segment_symbol_files(config: &Config, segment: &Segment) -> Vec<String> {
    let mut files = vec![format!(
        "{}/{}_user_syms.txt",
        config.options.syms_path, segment.name
    )];
    for name in &config.options.symbol_files {
        files.push(format!("{}/{}", config.options.syms_path, name));
    }
    files
}

/// Load the symbol files, then add a `_func_%08X` entry for every function in
/// this segment.
///
/// The function symbols are recovered in-process rather than read back from the
/// `<segment>_syms.gen.txt` this run is about to write, so output never depends on
/// what the previous run happened to leave on disk. They go in last: a
/// hand-written name always beats the generated placeholder.
fn load_segment_symbols(
    config: &Config,
    segment: &Segment,
    file_contents: &Vec<u8>,
) -> SymbolTable {
    let mut symbols = read_symbol_files(&segment_symbol_files(config, segment));

    // Detected before the `_func_%08X` fallback names below, so a real name from
    // a *_user_syms.txt file (loaded above) still wins, but a detected intrinsic
    // entry point wins over the generic placeholder.
    for (address, name) in find_ashiftrt_r4_symbols(file_contents, segment.vram as u32) {
        symbols.insert(address, Symbol { name, size: None });
    }
    for (address, name) in find_libgcc_helper_symbols(file_contents, segment.vram as u32) {
        symbols.insert(address, Symbol { name, size: None });
    }

    if let Some(subsegments) = &segment.subsegments {
        for subsegment in subsegments {
            let subsegment_type = subsegment
                .segment_type
                .as_ref()
                .map(String::as_str)
                .unwrap_or("Unknown");
            if subsegment_type == "data" || subsegment_type.starts_with('.') {
                continue;
            }
            let subsegment_end = subsegment.end.unwrap_or(file_contents.len() as u64);
            let ranges = collect_function_ranges(
                file_contents,
                subsegment.start,
                subsegment_end,
                segment.vram as u32,
                &subsegment.function_ranges,
            );
            for range in &ranges {
                if !emits_function_symbol(range, config.options.decomp_empty_funcs) {
                    continue;
                }
                let address = range.phys_start + segment.vram as u32;
                symbols.insert(
                    address,
                    Symbol {
                        name: format!("_func_{:08X}", address),
                        size: None,
                    },
                );
            }
        }
    }

    symbols
}

fn handle_segments(file_contents: &Vec<u8>, config: &Config) {
    let mut previous_asm_files = HashSet::new();
    find_asm_files(
        std::path::Path::new(&config.options.asm_path),
        &mut previous_asm_files,
    );
    let mut processed_sections = Vec::<ProcessedSection>::new();
    let mut auto_symbols = BTreeSet::<u32>::new();
    if let Some(segments) = &config.segments {
        for segment in segments {
            let user_symbols = load_segment_symbols(config, segment, file_contents);
            if verbose() {
                println!("Segment Name: {}", segment.name);
                println!("Segment Type: {}", segment.segment_type);
                println!("Segment Start: {}", segment.start);
            }

            if let Some(subsegments) = &segment.subsegments {
                for subsegment in subsegments {
                    let subsegment_start = subsegment.start;
                    let subsegment_type = subsegment
                        .segment_type
                        .as_ref()
                        .unwrap_or(&"Unknown".to_string())
                        .clone();
                    let subsegment_file = subsegment
                        .file
                        .as_ref()
                        .unwrap_or(&"Unknown".to_string())
                        .clone();

                    let subsegment_start = subsegment.start;
                    let subsegment_end = subsegment.end.unwrap_or(file_contents.len() as u64);

                    if verbose() {
                        println!(
                            "subsegment {:08X}-{:08X} {} {}",
                            subsegment_start, subsegment_end, subsegment_type, subsegment_file,
                        );
                    }

                    if subsegment_type == "data" || subsegment_type.starts_with('.') {
                        // just emit words
                        let mut data_str = String::new();
                        for i in (subsegment_start..subsegment_end).step_by(2) {
                            let ii = i as usize;
                            let data: u32 =
                                ((file_contents[ii] as u32) << 8) | file_contents[ii + 1] as u32;
                            data_str.push_str(&format!(
                                "/* 0x{:08X} */ .word 0x{:04X}\n",
                                i + segment.vram,
                                data
                            ));
                        }

                        let processed_section = ProcessedSection {
                            is_code: false,
                            disassembled_funcs: BTreeMap::<u32, DisassembledFunc>::new(),
                            data: data_str,
                            addr: subsegment_start,
                            vaddr: subsegment_start + segment.vram,
                            vbase: segment.vram,
                        };
                        processed_sections.push(processed_section);
                    } else {
                        // find functions and process
                        let disassembled_funcs = handle_code_section(
                            file_contents,
                            subsegment_start,
                            subsegment_end,
                            segment.vram,
                            &user_symbols,
                            &mut auto_symbols,
                            &subsegment.function_ranges,
                            &subsegment.data_ranges,
                        );

                        let processed_section = ProcessedSection {
                            is_code: true,
                            disassembled_funcs: disassembled_funcs,
                            data: "".to_string(),
                            addr: subsegment_start,
                            vaddr: subsegment_start + segment.vram,
                            vbase: segment.vram,
                        };
                        processed_sections.push(processed_section);
                    }
                }
            }
        }
    }

    let mut includes: HashSet<String> = HashSet::new();

    // determine first what has been decompiled
    if let Some(segs) = &config.segments {
        if !segs.is_empty() {
            let segment_name = &segs[0].name;
            let base_addr = &segs[0].vram;
            let path = &config.options.src_path;
            for seg in segs {
                if let Some(subsegments) = &seg.subsegments {
                    for subseg in subsegments {
                        if verbose() {
                            println!("seg {:#?}", subseg);
                        }
                        // // need to check the designated file rather than just the segment
                        // // collect all c files specified in the yaml, then check all of those
                        // // and add to includes

                        if let Some(subseg_file) = &subseg.file {
                            let c_filename = format!("{}/{}.c", path, subseg_file);

                            if verbose() {
                                println!("checking {}", c_filename);
                            }
                            if Path::new(&c_filename).exists() {
                                match find_include_asm_in_c_file(&c_filename) {
                                    Ok(set) => {
                                        if verbose() {
                                            println!("adding {:#?}", set);
                                        }
                                        includes.extend(set)
                                    }
                                    Err(err) => {
                                        eprintln!("Error reading the file: {}", err);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // all the segments are processed, emit files

    std::fs::create_dir_all(&config.options.asm_path).expect("Failed to create directories.");
    std::fs::create_dir_all(&format!("{}/f_nonmat", config.options.asm_path))
        .expect("Failed to create directories.");
    std::fs::create_dir_all(&format!("{}/f_match", config.options.asm_path))
        .expect("Failed to create directories.");
    std::fs::create_dir_all(&format!("{}/data", config.options.asm_path))
        .expect("Failed to create directories.");

    std::fs::create_dir_all(&config.options.ld_scripts_path)
        .expect("Failed to create directories.");
    std::fs::create_dir_all(&config.options.syms_path).expect("Failed to create directories.");
    // Emit all asm, then remove only stale generated assembly from the previous
    // extraction. Other files under asm_path are deliberately preserved.
    let mut emitted_asm_files = HashSet::new();
    for processed_section in &processed_sections {
        if !processed_section.is_code {
            emitted_asm_files.insert(emit_asm_file(
                format!(
                    "{}/data/d{:07X}.s",
                    config.options.asm_path, processed_section.vaddr
                ),
                processed_section.data.clone(),
            ));
        } else {
            for (_addr, df) in &processed_section.disassembled_funcs {
                let func_name = format!("func_{:08X}", df.addr + processed_section.vbase as u32);
                let asm_id = format!("f{:07X}", df.addr + processed_section.vbase as u32);

                if includes.contains(&func_name) || includes.contains(&asm_id) {
                    // this has not been decompiled
                    emitted_asm_files.insert(emit_asm_file(
                        format!(
                            "{}/f_nonmat/f{:07X}.s",
                            config.options.asm_path,
                            df.addr + processed_section.vbase as u32
                        ),
                        df.text.clone(),
                    ));
                } else {
                    // has been decompiled
                    emitted_asm_files.insert(emit_asm_file(
                        format!(
                            "{}/f_match/f{:07X}.s",
                            config.options.asm_path,
                            df.addr + processed_section.vbase as u32
                        ),
                        df.text.clone(),
                    ));
                }
            }
        }
    }
    remove_stale_asm_files(&previous_asm_files, &emitted_asm_files);

    let path = &config.options.src_path;
    let asm_path = &config.options.asm_path;

    std::fs::create_dir_all(path).expect("Failed to create src_path directories.");

    if let Some(segs) = &config.segments {
        if !segs.is_empty() {
            let segment_name = &segs[0].name;
            let base_addr = &segs[0].vram;

            let syms_filename = format!("{}/{}_syms.gen.txt", &config.options.syms_path, segment_name);
            // Buffered rather than written straight out: this file is a linker
            // input for this overlay and every overlay that inherits from it,
            // so it must only be touched when its contents actually change.
            let mut syms_file: Vec<u8> = Vec::new();

            let c_filename = format!("{}/{}.c", path, segment_name);

            // don't overwite the c file if it's already existing
            if !Path::new(&c_filename).exists() {
                write_c_file(config, &path, &segment_name, &asm_path, &processed_sections);
            }

            {
                // The linker script is generated entirely from the YAML so it can be ephemeral.
                let filename = format!("{}/{}.ld", &config.options.ld_scripts_path, segment_name);
                let inputs = linker_inputs(&segs[0]);
                let linker_script = gen_ld_script(
                    segment_name,
                    &format!("{:08X}", base_addr),
                    segs[0].subalign.unwrap_or(2),
                    &config.options.obj_path,
                    config.options.check_layout,
                    &inputs,
                );
                write_if_changed(&filename, &format!("{}\n", linker_script));
            }

            // write symbols
            for processed_section in &processed_sections {
                if !processed_section.is_code {
                } else {
                    for pair in &processed_section.disassembled_funcs {
                        // assume this is a empty function if the size is 8
                        if (pair.1.end - pair.1.addr == 8) && config.options.decomp_empty_funcs {
                        } else {
                            // need _ prefix for name mangling
                            // seems like all asm symbols need _ to be accessible
                            // from C
                            writeln!(
                                &mut syms_file,
                                "_func_{:08X} = 0x{:08X};",
                                pair.1.addr + processed_section.vbase as u32,
                                pair.1.addr + processed_section.vbase as u32
                            )
                            .expect("Failed to write to file.");
                        }
                    }
                }
            }

            // Detected libgcc helper entry points live wherever their bytes
            // physically are (only ever this target's own file_contents), but
            // other overlays call into them too, so the addresses go in the
            // same generated linker script every overlay already inherits -
            // exactly like the _func_%08X entries above.
            let mut intrinsic_symbols: Vec<(u32, String)> =
                find_ashiftrt_r4_symbols(file_contents, *base_addr as u32);
            intrinsic_symbols
                .extend(find_libgcc_helper_symbols(file_contents, *base_addr as u32));
            intrinsic_symbols.sort();
            for (address, name) in &intrinsic_symbols {
                writeln!(&mut syms_file, "{} = 0x{:08X};", name, address)
                    .expect("Failed to write to file.");
            }

            write_if_changed(
                &syms_filename,
                std::str::from_utf8(&syms_file).expect("symbol file is not UTF-8"),
            );

            write_data_symbols(config, segment_name, &auto_symbols);
        }
    }
}

/// Define the generated `_DAT_%08X` names so the references the disassembler
/// emitted actually resolve.
///
/// This is a separate file from `<target>_syms.gen.txt` because every target links
/// its own copy: the names are a pure function of the address, so two targets
/// referencing the same unnamed global independently produce the same name and
/// the duplicate assignments agree. Naming one of these addresses in a
/// `*_user_syms.txt` removes it from here on the next run.
fn write_data_symbols(config: &Config, segment_name: &str, auto_symbols: &BTreeSet<u32>) {
    let filename = format!(
        "{}/{}_data_syms.gen.txt",
        &config.options.syms_path, segment_name
    );
    let mut contents = String::from(
        "/* Generated by saturn-splitter: addresses referenced by this target's\n\
         \x20  assembly that no *_user_syms.txt names yet. Naming one there\n\
         \x20  removes it from this file. */\n",
    );
    for address in auto_symbols {
        contents.push_str(&format!(
            "{} = 0x{:08X};\n",
            auto_symbol_name(*address),
            address
        ));
    }
    write_if_changed(&filename, &contents);
}

use std::env;

fn main() {
    let all_args: Vec<String> = env::args().collect();

    let verbose = all_args.iter().any(|arg| arg == "-v" || arg == "--verbose");
    VERBOSE.store(verbose, Ordering::Relaxed);

    let args: Vec<String> = all_args
        .into_iter()
        .filter(|arg| arg != "-v" && arg != "--verbose")
        .collect();

    if let Some(filename) = args.get(1) {
        if filename == "--find-funcs" {
            if let Some(funcs_filename) = args.get(2) {
                match read_file_to_vec(&funcs_filename) {
                    Ok(file_contents) => {
                        let mut ranges = Vec::<FunctionRange>::new();
                        find_funcs(&file_contents, 0, file_contents.len() as u64, &mut ranges);
                        for range in ranges {
                            // println!("{:08X}-{:08X}", range.phys_start, range.phys_end);
                            println!("0x{:08X},", range.phys_start + 0x06066000);
                        }
                    }
                    Err(error) => {
                        eprintln!("Error: {:?} {}", error, funcs_filename);
                    }
                }
            }
            return;
        }
        println!("Reading: {}", filename);
        let config = parse_yaml2(filename.to_string());

        match read_file_to_vec(&config.options.target_path) {
            Ok(file_contents) => {
                handle_segments(&file_contents, &config);
            }
            Err(error) => {
                eprintln!("Error: {:?} {}", error, config.options.target_path);
            }
        }
    }
}

use std::fs;
use std::path::PathBuf;
use std::process::Command;
use tempfile::NamedTempFile;

fn assemble(input: &str) -> Result<Vec<u8>, Box<dyn std::error::Error>> {
    // Create a temporary file and write the assembly code to it
    let mut asm_file = NamedTempFile::new()?;
    asm_file.write_all(input.as_bytes())?;

    // Create a temporary file for the output
    let output_file = NamedTempFile::new()?;

    // assemble and dump as binary
    let cmd_str = format!(
        "sh-elf-as -o /work/{} /work/{} && sh-elf-objcopy -O binary /work/{} /work/{}",
        output_file.path().file_name().unwrap().to_string_lossy(),
        asm_file.path().file_name().unwrap().to_string_lossy(),
        output_file.path().file_name().unwrap().to_string_lossy(),
        output_file.path().file_name().unwrap().to_string_lossy(),
    );

    let output = Command::new("docker")
        .args(&[
            "run",
            "-v",
            &format!(
                "{}:/work",
                output_file.path().parent().unwrap().to_string_lossy()
            ),
            "binutils-sh-elf",
            "/bin/sh",
            "-c",
            &cmd_str,
        ])
        .output()?;

    // Print stdout
    if !output.stdout.is_empty() {
        println!("{}", String::from_utf8_lossy(&output.stdout));
    }

    // Print stderr
    if !output.stderr.is_empty() {
        eprintln!("{}", String::from_utf8_lossy(&output.stderr));
    }

    // Read the output file into a byte vector
    let binary = std::fs::read(output_file.path())?;

    Ok(binary)
}

use similar::{ChangeTag, TextDiff};

fn print_diff(expected_lines: String, actual_lines: String) {
    let diff = TextDiff::from_lines(&expected_lines, &actual_lines);

    for diff in diff.iter_all_changes() {
        match diff.tag() {
            ChangeTag::Delete => print!("\x1b[31m{}\x1b[0m", diff),
            ChangeTag::Insert => print!("\x1b[32m{}\x1b[0m", diff),
            ChangeTag::Equal => print!("{}", diff),
        }
    }
    println!();
}

fn asm_test_case(asm: String, expected: String, virtual_base_addr: u64) {
    let output = assemble(&asm).unwrap();

    println!("output: {:?} ", output);

    let mut data_labels = HashMap::<u32, DataLabel>::new();
    let mut branch_labels = HashMap::<u32, String>::new();

    let mut output_string = String::new();

    let disassembled_funcs = handle_code_section(
        &output,
        0,
        output.len().try_into().unwrap(),
        virtual_base_addr,
        &SymbolTable::default(),
        &mut BTreeSet::new(),
        &[],
        &[],
    );

    let trimmed_right: String = expected
        .lines()
        .map(|line| line.trim())
        .collect::<Vec<_>>()
        .join("\n");

    if disassembled_funcs[&8].text != trimmed_right {
        let actual_lines = disassembled_funcs[&8].text.clone();
        let expected_lines = trimmed_right;

        print_diff(expected_lines, actual_lines);
        assert!(false);
    }
}

#[derive(Debug, PartialEq)]
struct LinkerInput {
    start: u64,
    object: String,
    section: String,
}

fn linker_inputs(segment: &Segment) -> Vec<LinkerInput> {
    let mut seen = HashSet::new();
    let mut inputs = Vec::new();
    let mut legacy_text_starts = HashMap::new();
    let mut legacy_c_files = HashSet::new();

    if let Some(subsegments) = &segment.subsegments {
        for subsegment in subsegments {
            if matches!(subsegment.segment_type.as_deref(), Some("data") | Some("c")) {
                if let Some(file) = &subsegment.file {
                    legacy_text_starts
                        .entry(file.clone())
                        .and_modify(|start: &mut u64| *start = (*start).min(subsegment.start))
                        .or_insert(subsegment.start);
                    if subsegment.segment_type.as_deref() == Some("c") {
                        legacy_c_files.insert(file.clone());
                    }
                }
            }
        }
        for subsegment in subsegments {
            let Some(file) = &subsegment.file else {
                continue;
            };
            let section = match subsegment.segment_type.as_deref() {
                Some("c") => ".text",
                Some("data") if !legacy_c_files.contains(file) => ".text",
                Some(".text") => ".text",
                Some(".data") => ".data",
                Some(".rodata") => ".rodata",
                Some(".bss") => ".bss",
                Some(".sbss") => ".sbss",
                _ => continue,
            };
            if seen.insert((file.clone(), section)) {
                inputs.push(LinkerInput {
                    start: if matches!(subsegment.segment_type.as_deref(), Some("c") | Some("data"))
                    {
                        legacy_text_starts[file]
                    } else {
                        subsegment.start
                    },
                    object: format!("{}.o", file),
                    section: section.to_string(),
                });
            }
        }
    }

    inputs
}

fn gen_ld_script(
    zero_prefix: &str,
    addr: &str,
    subalign: u64,
    obj_path: &str,
    check_layout: bool,
    inputs: &[LinkerInput],
) -> String {
    let mut code = String::new();

    code.push_str("SECTIONS\n{\n");
    code.push_str("    __romPos = 0;\n");
    code.push_str("    _gp = 0x0;\n");
    code.push_str(&format!("    {}_ROM_START = __romPos;\n", zero_prefix));
    code.push_str(&format!(
        "    {}_VRAM = ADDR(.{});\n",
        zero_prefix, zero_prefix
    ));
    code.push_str(&format!(
        "    .{} 0x{} : AT({}_ROM_START) SUBALIGN({})\n    {{\n",
        zero_prefix, addr, zero_prefix, subalign
    ));
    code.push_str(&format!("        {}_TEXT_START = .;\n", zero_prefix));
    for input in inputs {
        if check_layout {
            code.push_str(&format!(
                "        ASSERT(. - ADDR(.{}) == 0x{:X}, \"{} {} starts at the wrong offset\");\n",
                zero_prefix, input.start, input.object, input.section,
            ));
        }
        let path = if obj_path.is_empty() {
            input.object.clone()
        } else {
            format!("{}/{}", obj_path.trim_end_matches('/'), input.object)
        };
        code.push_str(&format!("        {}({});\n", path, input.section));
    }
    code.push_str(&format!("        {}_TEXT_END = .;\n", zero_prefix));
    code.push_str(&format!(
        "        {}_TEXT_SIZE = ABSOLUTE({}_TEXT_END - {}_TEXT_START);\n    }}\n",
        zero_prefix, zero_prefix, zero_prefix
    ));
    code.push_str(&format!("    __romPos += SIZEOF(.{});\n", zero_prefix));
    code.push_str("    __romPos = ALIGN(__romPos, 16);\n");
    code.push_str(&format!("    {}_ROM_END = __romPos;\n", zero_prefix));
    code.push_str(&format!("    {}_VRAM_END = .;\n", zero_prefix));
    code.push_str("\n    /DISCARD/ :\n    {\n");
    code.push_str("        *(*);\n    }\n");
    code.push_str("}");

    code
}

#[cfg(test)]
mod tests {
    use super::*;

    fn words_bytes(words: &[u16]) -> Vec<u8> {
        words.iter().flat_map(|word| word.to_be_bytes()).collect()
    }

    #[test]
    fn test_asm_emission_preserves_unchanged_files_and_removes_stale_files() {
        let output = tempfile::tempdir().unwrap();
        let current = output.path().join("current.s");
        let stale = output.path().join("stale.s");
        let preserved = output.path().join("notes.txt");
        std::fs::write(&current, "current\n").unwrap();
        std::fs::write(&stale, "stale").unwrap();
        std::fs::write(&preserved, "keep").unwrap();

        let mut previous = HashSet::new();
        find_asm_files(output.path(), &mut previous);
        let modified_before = std::fs::metadata(&current).unwrap().modified().unwrap();
        std::thread::sleep(std::time::Duration::from_millis(1100));

        let emitted = HashSet::from([emit_asm_file(
            current.to_string_lossy().into_owned(),
            "current".to_string(),
        )]);
        remove_stale_asm_files(&previous, &emitted);

        let modified_after = std::fs::metadata(&current).unwrap().modified().unwrap();
        assert_eq!(modified_before, modified_after);
        assert_eq!(std::fs::read_to_string(&current).unwrap(), "current\n");
        assert!(!stale.exists());
        assert!(preserved.exists());
    }

    #[test]
    fn test_include_asm_records_address_and_semantic_name() {
        let mut source = NamedTempFile::new().unwrap();
        writeln!(
            source,
            "INCLUDE_ASM(\"asm/saturn/game/f_nonmat\", f6070A60, RunMainEngine);"
        )
        .unwrap();

        let includes = find_include_asm_in_c_file(source.path().to_str().unwrap()).unwrap();

        assert!(includes.contains("f6070A60"));
        assert!(includes.contains("RunMainEngine"));
    }

    #[test]
    fn test_find_funcs_ignores_rts_inside_referenced_long_literal() {
        let bytes = words_bytes(&[
            0x0009, 0x0009, 0x2f86, 0xd102, 0x0009, 0x0009, 0x0009, 0x0009, 0xf000, 0x000b, 0x0009,
            0x000b, 0x68f6,
        ]);
        let mut ranges = Vec::new();

        find_funcs(&bytes, 0, bytes.len() as u64, &mut ranges);

        assert_eq!(ranges.len(), 1);
        assert_eq!(ranges[0].phys_start, 4);
        assert_eq!(ranges[0].phys_end, 24);
    }

    #[test]
    fn test_find_funcs_does_not_ignore_unreferenced_rts_pattern() {
        let bytes = words_bytes(&[0x0009, 0x0009, 0x2f86, 0x0009, 0xf000, 0x000b, 0x68f6]);
        let mut ranges = Vec::new();

        find_funcs(&bytes, 0, bytes.len() as u64, &mut ranges);

        assert_eq!(ranges.len(), 1);
        assert_eq!(ranges[0].phys_start, 4);
        assert_eq!(ranges[0].phys_end, 12);
    }

    #[test]
    fn test_find_funcs_keeps_rts_at_start_of_long_target() {
        let bytes = words_bytes(&[
            0x0009, 0x0009, 0x2f86, 0xd102, 0x0009, 0x0009, 0x0009, 0x0009, 0x000b, 0x1234,
        ]);
        let mut ranges = Vec::new();

        find_funcs(&bytes, 0, bytes.len() as u64, &mut ranges);

        assert_eq!(ranges.len(), 1);
        assert_eq!(ranges[0].phys_start, 4);
        assert_eq!(ranges[0].phys_end, 18);
    }

    #[test]
    fn test_find_funcs_keeps_rts_targeted_by_word_pattern() {
        let bytes = words_bytes(&[
            0x0009, 0x0009, 0x2f86, 0x9303, 0x0009, 0x0009, 0x0009, 0x0009, 0x000b, 0x1234,
        ]);
        let mut ranges = Vec::new();

        find_funcs(&bytes, 0, bytes.len() as u64, &mut ranges);

        assert_eq!(ranges.len(), 1);
        assert_eq!(ranges[0].phys_start, 4);
        assert_eq!(ranges[0].phys_end, 18);
    }

    #[test]
    fn test_find_funcs_prefers_full_prologue_over_internal_push() {
        let bytes = words_bytes(&[
            0x0009, 0x0009, 0x2f86, 0x2f96, 0x2fa6, 0xd102, 0x0009, 0x0009, 0x0009, 0x0009, 0xf000,
            0x000b, 0x2fd6, 0x0009, 0x000b, 0x68f6,
        ]);
        let mut ranges = Vec::new();

        find_funcs(&bytes, 0, bytes.len() as u64, &mut ranges);

        assert_eq!(ranges.len(), 1);
        assert_eq!(ranges[0].phys_start, 4);
    }

    #[test]
    fn test_ld_script() {
        let expected = r#"SECTIONS
{
    __romPos = 0;
    _gp = 0x0;
    zero_ROM_START = __romPos;
    zero_VRAM = ADDR(.zero);
    .zero 0x06004080 : AT(zero_ROM_START) SUBALIGN(2)
    {
        zero_TEXT_START = .;
        build/saturn/zero.o(.text);
        zero_TEXT_END = .;
        zero_TEXT_SIZE = ABSOLUTE(zero_TEXT_END - zero_TEXT_START);
    }
    __romPos += SIZEOF(.zero);
    __romPos = ALIGN(__romPos, 16);
    zero_ROM_END = __romPos;
    zero_VRAM_END = .;

    /DISCARD/ :
    {
        *(*);
    }
}"#;

        let actual = gen_ld_script(
            "zero",
            "06004080",
            2,
            "build/saturn",
            false,
            &[LinkerInput {
                start: 0,
                object: "zero.o".to_string(),
                section: ".text".to_string(),
            }],
        );
        print_diff(expected.to_string(), actual.clone());
        assert!(expected == actual);
    }

    #[test]
    fn test_linker_inputs_are_ordered_and_deduplicated() {
        let segment = Segment {
            name: "zero".to_string(),
            segment_type: "code".to_string(),
            start: 0,
            vram: 0x06004080,
            subalign: Some(4),
            subsegments: Some(vec![
                Subsegment {
                    start: 8,
                    end: Some(8),
                    segment_type: Some("data".to_string()),
                    file: Some("zero".to_string()),
                    function_ranges: Vec::new(),
                    data_ranges: Vec::new(),
                },
                Subsegment {
                    start: 0,
                    end: Some(16),
                    segment_type: Some("c".to_string()),
                    file: Some("zero".to_string()),
                    function_ranges: Vec::new(),
                    data_ranges: Vec::new(),
                },
                Subsegment {
                    start: 16,
                    end: Some(24),
                    segment_type: Some("c".to_string()),
                    file: Some("lib/spr/spr_1c".to_string()),
                    function_ranges: Vec::new(),
                    data_ranges: Vec::new(),
                },
                Subsegment {
                    start: 24,
                    end: Some(32),
                    segment_type: Some("c".to_string()),
                    file: Some("zero".to_string()),
                    function_ranges: Vec::new(),
                    data_ranges: Vec::new(),
                },
            ]),
        };

        assert_eq!(
            linker_inputs(&segment),
            vec![
                LinkerInput {
                    start: 0,
                    object: "zero.o".to_string(),
                    section: ".text".to_string(),
                },
                LinkerInput {
                    start: 16,
                    object: "lib/spr/spr_1c.o".to_string(),
                    section: ".text".to_string(),
                },
            ]
        );
    }

    #[test]
    fn test_splat_style_named_sections_generate_in_yaml_order() {
        let yaml = r#"
options:
  target_path: fixture.bin
  asm_path: asm
  src_path: src
  obj_path: build
  ld_scripts_path: build
  syms_path: build
  check_layout: true
  decomp_empty_funcs: false
segments:
  - name: fixture
    type: code
    start: 0
    vram: 0x06010000
    subalign: 2
    subsegments:
      - [0x0, .data, header]
      - [0x8, c, main]
      - [0x20, .data, animations]
      - [0x28, .rodata, tables]
      - start: 0x30
        type: data
        file: raw_tail
"#;
        let config: Config = serde_yaml::from_str(yaml).unwrap();
        let segment = &config.segments.as_ref().unwrap()[0];
        let inputs = linker_inputs(segment);

        assert_eq!(
            inputs,
            vec![
                LinkerInput {
                    start: 0,
                    object: "header.o".to_string(),
                    section: ".data".to_string()
                },
                LinkerInput {
                    start: 8,
                    object: "main.o".to_string(),
                    section: ".text".to_string()
                },
                LinkerInput {
                    start: 0x20,
                    object: "animations.o".to_string(),
                    section: ".data".to_string()
                },
                LinkerInput {
                    start: 0x28,
                    object: "tables.o".to_string(),
                    section: ".rodata".to_string()
                },
                LinkerInput {
                    start: 0x30,
                    object: "raw_tail.o".to_string(),
                    section: ".text".to_string()
                },
            ]
        );

        let script = gen_ld_script("fixture", "06010000", 2, "build", true, &inputs);
        assert!(script.contains("ASSERT(. - ADDR(.fixture) == 0x0"));
        assert!(script.contains("build/header.o(.data);"));
        assert!(script.contains("ASSERT(. - ADDR(.fixture) == 0x8"));
        assert!(script.contains("build/main.o(.text);"));
        assert!(script.contains("ASSERT(. - ADDR(.fixture) == 0x20"));
        assert!(script.contains("build/animations.o(.data);"));
        assert!(script.contains("ASSERT(. - ADDR(.fixture) == 0x28"));
        assert!(script.contains("build/tables.o(.rodata);"));
        assert!(script.contains("ASSERT(. - ADDR(.fixture) == 0x30"));
        assert!(script.contains("build/raw_tail.o(.text);"));
    }

    fn test_base_mov_l(expected: String, base: u64) {
        let asm = r#"
        mov.l r8, @-r15
        mov r0, r1
        rts
        nop
    
        mov.l r8, @-r15
        mov.l      @(dtest-.,pc),r0
        mov.w      @(wtest-.,pc),r1
        rts
        nop
        .align 2
        dtest:
        .long 0xdeadbeef
        .align 2
        wtest:
        .word 0xface
        "#;

        asm_test_case(asm.to_string(), expected.to_string(), base);
    }

    // mov.l      @(dtest-.,pc),r0
    // is apparently the new version of @(dtest,pc), r0
    // see https://github.com/bminor/binutils-gdb/commit/9691d64f9a558a599867a6528db1908e4c5bc63f
    #[test]
    fn check_mov_l_pc_0() {
        // TODO mov.w/mov.l should emit a label in addition to labeling as
        // data
        // let expected = r#"glabel func_00000008
        // /* 0x00000008 0x2F86 */ mov.l r8, @-r15
        // /* 0x0000000A 0xD002 */ mov.l @(0x00A, pc), r0
        // /* 0x0000000C 0x9104 */ mov.w @(0x00C, pc), r1
        // /* 0x0000000E 0x000B */ rts
        // /* 0x00000010 0x0009 */ nop
        // /* 0x00000012 */ .word 0x0009
        // /* 0x00000014 */ .word 0xDEAD
        // /* 0x00000016 */ .word 0xBEEF
        // /* 0x00000018 */ .word 0xFACE
        // /* 0x0000001A */ .word 0x0009
        // "#;
        let expected = r#"glabel func_00000008
                        /* 0x00000008 0x2F86 */ mov.l r8, @-r15
                        /* 0x0000000A 0xD002 */ mov.l @(dat_00000014, pc), r0
                        /* 0x0000000C 0x9104 */ mov.w @(dat_00000018, pc), r1
                        /* 0x0000000E 0x000B */ rts
                        /* 0x00000010 0x0009 */ nop
                        /* 0x00000012 */ .word 0x0009
                        dat_00000014: /* source: 0000000A */
                        /* 00000014 */ .long 0xDEADBEEF
                        dat_00000018: /* source: 0000000C */
                        .word 0xFACE
                        /* 0x0000001A */ .word 0x0009
                        "#;
        test_base_mov_l(expected.to_string(), 0);
    }

    #[test]
    fn check_mov_l_pc_1000() {
        // TODO mov.w/mov.l should emit a label in addition to labeling as
        // data
        let expected = r#"glabel func_00001008
        /* 0x00001008 0x2F86 */ mov.l r8, @-r15
        /* 0x0000100A 0xD002 */ mov.l @(dat_00001014, pc), r0
        /* 0x0000100C 0x9104 */ mov.w @(dat_00001018, pc), r1
        /* 0x0000100E 0x000B */ rts
        /* 0x00001010 0x0009 */ nop
        /* 0x00001012 */ .word 0x0009
        dat_00001014: /* source: 0000100A */
        /* 00001014 */ .long 0xDEADBEEF
        dat_00001018: /* source: 0000100C */
        .word 0xFACE
        /* 0x0000101A */ .word 0x0009
        "#;

        test_base_mov_l(expected.to_string(), 0x1000);
    }

    fn test_base(expected: String, base: u64) {
        let asm = r#"
        mov.l r8, @-r15
        mov r0, r1
        rts
        nop
    
        mov.l r8, @-r15
        mov r0, r1
        bra label
        nop
        label:
        mov r1, r0
        rts
        nop
        "#;
        asm_test_case(asm.to_string(), expected.to_string(), base);
    }

    #[test]
    fn do_asm_0() {
        let expected_0 = r#"glabel func_00000008
        /* 0x00000008 0x2F86 */ mov.l r8, @-r15
        /* 0x0000000A 0x6103 */ mov r0, r1
        /* 0x0000000C 0xA000 */ bra lab_00000010
        /* 0x0000000E 0x0009 */ nop
        lab_00000010:
        /* 0x00000010 0x6013 */ mov r1, r0
        /* 0x00000012 0x000B */ rts
        /* 0x00000014 0x0009 */ nop
        "#;
        test_base(expected_0.to_string(), 0);
    }

    #[test]
    fn do_asm_1000() {
        let expected = r#"glabel func_00001008
        /* 0x00001008 0x2F86 */ mov.l r8, @-r15
        /* 0x0000100A 0x6103 */ mov r0, r1
        /* 0x0000100C 0xA000 */ bra lab_00001010
        /* 0x0000100E 0x0009 */ nop
        lab_00001010:
        /* 0x00001010 0x6013 */ mov r1, r0
        /* 0x00001012 0x000B */ rts
        /* 0x00001014 0x0009 */ nop
        "#;
        test_base(expected.to_string(), 0x1000);
    }

    #[test]
    fn test_sts_l() {
        let mut string = String::new();
        let mut data_labels = HashMap::<u32, DataLabel>::new();
        let mut branch_labels = HashMap::<u32, String>::new();
        sh2_disasm(
            0,
            0x4f22,
            true,
            &mut string,
            &mut data_labels,
            &mut branch_labels,
        );
        assert_eq!(string, "sts.l pr, @-r15");
    }

    #[test]
    fn test_mov() {
        let mut string = String::new();
        let mut data_labels = HashMap::<u32, DataLabel>::new();
        let mut branch_labels = HashMap::<u32, String>::new();
        sh2_disasm(
            0,
            0x936e,
            true,
            &mut string,
            &mut data_labels,
            &mut branch_labels,
        );
        assert_eq!(string, "mov.w @(0x0E0,pc),r3");
    }

    #[test]
    fn test_mov_l() {
        let mut string = String::new();
        let mut data_labels = HashMap::<u32, DataLabel>::new();
        let mut branch_labels = HashMap::<u32, String>::new();
        sh2_disasm(
            0x7a,
            0xd637,
            true,
            &mut string,
            &mut data_labels,
            &mut branch_labels,
        );
        assert_eq!(string, "mov.l @(0x0DE,pc),r6");
    }

    #[test]
    fn test_sts_mach() {
        let mut string = String::new();
        let mut data_labels = HashMap::<u32, DataLabel>::new();
        let mut branch_labels = HashMap::<u32, String>::new();
        sh2_disasm(
            0x7a,
            0x010a,
            true,
            &mut string,
            &mut data_labels,
            &mut branch_labels,
        );
        assert_eq!(string, "sts mach, r1");
    }

    #[test]
    fn test_sts_macl() {
        let mut string = String::new();
        let mut data_labels = HashMap::<u32, DataLabel>::new();
        let mut branch_labels = HashMap::<u32, String>::new();
        sh2_disasm(
            0x7a,
            0x031a,
            true,
            &mut string,
            &mut data_labels,
            &mut branch_labels,
        );
        assert_eq!(string, "sts macl, r3");
    }

    #[test]
    fn test_mov_l_r1() {
        let mut string = String::new();
        let mut data_labels = HashMap::<u32, DataLabel>::new();
        let mut branch_labels = HashMap::<u32, String>::new();
        sh2_disasm(
            0x7a,
            0x0916,
            true,
            &mut string,
            &mut data_labels,
            &mut branch_labels,
        );
        assert_eq!(string, "mov.l r1,@(r0,r9)");
    }

    #[test]
    fn test_stc_gbr() {
        let mut string = String::new();
        let mut data_labels = HashMap::<u32, DataLabel>::new();
        let mut branch_labels = HashMap::<u32, String>::new();
        sh2_disasm(
            0x7a,
            0x0012,
            true,
            &mut string,
            &mut data_labels,
            &mut branch_labels,
        );
        assert_eq!(string, "stc gbr, r0");
    }

    #[test]
    fn test_mov_w_r() {
        let mut string = String::new();
        let mut data_labels = HashMap::<u32, DataLabel>::new();
        let mut branch_labels = HashMap::<u32, String>::new();
        sh2_disasm(
            0x7a,
            0x0e15,
            true,
            &mut string,
            &mut data_labels,
            &mut branch_labels,
        );
        assert_eq!(string, "mov.w r1,@(r0,r14)");
    }

    #[test]
    fn test_8450() {
        let mut string = String::new();
        let mut data_labels = HashMap::<u32, DataLabel>::new();
        let mut branch_labels = HashMap::<u32, String>::new();
        sh2_disasm(
            0x7a,
            0x8450,
            true,
            &mut string,
            &mut data_labels,
            &mut branch_labels,
        );
        assert_eq!(string, "mov.b @(0x000,r5),r0");
    }

    #[test]
    fn test_8550() {
        let mut string = String::new();
        let mut data_labels = HashMap::<u32, DataLabel>::new();
        let mut branch_labels = HashMap::<u32, String>::new();
        sh2_disasm(
            0x7a,
            0x8550,
            true,
            &mut string,
            &mut data_labels,
            &mut branch_labels,
        );
        assert_eq!(string, "mov.w @(0x000,r5),r0");
    }

    #[test]
    fn test_6143() {
        let mut string = String::new();
        let mut data_labels = HashMap::<u32, DataLabel>::new();
        let mut branch_labels = HashMap::<u32, String>::new();
        sh2_disasm(
            0x7a,
            0x6143,
            true,
            &mut string,
            &mut data_labels,
            &mut branch_labels,
        );
        assert_eq!(string, "mov r4, r1");
    }

    #[test]
    fn test_01ff() {
        let mut string = String::new();
        let mut data_labels = HashMap::<u32, DataLabel>::new();
        let mut branch_labels = HashMap::<u32, String>::new();
        sh2_disasm(
            0x7a,
            0x01ff,
            true,
            &mut string,
            &mut data_labels,
            &mut branch_labels,
        );
        assert_eq!(string, "mac.l @r15+, @r1+");
    }

    #[test]
    fn test_parse_tt_000_yaml() {
        let config = parse_yaml2("./config.yaml".to_string());

        let segments = config.segments.expect("Missing segments");

        assert_eq!(segments.len(), 1);

        let seg = &segments[0];
        assert_eq!(seg.name, "tt_000");
        assert_eq!(seg.segment_type, "code");
        assert_eq!(seg.start, 0);
        assert_eq!(seg.vram, 0x80170000);

        let subsegments = seg.subsegments.as_ref().unwrap();
        assert_eq!(subsegments.len(), 3);

        assert_eq!(subsegments[0].start, 0x0);
        assert_eq!(subsegments[0].end, Some(0x5F));
        assert_eq!(subsegments[0].segment_type.as_deref(), Some("data"));

        assert_eq!(subsegments[1].start, 0x60);
        assert_eq!(subsegments[1].end, Some(0x2857));
        assert_eq!(subsegments[1].segment_type.as_deref(), Some("c"));

        assert_eq!(subsegments[2].start, 0x2858);
        assert_eq!(subsegments[2].end, Some(0x7000));
        assert_eq!(subsegments[2].segment_type.as_deref(), Some("data"));
    }

    #[test]
    fn test_find_funcs_rejects_epilogue_tail_as_prologue() {
        let words = [
            0x2f16, // mov.l r1,@-r15: temporary call save, not a prologue
            0xd107, 0x6763, 0x410b, 0xe400, 0x6fe3, 0x4f26, 0x000b, 0x6ef6,
        ];
        let mut bytes = Vec::new();
        for word in words {
            bytes.push((word >> 8) as u8);
            bytes.push(word as u8);
        }

        let mut ranges = Vec::new();
        find_funcs(&bytes, 0, bytes.len() as u64, &mut ranges);

        assert!(ranges.is_empty());
    }

    #[test]
    fn test_function_entry_hint_recovers_ambiguous_prologue() {
        let bytes = words_bytes(&[
            0x0009, 0x2f86, 0x2fe6, 0x4f22, 0x2f06, 0x0009, 0x6fe3, 0x4f26, 0x6ef6, 0x000b, 0x68f6,
        ]);

        let funcs = handle_code_section(
            &bytes,
            0,
            bytes.len() as u64,
            0,
            &SymbolTable::default(),
            &mut BTreeSet::new(),
            &[[2, 20]],
            &[],
        );

        assert!(funcs.contains_key(&2));
        assert!(funcs[&2].text.contains("glabel func_00000002"));
        assert!(funcs[&2].text.contains("/* 0x00000014 0x68F6 */"));
    }

    #[test]
    fn test_forced_function_range_replaces_nested_auto_range() {
        let bytes = words_bytes(&[0x0009, 0x2f86, 0x0009, 0x000b, 0x68f6]);

        let funcs = handle_code_section(
            &bytes,
            0,
            bytes.len() as u64,
            0x06000000,
            &SymbolTable::default(),
            &mut BTreeSet::new(),
            &[[0, 8]],
            &[],
        );

        assert!(funcs.contains_key(&0));
        assert!(!funcs.contains_key(&2));
    }

    #[test]
    fn test_subsegment_deserializes_forced_ranges() {
        let subsegment: Subsegment = serde_yaml::from_str(
            "start: 0x100\nend: 0x1ff\ntype: c\nfile: test\nfunction_ranges:\n  - [0x120, 0x180]\ndata_ranges:\n  - [0x140, 0x15f]\n",
        )
        .unwrap();

        assert_eq!(subsegment.function_ranges, vec![[0x120, 0x180]]);
        assert_eq!(subsegment.data_ranges, vec![[0x140, 0x15f]]);
    }

    #[test]
    fn test_forced_data_range_is_emitted_as_words_inside_function() {
        let bytes = words_bytes(&[0x2fe6, 0xafff, 0x0009, 0x1234, 0x5678, 0x000b, 0x0009]);

        let funcs = handle_code_section(
            &bytes,
            0,
            bytes.len() as u64,
            0x06000000,
            &SymbolTable::default(),
            &mut BTreeSet::new(),
            &[[0, 12]],
            &[[6, 9]],
        );

        assert!(funcs[&0].text.contains("/* 0x06000006 */ .word 0x1234"));
        assert!(funcs[&0].text.contains("/* 0x06000008 */ .word 0x5678"));
    }

    #[test]
    fn test_far_jump_to_intra_function_target_uses_local_label() {
        // push r1 / mov.l @(pc,disp),r1 / jmp @r1 / mov.l @r15+,r1 (delay slot)
        // / unreachable padding / <target>: mov #0,r0 / rts
        // / .long <target address>
        let bytes = words_bytes(&[
            0x2f16, 0xd103, 0x412b, 0x61f6, 0x0009, 0x0009, 0xe000, 0x000b, 0x0600, 0x000c,
        ]);

        let funcs = handle_code_section(
            &bytes,
            0,
            bytes.len() as u64,
            0x06000000,
            &SymbolTable::default(),
            &mut BTreeSet::new(),
            &[[0, bytes.len() as u32 - 1]],
            &[],
        );

        assert!(funcs[&0].text.contains(".L0600000C:"));
        assert!(funcs[&0].text.contains("/* 06000010 */ .long .L0600000C"));
        assert!(!funcs[&0].text.contains("_DAT_0600000C"));
    }

    #[test]
    fn test_find_ashiftrt_r4_symbols_names_every_ladder_entry() {
        // The real ___ashiftrt_r4 ladder bytes, as emitted by `sh-elf-as` from
        // gcc/config/sh/lib1funcs.asm and verified against this game's own
        // copy at 0x06030E18.
        const LADDER_BYTES: &[u8] = &[
            0x44, 0x24, 0x00, 0x0b, 0x34, 0x4a, 0x44, 0x21, 0x44, 0x21, 0x44, 0x21, 0x44, 0x21, 0x44,
            0x21, 0x44, 0x21, 0x44, 0x29, 0x44, 0x19, 0x00, 0x0b, 0x64, 0x4e, 0x44, 0x21, 0x44, 0x21,
            0x44, 0x21, 0x44, 0x21, 0x44, 0x21, 0x44, 0x21, 0x44, 0x21, 0x44, 0x29, 0x00, 0x0b, 0x64,
            0x4f, 0x44, 0x21, 0x44, 0x21, 0x44, 0x21, 0x44, 0x21, 0x44, 0x21, 0x44, 0x21, 0x44, 0x21,
            0x44, 0x21, 0x44, 0x21, 0x44, 0x21, 0x44, 0x21, 0x44, 0x21, 0x44, 0x21, 0x44, 0x21, 0x00,
            0x0b, 0x44, 0x21, 0x00, 0x0b, 0x00, 0x09,
        ];
        let mut bytes = vec![0x00u8; 0x10];
        bytes.extend_from_slice(LADDER_BYTES);
        let symbols = find_ashiftrt_r4_symbols(&bytes, 0x06000000);

        assert_eq!(symbols.len(), ASHIFTRT_R4_LADDER.len());
        assert!(symbols.contains(&(0x06000010, "___ashiftrt_r4_32".to_string())));
        assert!(symbols.contains(&(0x06000010, "___ashiftrt_r4_31".to_string())));
        // Verified against this game's own copy at 0x06030E18: shift-by-6 lands
        // 0x40 bytes past the anchor.
        assert!(symbols.contains(&(0x06000050, "___ashiftrt_r4_6".to_string())));
    }

    #[test]
    fn test_find_libgcc_helper_symbols_disambiguates_shared_prefix() {
        // ___ashrsi3, ___ashlsi3 and ___lshrsi3 share an identical opening, so
        // this exercises that the whole-body signature still tells them apart
        // instead of only matching the first one in the table.
        let mut bytes = vec![0x00u8; 4];
        bytes.extend_from_slice(ASHR_LSHRSI3);
        let symbols = find_libgcc_helper_symbols(&bytes, 0x06000000);

        assert_eq!(symbols, vec![(0x06000004, "___lshrsi3".to_string())]);
    }

    #[test]
    fn test_canonical_helper_name_labels_the_function_body() {
        let bytes = words_bytes(&[0xe001, 0x000b, 0x0009]);
        let mut symbols = SymbolTable::default();
        symbols.insert(
            0x06000000,
            Symbol {
                name: "___ashiftrt_r4_20".to_string(),
                size: None,
            },
        );
        let funcs = handle_code_section(
            &bytes,
            0,
            bytes.len() as u64,
            0x06000000,
            &symbols,
            &mut BTreeSet::new(),
            &[[0, 0]],
            &[],
        );

        assert!(funcs[&0].text.starts_with("glabel ___ashiftrt_r4_20\n"));
        assert!(!funcs[&0].text.contains("glabel func_06000000"));
    }

    #[test]
    fn test_mova_jump_table() {
        let mut bytes = vec![0u8; 0x40];
        let words = [
            0xe107, 0x3216, 0x6123, 0x311c, 0xc702, 0x011d, 0x301c, 0x402b, 0x0009, 0x0009, 0x0010,
            0x0012, 0x0014, 0x0016, 0x0018, 0x001a, 0x001c, 0x001e,
        ];
        for (index, word) in words.iter().enumerate() {
            bytes[index * 2] = (word >> 8) as u8;
            bytes[index * 2 + 1] = *word as u8;
        }

        let ranges = vec![FunctionRange {
            phys_start: 0,
            phys_end: 0x3e,
            is_data: false,
        }];
        let mut branch_labels = HashMap::new();
        let mut entries = HashMap::new();
        let mut dispatch_targets = HashMap::new();
        find_jump_tables(
            &bytes,
            0,
            bytes.len() as u64,
            0,
            &ranges,
            &mut branch_labels,
            &mut entries,
            &mut dispatch_targets,
        );

        assert_eq!(branch_labels.get(&0x14).unwrap(), ".Ljtbl_00000014");
        assert_eq!(entries.len(), 8);
        // The jmp @r0 dispatch itself is at i+6 (direct-dispatch shape);
        // this is what the cfg.rs flood-fill walker's resolve_jump_table
        // oracle keys off of.
        assert_eq!(
            dispatch_targets.get(&0xe),
            Some(&(0x14u32, 16u32, vec![0x24, 0x26, 0x28, 0x2a, 0x2c, 0x2e, 0x30, 0x32]))
        );
        branch_labels.insert(0x1a, ".L0000001A".to_string());
        remove_jump_table_internal_labels(&mut branch_labels, &entries);
        assert!(!branch_labels.contains_key(&0x1a));
        assert_eq!(
            branch_labels.get(&0x14).unwrap(),
            &entries.get(&0x14).unwrap().table_label
        );

        let mut string = String::new();
        sh2_disasm(
            8,
            0xc702,
            true,
            &mut string,
            &HashMap::new(),
            &branch_labels,
        );
        assert_eq!(string, "mova .Ljtbl_00000014,r0");
    }

    #[test]
    fn test_mova_signed_byte_jump_table() {
        let mut bytes = vec![0u8; 0x30];
        let words = [
            0xe003, 0x3106, 0x890e, 0xc702, 0x011c, 0x301c, 0x402b, 0x6043,
        ];
        for (index, word) in words.iter().enumerate() {
            bytes[index * 2] = (word >> 8) as u8;
            bytes[index * 2 + 1] = *word as u8;
        }
        // Signed byte offsets from table 0x10 to four case labels.
        bytes[0x10..0x14].copy_from_slice(&[0x04, 0x08, 0x0c, 0x10]);
        for address in (0x14..0x24).step_by(4) {
            bytes[address] = 0x00;
            bytes[address + 1] = 0x0b;
            bytes[address + 2] = 0x00;
            bytes[address + 3] = 0x09;
        }

        let ranges = vec![FunctionRange {
            phys_start: 0,
            phys_end: 0x2e,
            is_data: false,
        }];
        let mut branch_labels = HashMap::new();
        let mut entries = HashMap::new();
        let mut dispatch_targets = HashMap::new();
        find_jump_tables(
            &bytes,
            0,
            bytes.len() as u64,
            0x06000000,
            &ranges,
            &mut branch_labels,
            &mut entries,
            &mut dispatch_targets,
        );

        assert_eq!(entries.len(), 4);
        assert_eq!(
            dispatch_targets.get(&0x0c),
            Some(&(0x10, 4, vec![0x14, 0x18, 0x1c, 0x20]))
        );
    }

    #[test]
    fn test_call_discovered_function_uses_its_byte_table_for_ownership() {
        let mut bytes = vec![0u8; 0x4c];
        let mut put_word = |address: usize, word: u16| {
            bytes[address] = (word >> 8) as u8;
            bytes[address + 1] = word as u8;
        };
        // Caller at 0x04, literal at 0x10, previously invisible callee at 0x20.
        for (address, word) in [
            (0x04, 0x2fe6),
            (0x06, 0xd002),
            (0x08, 0x400b),
            (0x0a, 0x0009),
            (0x0c, 0x000b),
            (0x0e, 0x0009),
            (0x20, 0xe003),
            (0x22, 0x3106),
            (0x24, 0x8910),
            (0x26, 0xc702),
            (0x28, 0x011c),
            (0x2a, 0x301c),
            (0x2c, 0x402b),
            (0x2e, 0x6043),
            (0x34, 0x000b),
            (0x36, 0x0009),
            (0x38, 0x000b),
            (0x3a, 0x0009),
            (0x3c, 0x000b),
            (0x3e, 0x0009),
            (0x40, 0x000b),
            (0x42, 0x0009),
            (0x48, 0x000b),
            (0x4a, 0x0009),
        ] {
            put_word(address, word);
        }
        bytes[0x10..0x14].copy_from_slice(&0x20u32.to_be_bytes());
        bytes[0x30..0x34].copy_from_slice(&[0x04, 0x08, 0x0c, 0x10]);

        let ranges = collect_function_ranges(&bytes, 0, bytes.len() as u64, 0, &[]);
        let helper = ranges.iter().find(|range| range.phys_start == 0x20).unwrap();
        assert_eq!(helper.phys_end, 0x4a);
    }

    #[test]
    fn test_mova_jump_table_with_bounds_branch_between_index_copy_and_dispatch() {
        let mut bytes = vec![0u8; 0x50];
        let words = [
            0xe107, 0x3216, 0x8f02, 0x6123, 0xa000, 0x0009, 0x311c, 0xc702, 0x011d, 0x301c, 0x402b,
            0x0009, 0x0010, 0x0012, 0x0014, 0x0016, 0x0018, 0x001a, 0x001c, 0x001e,
        ];
        for (index, word) in words.iter().enumerate() {
            bytes[index * 2] = (word >> 8) as u8;
            bytes[index * 2 + 1] = *word as u8;
        }

        let ranges = vec![FunctionRange {
            phys_start: 0,
            phys_end: 0x4e,
            is_data: false,
        }];
        let mut branch_labels = HashMap::new();
        let mut entries = HashMap::new();
        find_jump_tables(
            &bytes,
            0,
            bytes.len() as u64,
            0,
            &ranges,
            &mut branch_labels,
            &mut entries,
        &mut HashMap::new(),
        );

        assert_eq!(branch_labels.get(&0x18).unwrap(), ".Ljtbl_00000018");
        assert_eq!(entries.len(), 8);
    }

    #[test]
    fn test_mova_jump_table_infers_count_for_runtime_bound() {
        let mut bytes = vec![0u8; 0x40];
        let words = [
            0x9210, 0x3126, 0x311c, 0xc702, 0x011d, 0x301c, 0x402b, 0x0009, 0x0008, 0x000a, 0x000c,
            0x000e, 0x0009, 0x0009, 0x0009, 0x0009,
        ];
        for (index, word) in words.iter().enumerate() {
            bytes[index * 2] = (word >> 8) as u8;
            bytes[index * 2 + 1] = *word as u8;
        }

        let ranges = vec![FunctionRange {
            phys_start: 0,
            phys_end: 0x3e,
            is_data: false,
        }];
        let mut branch_labels = HashMap::new();
        let mut entries = HashMap::new();
        find_jump_tables(
            &bytes,
            0,
            bytes.len() as u64,
            0,
            &ranges,
            &mut branch_labels,
            &mut entries,
        &mut HashMap::new(),
        );

        assert_eq!(branch_labels.get(&0x10).unwrap(), ".Ljtbl_00000010");
        assert_eq!(entries.len(), 4);
    }

    #[test]
    fn test_mova_jump_table_with_branch_to_dispatch() {
        let mut bytes = vec![0u8; 0x40];
        let words = [
            0xe203, 0x3126, 0x311c, 0xc704, 0x011d, 0x301c, 0xa002, 0x0009, 0x0009, 0x0009, 0x402b,
            0x0009, 0x0008, 0x000a, 0x000c, 0x000e, 0x0009, 0x0009, 0x0009, 0x0009,
        ];
        for (index, word) in words.iter().enumerate() {
            bytes[index * 2] = (word >> 8) as u8;
            bytes[index * 2 + 1] = *word as u8;
        }

        let ranges = vec![FunctionRange {
            phys_start: 0,
            phys_end: 0x3e,
            is_data: false,
        }];
        let mut branch_labels = HashMap::new();
        let mut entries = HashMap::new();
        find_jump_tables(
            &bytes,
            0,
            bytes.len() as u64,
            0,
            &ranges,
            &mut branch_labels,
            &mut entries,
        &mut HashMap::new(),
        );

        assert_eq!(branch_labels.get(&0x18).unwrap(), ".Ljtbl_00000018");
        assert_eq!(entries.len(), 4);
    }

    #[test]
    fn test_mova_table_beyond_stale_function_end_is_classified() {
        let mut bytes = vec![0u8; 0x100];
        let words = [
            0xe207, 0x3126, 0x0009, 0x311c, 0xc720, 0x011d, 0x301c, 0x402b, 0x0009,
        ];
        for (index, word) in words.iter().enumerate() {
            bytes[index * 2] = (word >> 8) as u8;
            bytes[index * 2 + 1] = *word as u8;
        }
        for index in 0..8 {
            let entry = 0x8c + index * 2;
            let offset = (0x20i16 - 0x8ci16) as u16;
            bytes[entry] = (offset >> 8) as u8;
            bytes[entry + 1] = offset as u8;
        }

        let ranges = vec![FunctionRange {
            phys_start: 0,
            phys_end: 0x3e,
            is_data: false,
        }];
        let mut branch_labels = HashMap::new();
        let mut entries = HashMap::new();
        find_jump_tables(
            &bytes,
            0,
            bytes.len() as u64,
            0,
            &ranges,
            &mut branch_labels,
            &mut entries,
        &mut HashMap::new(),
        );

        assert_eq!(branch_labels.get(&0x8c), Some(&".Ljtbl_0000008C".to_string()));
        assert_eq!(entries.len(), 8);
    }

    #[test]
    fn test_mova_without_table_uses_symbolic_target() {
        let mut branch_labels = HashMap::new();
        find_branch_labels(0x060a7420, 0xc702, &mut branch_labels);

        let mut string = String::new();
        sh2_disasm(
            0x060a7420,
            0xc702,
            true,
            &mut string,
            &HashMap::new(),
            &branch_labels,
        );
        assert_eq!(string, "mova .L060A742C,r0");
    }

    fn symbols_from(lines: &[&str]) -> (SymbolTable, NamedTempFile) {
        let mut file = NamedTempFile::new().unwrap();
        for line in lines {
            writeln!(file, "{}", line).unwrap();
        }
        let symbols = read_user_symbols(file.path().to_str().unwrap());
        (symbols, file)
    }

    #[test]
    fn test_object_symbols_in_literal_pool() {
        let (symbols, _file) = symbols_from(&[
            "_g_Entities = 0x060997F8;",
            "_DestroyEntity = 0x0600FFB8;",
        ]);
        let call_sequence = vec![0xd100, 0x0009, 0x410b]
            .into_iter()
            .flat_map(|word: u16| word.to_be_bytes())
            .collect::<Vec<_>>();
        let mut auto = BTreeSet::new();

        assert_eq!(
            format_literal(0x060997f8, &symbols, &mut auto, true),
            "_g_Entities"
        );
        assert!(literal_feeds_call(&call_sequence, 0, 0));
        // Function pointers resolve the same way data pointers do.
        assert_eq!(
            format_literal(0x0600ffb8, &symbols, &mut auto, false),
            "_DestroyEntity"
        );
        assert!(auto.is_empty());
    }

    #[test]
    fn test_size_annotation_is_parsed() {
        let (symbols, _file) = symbols_from(&[
            "_g_Entities = 0x060997F8; /* size = 0xB800 */",
            "_g_Tilemap = 0x0605CE50; /*size=56*/",
            "_g_api = 0x060645A8;",
            "_bad = 0x06000100; /* size = nonsense */",
        ]);

        assert_eq!(symbols.get(0x060997f8).unwrap().size, Some(0xb800));
        // Decimal is accepted and whitespace around the annotation is optional.
        assert_eq!(symbols.get(0x0605ce50).unwrap().size, Some(56));
        assert_eq!(symbols.get(0x060645a8).unwrap().size, None);
        // An unparsable size must not be mistaken for a real one.
        assert_eq!(symbols.get(0x06000100).unwrap().size, None);
        assert_eq!(symbols.get(0x06000100).unwrap().name, "_bad");
    }

    #[test]
    fn test_interior_pointer_folds_only_within_declared_size() {
        let (symbols, _file) = symbols_from(&[
            "_g_Entities = 0x060997F8; /* size = 0xB800 */",
            "_shiftrt = 0x0600A000;",
        ]);
        let mut auto = BTreeSet::new();

        assert_eq!(
            format_literal(0x06099800, &symbols, &mut auto, true),
            "_g_Entities+0x8"
        );
        assert_eq!(
            format_literal(0x060a4ff7, &symbols, &mut auto, true),
            "_g_Entities+0xB7FF"
        );
        // One past the end belongs to whatever comes next, not to g_Entities.
        assert_eq!(
            format_literal(0x060a4ff8, &symbols, &mut auto, true),
            "_DAT_060A4FF8"
        );
        // A symbol with no declared size never absorbs a neighbouring address:
        // libgcc's shift helpers sit two bytes apart and folding onto them
        // would produce output that assembles but lies.
        assert_eq!(
            format_literal(0x0600a006, &symbols, &mut auto, true),
            "_DAT_0600A006"
        );
    }

    #[test]
    fn test_call_targets_never_fold_an_offset() {
        let (symbols, _file) =
            symbols_from(&["_g_Entities = 0x060997F8; /* size = 0xB800 */"]);
        let mut auto = BTreeSet::new();

        // A literal that feeds a jsr is an entry point, so resolving it to
        // g_Entities+8 would describe a call that does not happen.
        assert_eq!(
            format_literal(0x06099800, &symbols, &mut auto, false),
            "_DAT_06099800"
        );
        // The exact address still resolves either way.
        assert_eq!(
            format_literal(0x060997f8, &symbols, &mut auto, false),
            "_g_Entities"
        );
    }

    #[test]
    fn test_canonical_symbols_outrank_generated_placeholders() {
        let mut generated = NamedTempFile::new().unwrap();
        writeln!(generated, "_func_0600FFB8 = 0x0600FFB8;").unwrap();
        let mut hand_written = NamedTempFile::new().unwrap();
        writeln!(hand_written, "_DestroyEntity = 0x0600FFB8;").unwrap();

        let symbols = read_symbol_files(&[
            hand_written.path().to_str().unwrap().to_string(),
            generated.path().to_str().unwrap().to_string(),
        ]);
        assert_eq!(symbols.lookup(0x0600ffb8).unwrap(), "_DestroyEntity");

        // Canonical names also win when an inherited generated file happened
        // to list its placeholder first.
        let symbols = read_symbol_files(&[
            generated.path().to_str().unwrap().to_string(),
            hand_written.path().to_str().unwrap().to_string(),
        ]);
        assert_eq!(symbols.lookup(0x0600ffb8).unwrap(), "_DestroyEntity");
    }

    #[test]
    fn test_missing_symbol_file_is_not_fatal() {
        let symbols = read_symbol_files(&["/nonexistent/syms.txt".to_string()]);
        assert!(symbols.lookup(0x06000000).is_none());
    }

    #[test]
    fn test_unresolved_work_ram_address_gets_generated_symbol() {
        let symbols = SymbolTable::default();
        let mut auto = BTreeSet::new();

        assert_eq!(
            format_literal(0x0605c11c, &symbols, &mut auto, true),
            "_DAT_0605C11C"
        );
        // Constants outside work RAM stay hex: they are not addresses.
        assert_eq!(format_literal(0x00300010, &symbols, &mut auto, true), "0x00300010");
        assert_eq!(format_literal(0x06100000, &symbols, &mut auto, true), "0x06100000");
        assert_eq!(auto.iter().copied().collect::<Vec<_>>(), vec![0x0605c11c]);
    }

    #[test]
    fn test_empty_functions_are_excluded_from_symbols_when_decompiled() {
        let empty = FunctionRange {
            phys_start: 0x100,
            phys_end: 0x108,
            is_data: false,
        };
        let real = FunctionRange {
            phys_start: 0x200,
            phys_end: 0x280,
            is_data: false,
        };

        // The emit loop drops the linker symbol for an 8-byte function when
        // decomp_empty_funcs is set, so referring to one would leave an
        // undefined symbol at link time.
        assert!(!emits_function_symbol(&empty, true));
        assert!(emits_function_symbol(&empty, false));
        assert!(emits_function_symbol(&real, true));
    }

    #[test]
    fn test_function_range_is_only_an_entry_hint() {
        // Two `mov.l r14,@-r15 / mov r15,r14` prologues each ending in
        // `rts / nop`, so find_funcs recovers two functions. The leading
        // padding matters: find_funcs rejects a candidate starting at zero.
        let words = [
            0x0009u16, 0x0009, 0x2fe6, 0x6ef3, 0x6ef6, 0x000b, 0x0009, 0x0009, 0x2fe6, 0x6ef3,
            0x6ef6, 0x000b, 0x0009, 0x0009,
        ];
        let mut bytes = vec![0u8; words.len() * 2];
        for (index, word) in words.iter().enumerate() {
            bytes[index * 2] = (word >> 8) as u8;
            bytes[index * 2 + 1] = *word as u8;
        }
        let end = bytes.len() as u64;

        let discovered = collect_function_ranges(&bytes, 0, end, 0, &[]);
        assert_eq!(discovered.len(), 2);

        // A legacy YAML range contributes its start as an entry hint. Its old
        // oversized end must not swallow the later independent function.
        let forced = collect_function_ranges(&bytes, 0, end, 0, &[[0x0, end as u32 - 2]]);
        assert_eq!(forced.len(), 2);
        assert_eq!(forced[0].phys_start, 0);
        assert_eq!(forced[0].phys_end, 12);
        assert_eq!(forced[1].phys_start, 16);
        assert_eq!(forced[1].phys_end, 24);
    }

    #[test]
    fn test_leaf_callee_with_no_push_is_recovered_via_confirmed_jsr_target() {
        // This is the bug split-core.txt exists to fix: a leaf function with
        // no register-save prologue is invisible to find_funcs's push-scan
        // heuristic, so without the Stage 3/4 augmentation it would have no
        // FunctionRange at all - meaning any reference to it renders as
        // _DAT_%08X instead of a real _func_%08X symbol.
        //
        //   0x00: nop / nop                          (padding)
        //   0x04: push r14                           (caller func_start)
        //   0x06: mov.l @(pc,2),r0  -> literal @0x10
        //   0x08: jsr @r0
        //   0x0a: nop                                (jsr delay slot)
        //   0x0c: rts                                (caller's rts)
        //   0x0e: nop                                (rts delay slot)
        //   0x10: .long 0x00000014                   (literal pool: callee addr)
        //   0x14: mov #5,r0                           (callee - NO push at all)
        //   0x16: rts
        //   0x18: nop                                (rts delay slot)
        let words: [u16; 13] = [
            0x0009, 0x0009, 0x2fe6, 0xd002, 0x400b, 0x0009, 0x000b, 0x0009, 0x0000, 0x0014,
            0xe005, 0x000b, 0x0009,
        ];
        let mut bytes = vec![0u8; words.len() * 2];
        for (index, word) in words.iter().enumerate() {
            bytes[index * 2] = (word >> 8) as u8;
            bytes[index * 2 + 1] = *word as u8;
        }
        let end = bytes.len() as u64;

        // find_funcs alone only recovers the caller - confirms the fixture
        // actually exercises the "missed by the heuristic" case.
        let mut baseline = Vec::<FunctionRange>::new();
        find_funcs(&bytes, 0, end, &mut baseline);
        assert_eq!(baseline.len(), 1);
        assert_eq!(baseline[0].phys_start, 4);

        let ranges = collect_function_ranges(&bytes, 0, end, 0, &[]);
        assert_eq!(ranges.len(), 2, "the jsr-confirmed leaf must now get its own range too");
        assert_eq!(ranges[0].phys_start, 4);
        assert_eq!(ranges[0].phys_end, 0xe);
        assert_eq!(
            ranges[1].phys_start, 0x14,
            "the leaf's range must start exactly at the confirmed call target"
        );
        assert_eq!(
            ranges[1].phys_end, 0x18,
            "the leaf's extent comes from real reachability (mov/rts/delay), not a guess"
        );
    }

    #[test]
    fn test_confirmed_prefix_absorbs_fallthrough_continuation() {
        // The caller references 0x14, whose setup instruction falls through
        // into a conventional prologue at 0x16.  The prologue heuristic sees
        // 0x16, but control-flow ownership must keep the referenced prefix and
        // continuation in one function.
        let words: [u16; 15] = [
            0x0009, 0x0009, 0x2fe6, 0xd002, 0x400b, 0x0009, 0x000b, 0x0009, 0x0000, 0x0014,
            0xe103, 0x2fe6, 0x6ef6, 0x000b, 0x0009,
        ];
        let bytes = words_bytes(&words);

        let ranges = collect_function_ranges(&bytes, 0, bytes.len() as u64, 0, &[]);
        assert_eq!(ranges.len(), 2);
        assert_eq!((ranges[1].phys_start, ranges[1].phys_end), (0x14, 0x1c));
    }

    #[test]
    fn test_forced_prefix_absorbs_fallthrough_continuation() {
        // As above, except the true entry is supplied by YAML rather than a
        // call site.  This is the shape of zero's formerly "shared-tail"
        // ranges: they are ordinary functions cut at an internal prologue.
        let words: [u16; 7] = [0x0009, 0x0009, 0xe103, 0x2fe6, 0x6ef6, 0x000b, 0x0009];
        let bytes = words_bytes(&words);

        let ranges = collect_function_ranges(&bytes, 0, bytes.len() as u64, 0, &[[4, 5]]);
        assert_eq!(ranges.len(), 1);
        assert_eq!((ranges[0].phys_start, ranges[0].phys_end), (4, 12));
    }

    #[test]
    fn test_generated_labels_are_local() {
        let mut branch_labels = HashMap::new();
        let mut data_labels = HashMap::new();
        add_label(0x060a9200, &mut branch_labels);
        add_data_label(0x060a91e0, 0x060a9298, 4, &mut data_labels);

        assert_eq!(branch_labels.get(&0x060a9200).unwrap(), ".L060A9200");
        assert_eq!(
            data_labels.get(&0x060a9298).unwrap().label,
            ".Ldat_060A9298"
        );
    }

    #[test]
    fn test_normalized_operand_syntax() {
        let mut data_labels = HashMap::new();
        add_data_label(0, 12, 4, &mut data_labels);
        let branch_labels = HashMap::new();

        let mut pc_relative = String::new();
        sh2_disasm(
            0,
            0xd002,
            true,
            &mut pc_relative,
            &data_labels,
            &branch_labels,
        );
        assert_eq!(pc_relative, "mov.l .Ldat_0000000C,r0");

        let mut displaced = String::new();
        sh2_disasm(
            0,
            0x181d,
            true,
            &mut displaced,
            &HashMap::new(),
            &branch_labels,
        );
        assert_eq!(displaced, "mov.l r1,@(0x034,r8)");

        let mut indexed = String::new();
        sh2_disasm(
            0,
            0x011d,
            true,
            &mut indexed,
            &HashMap::new(),
            &branch_labels,
        );
        assert_eq!(indexed, "mov.w @(r0,r1),r1");
    }
}
