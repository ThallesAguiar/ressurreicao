//! Control-flow classification for SH-2 opcodes, and (in later stages) the
//! flood-fill walker that uses it to recover a function's real extent and
//! literal pool instead of guessing ("nearest push run", "nearest rts").
//!
//! See /sotn/split-core.txt for the full plan this module implements.

/// Where a control-transfer instruction goes, when that's known statically
/// from the encoding alone. `Unknown` covers register-indirect transfers
/// (`braf`/`bsrf`/`jmp @rN`/`jsr @rN`) whose real target has to be proven by
/// some other analysis (a jump-table dispatch pattern, or a provable
/// PC-relative literal load feeding the register) before a walker can trust
/// it as a successor.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum BranchTarget {
    Known(u32),
    Unknown,
}

/// The control-flow effect of one instruction, independent of how it's
/// formatted for output.
///
/// Every variant here also carries an implicit rule about SH-2 delay slots,
/// documented on each variant. Getting this wrong either fuses a byte into
/// the wrong path or forgets to fuse one in - see `has_delay_slot`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Effect {
    /// An ordinary instruction. Successor: `addr + 2`.
    Fallthrough,

    /// `bt` / `bf`. **No delay slot** - unlike every other branch-class
    /// instruction on SH-2. If taken, control goes straight to `target`; if
    /// not taken, control falls straight through to `addr + 2`. Neither
    /// successor is "forced": each is only live on its own path.
    CondBranchNoDelay { target: u32 },

    /// `bt/s` / `bf/s`. The instruction at `addr + 2` (the delay slot)
    /// *always* executes first, regardless of branch direction. After that,
    /// control goes to `target` (taken) or `addr + 4` (not taken, i.e. past
    /// the delay slot). Model this as a fused (addr, addr+2) unit: the
    /// walker must claim `addr + 2` as code on *both* paths before pushing
    /// either successor.
    CondBranchDelayed { target: u32 },

    /// `bra` / `braf`. The delay slot at `addr + 2` always executes, then
    /// control goes to `target` only. There is no `addr + 4` successor -
    /// anything physically after the delay slot is not reachable through
    /// this instruction (e.g. an inline literal pool placed right after a
    /// `bra`'s delay slot).
    UncondBranchDelayed { target: BranchTarget },

    /// `bsr` / `bsrf` / `jsr @rN`. The delay slot at `addr + 2` always
    /// executes as part of the *caller's* path, then the call happens, then
    /// (because it's a call, not a tail jump) control resumes at `addr + 4`
    /// once the callee returns. `target` is a candidate entry point for a
    /// *different* function - a call never gets absorbed into the caller's
    /// own range.
    CallDelayed { target: BranchTarget },

    /// `rts`. The delay slot at `addr + 2` always executes, then the path
    /// ends. No successor.
    ReturnDelayed,

    /// `mov.l @(pc,disp),rN` or `mova @(disp,pc),r0`. Not a control
    /// transfer: `addr + 2` is still the next code address. But the
    /// referenced `addr` (a PC-relative literal) is data, not code, and
    /// must be claimed as such rather than pushed onto the code worklist.
    /// `size` is the claim size in bytes (4 for a `.long` literal; a proven
    /// jump table claims its own, larger, size separately once the table's
    /// entry count is known).
    PcLiteralLoad { addr: u32, size: u32 },
}

/// `bt`/`bf` 8-bit signed displacement, doubled, relative to `addr + 4`.
/// Same formula `find_branch_labels`/the `bf`/`bt`/`bf.s`/`bt.s` string
/// formatters already use - kept here so the CFG classifier and those call
/// sites can never drift apart on the math.
pub fn bf_bt_target(v_addr: u32, op: u32) -> u32 {
    if (op & 0x80) == 0x80 {
        ((op & 0xff) + 0xffffff00)
            .wrapping_mul(2)
            .wrapping_add(v_addr)
            .wrapping_add(4)
    } else {
        ((op & 0xff) * 2) + v_addr + 4
    }
}

/// `bra`/`bsr` 12-bit signed displacement, doubled, relative to `addr + 4`.
pub fn bra_bsr_target(v_addr: u32, op: u32) -> u32 {
    if (op & 0x800) == 0x800 {
        ((op & 0xfff) + 0xfffff000)
            .wrapping_mul(2)
            .wrapping_add(v_addr)
            .wrapping_add(4)
    } else {
        (op & 0xfff) * 2 + v_addr + 4
    }
}

/// `mova @(disp,pc),r0` target: word-aligned `addr + 4`, plus `disp * 4`.
pub fn mova_target(v_addr: u32, op: u32) -> u32 {
    ((v_addr + 4) & 0xfffffffc) + ((op & 0xff) * 4)
}

/// `mov.l @(pc,disp),rN` target: word-aligned `addr + 4`, plus `disp * 4`.
/// Same formula as `mova_target` (both are PC-relative word-aligned loads);
/// kept as a separate name because `find_data_labels`'s existing 0xd000 arm
/// calls it that.
pub fn movl_pc_target(v_addr: u32, op: u32) -> u32 {
    mova_target(v_addr, op) & 0xfffffffc
}

/// True for every SH-2 control-transfer instruction *except* `bt`/`bf`,
/// which are the sole branch-class opcodes with no delay slot.
pub fn has_delay_slot(op: u32) -> bool {
    !matches!(op & 0xff00, 0x8900 | 0x8b00)
        && (is_cond_branch(op) || is_uncond_branch(op) || is_jmp(op) || is_call(op) || is_return(op))
}

fn is_cond_branch(op: u32) -> bool {
    matches!(op & 0xff00, 0x8900 | 0x8b00 | 0x8d00 | 0x8f00)
}

fn is_uncond_branch(op: u32) -> bool {
    (op & 0xf000) == 0xa000 || (op & 0xf0ff) == 0x0023
}

fn is_call(op: u32) -> bool {
    (op & 0xf000) == 0xb000 || (op & 0xf0ff) == 0x0003 || (op & 0xf0ff) == 0x400b
}

fn is_return(op: u32) -> bool {
    op == 0x000b
}

/// `jmp @rN` is intentionally not part of `is_uncond_branch`/`has_delay_slot`
/// dispatch here since its Effect is produced directly in `classify_effect`
/// (opcode `0x402b`); this mirrors the existing string formatter's split
/// between `jmp @rN` and `braf`.
fn is_jmp(op: u32) -> bool {
    (op & 0xf0ff) == 0x402b
}

/// Classify one instruction's control-flow effect. `v_addr` is the
/// instruction's own (virtual) address.
pub fn classify_effect(v_addr: u32, op: u32) -> Effect {
    if (op & 0xff00) == 0x8900 {
        // bt - no delay slot
        return Effect::CondBranchNoDelay {
            target: bf_bt_target(v_addr, op),
        };
    }
    if (op & 0xff00) == 0x8b00 {
        // bf - no delay slot
        return Effect::CondBranchNoDelay {
            target: bf_bt_target(v_addr, op),
        };
    }
    if (op & 0xff00) == 0x8d00 || (op & 0xff00) == 0x8f00 {
        // bt.s / bf.s - delayed
        return Effect::CondBranchDelayed {
            target: bf_bt_target(v_addr, op),
        };
    }
    if (op & 0xf000) == 0xa000 {
        // bra
        return Effect::UncondBranchDelayed {
            target: BranchTarget::Known(bra_bsr_target(v_addr, op)),
        };
    }
    if (op & 0xf000) == 0xb000 {
        // bsr
        return Effect::CallDelayed {
            target: BranchTarget::Known(bra_bsr_target(v_addr, op)),
        };
    }
    if (op & 0xf0ff) == 0x0023 {
        // braf @rN - register-indirect, not known from the encoding alone
        return Effect::UncondBranchDelayed {
            target: BranchTarget::Unknown,
        };
    }
    if (op & 0xf0ff) == 0x0003 {
        // bsrf @rN
        return Effect::CallDelayed {
            target: BranchTarget::Unknown,
        };
    }
    if is_jmp(op) {
        // jmp @rN - resolved elsewhere via jump-table pattern matching
        return Effect::UncondBranchDelayed {
            target: BranchTarget::Unknown,
        };
    }
    if (op & 0xf0ff) == 0x400b {
        // jsr @rN - resolved elsewhere via the literal-feeds-call proof
        return Effect::CallDelayed {
            target: BranchTarget::Unknown,
        };
    }
    if is_return(op) {
        return Effect::ReturnDelayed;
    }
    if (op & 0xf000) == 0xd000 {
        // mov.l @(pc,disp),rN
        return Effect::PcLiteralLoad {
            addr: movl_pc_target(v_addr, op),
            size: 4,
        };
    }
    if (op & 0xff00) == 0xc700 {
        // mova @(disp,pc),r0
        return Effect::PcLiteralLoad {
            addr: mova_target(v_addr, op),
            size: 4,
        };
    }
    Effect::Fallthrough
}

// ---------------------------------------------------------------------------
// Stage 3: confirmed jsr call-target resolution
// ---------------------------------------------------------------------------
//
// This is the backward counterpart of `literal_feeds_call` in main.rs (which
// scans *forward* from a `mov.l @(pc,disp),rN` for a matching `jsr @rN`, to
// decide whether a literal is a call target for `format_literal`'s
// `allow_interior` choice). Here the direction is reversed: given a `jsr
// @rN`'s own address, find the `mov.l` that fed it and read the literal it
// loaded - that value is the actual call target, and a real one proves the
// target is a function entry point regardless of whether `find_funcs`'s
// prologue heuristic ever noticed it (see split-core.txt's root-cause
// section - this is what fixes the _DAT_ mislabeling directly).

/// Scan window, in bytes, matching `literal_feeds_call`'s own forward window.
const JSR_LITERAL_SCAN_WINDOW: u32 = 34;

/// Conservatively report whether `op` replaces `register` before a later
/// indirect call can consume it. False negatives here create bogus call
/// targets, so ambiguous encodings intentionally count as writes.
fn writes_register(op: u32, register: u32) -> bool {
    let n = (op >> 8) & 0xf;
    match op >> 12 {
        // Most arithmetic, register loads, immediate loads, and shifts use
        // bits 11..8 as their destination. The 0x4n0b/0x4n2b transfers are
        // consumers, not writes.
        0x2 | 0x3 | 0x5 | 0x6 | 0x7 | 0x9 | 0xd | 0xe => n == register,
        0x4 => n == register && (op & 0x00ff) != 0x0b && (op & 0x00ff) != 0x2b,
        0x0 => {
            if matches!(op & 0xf0ff, 0x0003 | 0x0023) {
                false
            } else {
                n == register
            }
        }
        // The 0x8xxx and 0xcxxx groups contain R0 loads/ALU operations as
        // well as stores and branches. Treating them all as R0 clobbers is
        // conservative and avoids promoting an indexed table base to a
        // callee address.
        0x8 | 0xc => register == 0,
        _ => false,
    }
}

fn literal_load_reaches_indirect_transfer(
    file_contents: &[u8],
    load_phys: u32,
    transfer_site_phys: u32,
    register: u32,
    transfer_opcode: u32,
) -> bool {
    let mut addr = load_phys + 2;
    while addr <= transfer_site_phys {
        let pos = addr as usize;
        if pos + 1 >= file_contents.len() {
            return false;
        }
        let op = ((file_contents[pos] as u32) << 8) | file_contents[pos + 1] as u32;
        if addr == transfer_site_phys {
            return op & 0xf0ff == transfer_opcode && ((op >> 8) & 0xf) == register;
        }
        if writes_register(op, register) {
            return false;
        }
        addr += 2;
    }
    false
}

/// Forward counterpart used by main.rs when deciding whether a PC literal
/// is a direct call target rather than ordinary data. The loaded register
/// must reach the `jsr` without being overwritten.
pub fn literal_feeds_jsr(
    file_contents: &[u8],
    load_phys: u32,
) -> bool {
    literal_feeds_indirect_transfer(file_contents, load_phys, 0x400b)
}

/// True when a PC-relative literal load reaches `jmp @rN` without the loaded
/// register being overwritten. Used to distinguish local far jumps from
/// ordinary address-valued data.
pub fn literal_feeds_jmp(file_contents: &[u8], load_phys: u32) -> bool {
    literal_feeds_indirect_transfer(file_contents, load_phys, 0x402b)
}

fn literal_feeds_indirect_transfer(
    file_contents: &[u8],
    load_phys: u32,
    transfer_opcode: u32,
) -> bool {
    let pos = load_phys as usize;
    if pos + 1 >= file_contents.len() {
        return false;
    }
    let load = ((file_contents[pos] as u32) << 8) | file_contents[pos + 1] as u32;
    if load & 0xf000 != 0xd000 {
        return false;
    }
    let register = (load >> 8) & 0xf;
    let scan_end = load_phys
        .saturating_add(JSR_LITERAL_SCAN_WINDOW)
        .min(file_contents.len().saturating_sub(2) as u32);
    for transfer_site in ((load_phys + 2)..=scan_end).step_by(2) {
        let transfer_pos = transfer_site as usize;
        let op = ((file_contents[transfer_pos] as u32) << 8)
            | file_contents[transfer_pos + 1] as u32;
        if op & 0xf0ff == transfer_opcode && ((op >> 8) & 0xf) == register {
            return literal_load_reaches_indirect_transfer(
                file_contents,
                load_phys,
                transfer_site,
                register,
                transfer_opcode,
            );
        }
        if writes_register(op, register) {
            return false;
        }
    }
    false
}

/// If `call_site_phys` is a `jsr @rN` fed by a nearby `mov.l @(pc,disp),rN`,
/// return the physical address it calls. Returns `None` for anything else:
/// not a `jsr`, no feeding literal found within the window, or the literal
/// address falls outside `file_contents`.
pub fn resolve_jsr_target(
    file_contents: &[u8],
    virtual_base_addr: u32,
    call_site_phys: u32,
) -> Option<u32> {
    resolve_indirect_literal_target(file_contents, virtual_base_addr, call_site_phys, 0x400b)
}

/// Resolve a literal-fed `jmp @rN` to its physical destination. This is the
/// far-jump counterpart of `resolve_jsr_target`.
pub fn resolve_jmp_target(
    file_contents: &[u8],
    virtual_base_addr: u32,
    jump_site_phys: u32,
) -> Option<u32> {
    resolve_indirect_literal_target(file_contents, virtual_base_addr, jump_site_phys, 0x402b)
}

fn resolve_indirect_literal_target(
    file_contents: &[u8],
    virtual_base_addr: u32,
    transfer_site_phys: u32,
    transfer_opcode: u32,
) -> Option<u32> {
    let site = transfer_site_phys as usize;
    if site + 1 >= file_contents.len() {
        return None;
    }
    let op = ((file_contents[site] as u32) << 8) | file_contents[site + 1] as u32;
    if op & 0xf0ff != transfer_opcode {
        return None;
    }
    let register = (op >> 8) & 0xf;

    let scan_start = transfer_site_phys.saturating_sub(JSR_LITERAL_SCAN_WINDOW);
    let mut addr = transfer_site_phys;
    while addr > scan_start && addr >= 2 {
        addr -= 2;
        let pos = addr as usize;
        let load = ((file_contents[pos] as u32) << 8) | file_contents[pos + 1] as u32;
        if load & 0xf000 != 0xd000 || ((load >> 8) & 0xf) != register {
            continue;
        }
        if !literal_load_reaches_indirect_transfer(
            file_contents,
            addr,
            transfer_site_phys,
            register,
            transfer_opcode,
        ) {
            continue;
        }
        let v_addr = addr.wrapping_add(virtual_base_addr);
        let literal_v_addr = movl_pc_target(v_addr, load);
        let literal_phys = literal_v_addr.wrapping_sub(virtual_base_addr) as usize;
        if literal_phys + 3 >= file_contents.len() {
            return None;
        }
        let value = ((file_contents[literal_phys] as u32) << 24)
            | ((file_contents[literal_phys + 1] as u32) << 16)
            | ((file_contents[literal_phys + 2] as u32) << 8)
            | (file_contents[literal_phys + 3] as u32);
        return Some(value.wrapping_sub(virtual_base_addr));
    }
    None
}

// ---------------------------------------------------------------------------
// Stage 2: flood-fill walker
// ---------------------------------------------------------------------------
//
// Scoping note (see split-core.txt): resolving a *register-indirect*
// successor (`jmp @rN` dispatch tables, `jsr @rN` call targets) is compiler-
// pattern-specific dataflow in its own right - `find_jump_tables` in main.rs
// already does real backward tracing for the jump-table case (which
// register holds the checked index, where its bound came from, how many
// entries the table has) and is already covered by its own tests. Rather
// than duplicate that inside this walker, the walker takes it as a pluggable
// oracle: `resolve_jump_table`/`resolve_call_target` below. This keeps the
// walker's own job - reachable code + literal-pool/table *extent* discovery
// with correct delay-slot semantics - the thing that's actually new, without
// rewriting an already-tuned heuristic that isn't the source of the _DAT_
// mislabeling bug this rework exists to fix.

use std::collections::{BTreeMap, BTreeSet, VecDeque};

/// What one function's flood fill discovered.
#[derive(Debug, Default, PartialEq, Eq)]
pub struct WalkResult {
    /// Physical (file-offset) addresses of every instruction reachable from
    /// the entry point - this function's real code extent.
    pub claimed_code: BTreeSet<u32>,
    /// Physical addr -> byte size, for every PC-relative literal / jump
    /// table this function's code reaches. Not code - must not be decoded
    /// as instructions.
    pub claimed_data: BTreeMap<u32, u32>,
    /// Physical addresses proven to be called (jsr/bsr/bsrf, or a resolved
    /// jsr @rN) from within this function. Candidates for *other* functions'
    /// entry points - never absorbed into this walk's own claimed_code.
    pub call_targets: BTreeSet<u32>,
    /// Physical addresses that are intra-function branch targets (bt/bf/
    /// bt.s/bf.s/bra/jump-table entries) - for `.L` label generation.
    pub branch_targets: BTreeSet<u32>,
    /// Physical addresses this walk reached via ordinary control flow that
    /// turned out to belong to a *different* already-known function entry.
    /// A non-empty result here means a seed is missing or wrong somewhere -
    /// see the "Conflict policy" section of split-core.txt. The walker
    /// itself does not panic; the caller decides how loudly to surface it.
    pub boundary_conflicts: BTreeSet<u32>,
}

/// Flood-fill one function's control-flow graph from `entry_phys`.
///
/// All addresses in/out are physical (file-offset) addresses, matching
/// `FunctionRange`; `virtual_base_addr` converts to/from the virtual address
/// space that branch-target arithmetic (and `resolve_jsr_target`/
/// `resolve_jump_table`, which key off the disassembly's usual virtual
/// addresses) is expressed in.
///
/// `other_ranges` is every other function's *full* claimed extent
/// (inclusive `[phys_start, phys_end]`) already known in this subsegment
/// (from any seed source - YAML, prologue heuristic, other confirmed call
/// targets). Reaching *any* address owned by one of them via ordinary
/// control flow (not a call) stops that path and is recorded in
/// `boundary_conflicts` rather than silently merged into this function.
///
/// This must be the full interval, not just each function's entry point:
/// a branch that jumps clean over another function's start and lands
/// somewhere in its *middle* is just as much a conflict as landing exactly
/// on its first instruction, and checking only entry points would miss it -
/// silently absorbing bytes that already belong to another function.
pub fn flood_fill_function(
    file_contents: &[u8],
    virtual_base_addr: u32,
    section_start_phys: u32,
    section_end_phys: u32,
    entry_phys: u32,
    other_ranges: &[(u32, u32)],
    resolve_jsr_target: &dyn Fn(u32) -> Option<u32>,
    resolve_jump_table: &dyn Fn(u32) -> Option<(u32, u32, Vec<u32>)>,
) -> WalkResult {
    let mut result = WalkResult::default();
    let mut worklist: VecDeque<u32> = VecDeque::new();
    worklist.push_back(entry_phys);

    let in_bounds = |addr: u32| addr >= section_start_phys && addr + 1 < section_end_phys;
    let owned_elsewhere = |addr: u32| {
        addr != entry_phys
            && other_ranges
                .iter()
                .any(|&(start, end)| addr >= start && addr <= end)
    };

    while let Some(addr) = worklist.pop_front() {
        if result.claimed_code.contains(&addr) {
            continue;
        }
        if owned_elsewhere(addr) {
            result.boundary_conflicts.insert(addr);
            continue;
        }
        if !in_bounds(addr) || addr % 2 != 0 {
            continue;
        }
        if result.claimed_data.contains_key(&addr) {
            // Something already proved this address is data (e.g. a literal
            // pool cell); a control-flow edge landing here too is a
            // conflict, not code to decode.
            result.boundary_conflicts.insert(addr);
            continue;
        }

        let op = ((file_contents[addr as usize] as u32) << 8) | file_contents[addr as usize + 1] as u32;
        let v_addr = addr.wrapping_add(virtual_base_addr);
        let effect = classify_effect(v_addr, op);
        result.claimed_code.insert(addr);

        let to_phys = |v: u32| v.wrapping_sub(virtual_base_addr);

        match effect {
            Effect::Fallthrough => {
                worklist.push_back(addr.wrapping_add(2));
            }
            Effect::PcLiteralLoad { addr: v_target, size } => {
                let phys_target = to_phys(v_target);
                result
                    .claimed_data
                    .entry(phys_target)
                    .or_insert(size);
                worklist.push_back(addr.wrapping_add(2));
            }
            Effect::CondBranchNoDelay { target } => {
                let target_phys = to_phys(target);
                result.branch_targets.insert(target_phys);
                worklist.push_back(target_phys);
                worklist.push_back(addr.wrapping_add(2));
            }
            Effect::CondBranchDelayed { target } => {
                let delay_addr = addr.wrapping_add(2);
                claim_delay_slot(&mut result, delay_addr, in_bounds, owned_elsewhere);
                let target_phys = to_phys(target);
                result.branch_targets.insert(target_phys);
                worklist.push_back(target_phys);
                worklist.push_back(addr.wrapping_add(4));
            }
            Effect::UncondBranchDelayed { target } => {
                let delay_addr = addr.wrapping_add(2);
                claim_delay_slot(&mut result, delay_addr, in_bounds, owned_elsewhere);
                match target {
                    BranchTarget::Known(t) => {
                        let target_phys = to_phys(t);
                        result.branch_targets.insert(target_phys);
                        worklist.push_back(target_phys);
                    }
                    BranchTarget::Unknown => {
                        if let Some((table_addr, table_size, targets)) = resolve_jump_table(addr) {
                            result
                                .claimed_data
                                .entry(table_addr)
                                .or_insert(table_size);
                            for target_phys in targets {
                                result.branch_targets.insert(target_phys);
                                worklist.push_back(target_phys);
                            }
                        } else if let Some(target_phys) = resolve_jsr_target(addr) {
                            // The same literal-dataflow oracle also resolves
                            // local far `jmp @rN` transfers.
                            result.branch_targets.insert(target_phys);
                            worklist.push_back(target_phys);
                        }
                        // Unresolvable indirect jump: nothing more is
                        // statically reachable through this edge.
                    }
                }
            }
            Effect::CallDelayed { target } => {
                let delay_addr = addr.wrapping_add(2);
                claim_delay_slot(&mut result, delay_addr, in_bounds, owned_elsewhere);
                let resolved = match target {
                    BranchTarget::Known(t) => Some(to_phys(t)),
                    BranchTarget::Unknown => resolve_jsr_target(addr),
                };
                if let Some(call_target) = resolved {
                    result.call_targets.insert(call_target);
                }
                // A call returns into the caller: keep walking past it
                // regardless of whether the target was resolvable.
                worklist.push_back(addr.wrapping_add(4));
            }
            Effect::ReturnDelayed => {
                let delay_addr = addr.wrapping_add(2);
                claim_delay_slot(&mut result, delay_addr, in_bounds, owned_elsewhere);
                // Path ends here - no successor.
            }
        }
    }

    result
}

/// Claim a delay-slot instruction as this function's code. Per the SH-2 ISA
/// another branch can never legally occupy a delay slot, so this does not
/// recurse into `classify_effect` for it - it's always a plain instruction,
/// and its only job here is to be marked reachable.
///
/// Still checked against `owned_elsewhere`: if the byte the delay slot
/// *must* execute is already claimed by another function, that is not a
/// case to silently paper over by claiming it anyway - it means an entry
/// point or a branch target upstream was wrong, and the caller needs to see
/// it in `boundary_conflicts` rather than get a function that quietly
/// overlaps its neighbor.
fn claim_delay_slot(
    result: &mut WalkResult,
    delay_addr: u32,
    in_bounds: impl Fn(u32) -> bool,
    owned_elsewhere: impl Fn(u32) -> bool,
) {
    if !in_bounds(delay_addr) {
        return;
    }
    if owned_elsewhere(delay_addr) {
        result.boundary_conflicts.insert(delay_addr);
        return;
    }
    result.claimed_code.insert(delay_addr);
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn bt_has_no_delay_slot() {
        // bt 0x8900, 8-bit displacement 0 -> target = addr+4
        assert!(!has_delay_slot(0x8900));
        assert_eq!(
            classify_effect(0x1000, 0x8900),
            Effect::CondBranchNoDelay { target: 0x1004 }
        );
    }

    #[test]
    fn bf_has_no_delay_slot() {
        assert!(!has_delay_slot(0x8b00));
        assert_eq!(
            classify_effect(0x1000, 0x8b00),
            Effect::CondBranchNoDelay { target: 0x1004 }
        );
    }

    #[test]
    fn bts_bfs_have_delay_slot() {
        assert!(has_delay_slot(0x8d00));
        assert!(has_delay_slot(0x8f00));
        assert_eq!(
            classify_effect(0x1000, 0x8d00),
            Effect::CondBranchDelayed { target: 0x1004 }
        );
        assert_eq!(
            classify_effect(0x1000, 0x8f00),
            Effect::CondBranchDelayed { target: 0x1004 }
        );
    }

    #[test]
    fn bra_bsr_have_delay_slot_and_known_target() {
        assert!(has_delay_slot(0xa000));
        assert!(has_delay_slot(0xb000));
        assert_eq!(
            classify_effect(0x1000, 0xa000),
            Effect::UncondBranchDelayed {
                target: BranchTarget::Known(0x1004)
            }
        );
        assert_eq!(
            classify_effect(0x1000, 0xb000),
            Effect::CallDelayed {
                target: BranchTarget::Known(0x1004)
            }
        );
    }

    #[test]
    fn braf_bsrf_jmp_jsr_have_delay_slot_and_unknown_target() {
        assert!(has_delay_slot(0x0023)); // braf r0
        assert!(has_delay_slot(0x0003)); // bsrf r0
        assert!(has_delay_slot(0x402b)); // jmp @r0
        assert!(has_delay_slot(0x400b)); // jsr @r0
        assert_eq!(
            classify_effect(0x1000, 0x0023),
            Effect::UncondBranchDelayed {
                target: BranchTarget::Unknown
            }
        );
        assert_eq!(
            classify_effect(0x1000, 0x0003),
            Effect::CallDelayed {
                target: BranchTarget::Unknown
            }
        );
        assert_eq!(
            classify_effect(0x1000, 0x402b),
            Effect::UncondBranchDelayed {
                target: BranchTarget::Unknown
            }
        );
        assert_eq!(
            classify_effect(0x1000, 0x400b),
            Effect::CallDelayed {
                target: BranchTarget::Unknown
            }
        );
    }

    #[test]
    fn rts_has_delay_slot_and_no_successor() {
        assert!(has_delay_slot(0x000b));
        assert_eq!(classify_effect(0x1000, 0x000b), Effect::ReturnDelayed);
    }

    #[test]
    fn plain_alu_op_has_no_delay_slot() {
        // add r1, r0 (0x300c family)
        assert!(!has_delay_slot(0x300c));
        assert_eq!(classify_effect(0x1000, 0x300c), Effect::Fallthrough);
    }

    #[test]
    fn movl_pc_and_mova_claim_data_not_code() {
        assert!(!has_delay_slot(0xd000));
        assert!(!has_delay_slot(0xc700));
        // mov.l @(pc,0),r0 at 0x1000 -> literal at (0x1000+4)&~3 + 0 = 0x1004
        assert_eq!(
            classify_effect(0x1000, 0xd000),
            Effect::PcLiteralLoad {
                addr: 0x1004,
                size: 4
            }
        );
        // mova @(0,pc),r0 at 0x1000 -> same addressing
        assert_eq!(
            classify_effect(0x1000, 0xc700),
            Effect::PcLiteralLoad {
                addr: 0x1004,
                size: 4
            }
        );
    }

    fn words_bytes(words: &[u16]) -> Vec<u8> {
        let mut out = Vec::with_capacity(words.len() * 2);
        for w in words {
            out.push((w >> 8) as u8);
            out.push((*w & 0xff) as u8);
        }
        out
    }

    fn no_calls(_addr: u32) -> Option<u32> {
        None
    }
    fn no_tables(_addr: u32) -> Option<(u32, u32, Vec<u32>)> {
        None
    }

    #[test]
    fn leaf_function_with_no_push_is_recovered_by_flood_fill() {
        // mov #5,r0 / rts / nop(delay) - a leaf that find_funcs's push-scan
        // heuristic would never anchor on, since there's no push run at all.
        let bytes = words_bytes(&[0xe005, 0x000b, 0x0009]);
        let result = flood_fill_function(
            &bytes,
            0,
            0,
            bytes.len() as u32,
            0,
            &[],
            &no_calls,
            &no_tables,
        );
        assert_eq!(
            result.claimed_code,
            BTreeSet::from([0, 2, 4]),
            "expected the whole 3-instruction leaf body to be reachable"
        );
        assert!(result.claimed_data.is_empty());
        assert!(result.boundary_conflicts.is_empty());
    }

    #[test]
    fn unconditional_branch_delay_slot_does_not_fall_through_into_literal_pool() {
        // bra +0x8 / nop(delay) / .long (unreachable literal pool) / target: rts / nop(delay)
        let bytes = words_bytes(&[0xa002, 0x0009, 0xdead, 0xbeef, 0x000b, 0x0009]);
        let result = flood_fill_function(
            &bytes,
            0,
            0,
            bytes.len() as u32,
            0,
            &[],
            &no_calls,
            &no_tables,
        );
        assert_eq!(result.claimed_code, BTreeSet::from([0, 2, 8, 0xa]));
        assert!(
            !result.claimed_code.contains(&4) && !result.claimed_code.contains(&6),
            "the inline literal pool bytes must not be treated as reachable code"
        );
        assert!(result.claimed_data.is_empty());
    }

    #[test]
    fn cond_branch_no_delay_reaches_both_arms_without_a_forced_shared_cell() {
        // bt +0x8 / mov #1,r0 ; rts ; nop(delay)   [not-taken arm starts at addr+2]
        //          target: mov #2,r0 ; rts ; nop(delay)
        let bytes = words_bytes(&[
            0x8902, // bt target(=8)
            0xe001, // not-taken arm: mov #1,r0
            0x000b, // rts
            0x0009, // delay
            0xe002, // taken arm (addr 8): mov #2,r0
            0x000b, // rts
            0x0009, // delay
        ]);
        let result = flood_fill_function(
            &bytes,
            0,
            0,
            bytes.len() as u32,
            0,
            &[],
            &no_calls,
            &no_tables,
        );
        assert_eq!(
            result.claimed_code,
            BTreeSet::from([0, 2, 4, 6, 8, 0xa, 0xc]),
            "both the taken and not-taken arms must be fully reachable"
        );
    }

    #[test]
    fn cond_branch_delayed_forces_the_shared_delay_slot_and_splits_at_addr_plus_4() {
        // bt.s target(=0xA) / mov #7,r1 (delay slot, ALWAYS runs) /
        //   not-taken arm at addr+4: mov #9,r0 ; rts ; nop(delay)
        //   taken arm at 0xA: mov #2,r0 ; rts ; nop(delay)
        let bytes = words_bytes(&[
            0x8d03, // bt.s target(=0xA)
            0xe107, // delay slot: mov #7,r1
            0xe009, // not-taken arm (addr 4): mov #9,r0
            0x000b, // rts
            0x0009, // delay
            0xe002, // taken arm (addr 0xA): mov #2,r0
            0x000b, // rts
            0x0009, // delay
        ]);
        let result = flood_fill_function(
            &bytes,
            0,
            0,
            bytes.len() as u32,
            0,
            &[],
            &no_calls,
            &no_tables,
        );
        assert!(
            result.claimed_code.contains(&2),
            "the delay slot at addr+2 must be claimed unconditionally, not just as a fallthrough"
        );
        assert_eq!(
            result.claimed_code,
            BTreeSet::from([0, 2, 4, 6, 8, 0xa, 0xc, 0xe])
        );
    }

    #[test]
    fn resolved_call_target_is_recorded_but_not_absorbed_into_caller() {
        // jsr @r1 / nop(delay) / rts / nop(delay)
        let bytes = words_bytes(&[0x410b, 0x0009, 0x000b, 0x0009]);
        let resolve_call = |addr: u32| if addr == 0 { Some(0x100) } else { None };
        let result = flood_fill_function(
            &bytes,
            0,
            0,
            bytes.len() as u32,
            0,
            &[],
            &resolve_call,
            &no_tables,
        );
        assert_eq!(result.call_targets, BTreeSet::from([0x100]));
        assert!(
            !result.claimed_code.contains(&0x100),
            "a call target belongs to a different function's walk, never this one's"
        );
        assert_eq!(
            result.claimed_code,
            BTreeSet::from([0, 2, 4, 6]),
            "the caller continues past the call at addr+4"
        );
    }

    #[test]
    fn unresolved_jsr_still_lets_the_caller_continue() {
        let bytes = words_bytes(&[0x410b, 0x0009, 0x000b, 0x0009]);
        let result = flood_fill_function(
            &bytes,
            0,
            0,
            bytes.len() as u32,
            0,
            &[],
            &no_calls,
            &no_tables,
        );
        assert!(result.call_targets.is_empty());
        assert_eq!(result.claimed_code, BTreeSet::from([0, 2, 4, 6]));
    }

    #[test]
    fn jump_table_dispatch_targets_are_not_recorded_as_calls() {
        // jmp @r1 / nop(delay) - table resolution handed off to the oracle
        let bytes = words_bytes(&[
            0x412b, 0x0009, // jmp @r1 ; nop(delay)  @ 0,2
            0x0000, 0x0000, // 4 bytes of table data claimed by the oracle @ 4
            0x000b, 0x0009, // target A: rts ; nop(delay) @ 8, a
            0x000b, 0x0009, // target B: rts ; nop(delay) @ c, e
        ]);
        let resolve_table = |addr: u32| {
            if addr == 0 {
                Some((4u32, 4u32, vec![8u32, 0xcu32]))
            } else {
                None
            }
        };
        let result = flood_fill_function(
            &bytes,
            0,
            0,
            bytes.len() as u32,
            0,
            &[],
            &no_calls,
            &resolve_table,
        );
        assert_eq!(result.claimed_data, BTreeMap::from([(4, 4)]));
        assert!(result.branch_targets.contains(&8));
        assert!(result.branch_targets.contains(&0xc));
        assert!(
            result.call_targets.is_empty(),
            "jump-table dispatch targets are jumps, not calls, and must never seed a new function"
        );
        assert_eq!(
            result.claimed_code,
            BTreeSet::from([0, 2, 8, 0xa, 0xc, 0xe])
        );
    }

    #[test]
    fn ordinary_control_flow_reaching_another_functions_entry_is_a_conflict_not_a_merge() {
        // A plain bra jumps straight into what's declared as another
        // function's entry point, which must be reported, not silently
        // absorbed into this walk.
        let bytes = words_bytes(&[0xa000, 0x0009, 0x000b, 0x0009]); // bra target(=4) / nop(delay) / rts / nop
        let other_ranges = [(4u32, 4u32)];
        let result = flood_fill_function(
            &bytes,
            0,
            0,
            bytes.len() as u32,
            0,
            &other_ranges,
            &no_calls,
            &no_tables,
        );
        assert!(result.boundary_conflicts.contains(&4));
        assert!(!result.claimed_code.contains(&4));
    }

    #[test]
    fn resolve_jsr_target_reads_the_feeding_literal() {
        // mov.l @(pc,0),r1 / jsr @r1 / .long 0x00000200
        let bytes = words_bytes(&[0xd100, 0x410b, 0x0000, 0x0200]);
        assert_eq!(resolve_jsr_target(&bytes, 0, 2), Some(0x200));
    }

    #[test]
    fn resolve_jsr_target_is_none_when_call_site_is_not_a_jsr() {
        let bytes = words_bytes(&[0xd100, 0x410b, 0x0000, 0x0200]);
        assert_eq!(resolve_jsr_target(&bytes, 0, 0), None);
    }

    #[test]
    fn resolve_jsr_target_is_none_without_a_feeding_literal() {
        // jsr @r1 with no preceding mov.l @(pc,disp),r1 in the scan window
        let bytes = words_bytes(&[0x6003, 0x410b, 0x0009, 0x000b]);
        assert_eq!(resolve_jsr_target(&bytes, 0, 2), None);
    }

    #[test]
    fn resolve_jsr_target_rejects_a_clobbered_call_register() {
        // mov.l @(pc,1),r0 loads a table base, then the indexed load replaces
        // r0 with an entry before jsr @r0. The table base is data, not a
        // function entry point.
        let bytes = words_bytes(&[0xd001, 0x001e, 0x400b, 0x0009, 0x0000, 0x0200]);
        assert_eq!(resolve_jsr_target(&bytes, 0, 4), None);
        assert!(!literal_feeds_jsr(&bytes, 0));
    }

    #[test]
    fn resolve_jsr_target_rejects_an_ordinary_register_overwrite() {
        // mov.l @(pc,1),r1 / mov r2,r1 / jsr @r1
        let bytes = words_bytes(&[0xd101, 0x6123, 0x410b, 0x0009, 0x0000, 0x0200]);
        assert_eq!(resolve_jsr_target(&bytes, 0, 4), None);
        assert!(!literal_feeds_jsr(&bytes, 0));
    }
}
