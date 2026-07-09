//! Guest-program profiler for jolt: turns a stream of guest PCs (e.g. an iterator
//! over a jolt execution trace) plus the guest ELF into a folded-stack profile
//! inspired by CKB's `ckb-vm-pprof` debugger. The folded text format is directly
//! consumable by:
//!   - `inferno-flamegraph` (SVG flamegraph)
//!   - CKB's `ckb-vm-pprof-converter`  (gzip pprof protobuf)
//!   - CKB's `ckb-vm-samply-converter` (Gecko JSON for `samply load`)
//!
//! One line per unique call path, root-first: `frame0; frame1; ...; frameN <count>`.

use object::{File, Object, ObjectSymbol, SymbolKind};
use std::collections::HashMap;
use std::io::Write;

/// A function from the guest ELF: address range + folded-frame label.
struct Func {
    start: u64,
    end: u64,
    label: String,
}

/// Write a folded-stack profile derived from a guest PC stream.
///
/// `elf` is the guest ELF bytes (built with symbols; `-g` not required — only the
/// symbol table is used). `pcs` yields one PC per executed guest instruction, in
/// order. Cycles are attributed by count (one per PC).
pub fn write_folded(
    elf: &[u8],
    pcs: impl Iterator<Item = u64>,
    writer: &mut impl Write,
) -> Result<(), Box<dyn std::error::Error>> {
    let obj = File::parse(elf)?;

    // Function ranges + demangled labels from the symbol table.
    let mut funcs: Vec<Func> = Vec::new();
    let mut start_map: HashMap<u64, usize> = HashMap::new();
    for sym in obj.symbols() {
        if sym.kind() != SymbolKind::Text || sym.size() == 0 {
            continue;
        }
        let start = sym.address();
        let mangled = sym.name().unwrap_or("??");
        let label = demangle(mangled);
        let idx = funcs.len();
        funcs.push(Func {
            start,
            end: start + sym.size(),
            label,
        });
        start_map.insert(start, idx);
    }

    // Shadow call stack (SP1-style range logic — no opcode decoding), backed by a
    // count trie so identical call paths merge without per-step allocation.
    // Node 0 is the root. Each node: (parent, func_idx, count). func_idx = !0 for root.
    let nil = usize::MAX;
    let mut nodes: Vec<(Option<usize>, usize, u64)> = vec![(None, nil, 0)];
    let mut child: HashMap<(usize, usize), usize> = HashMap::new();
    let mut stack: Vec<usize> = vec![0]; // node ids; stack[0] = root

    for pc in pcs {
        let top_func = nodes[*stack.last().unwrap()].1;
        let in_top = top_func != nil && pc > funcs[top_func].start && pc <= funcs[top_func].end;

        if in_top {
            // stay in the current function
        } else if let Some(&f) = start_map.get(&pc) {
            // pc is exactly a function entry → call (flatten recursion)
            let already = stack.iter().any(|&n| nodes[n].1 == f);
            if !already {
                let parent = *stack.last().unwrap();
                let next = *child.entry((parent, f)).or_insert_with(|| {
                    nodes.push((Some(parent), f, 0));
                    nodes.len() - 1
                });
                stack.push(next);
            }
        } else {
            // unwind to the deepest frame whose range contains pc (handles tail calls)
            let mut found = None;
            for (d, &n) in stack.iter().enumerate().rev() {
                let f = nodes[n].1;
                if f != nil && pc > funcs[f].start && pc <= funcs[f].end {
                    found = Some(d);
                    break;
                }
            }
            if let Some(d) = found {
                stack.truncate(d + 1);
            }
        }

        nodes[*stack.last().unwrap()].2 += 1;
    }

    // Emit folded text: walk every node, reconstruct its root-first path.
    for (id, (_, _, count)) in nodes.iter().enumerate() {
        if id == 0 || *count == 0 {
            continue;
        }
        let mut path: Vec<usize> = Vec::new();
        let mut cur = id;
        while let Some(parent) = nodes[cur].0 {
            path.push(nodes[cur].1);
            cur = parent;
        }
        path.reverse();
        let frames: Vec<&str> = path.iter().map(|&i| funcs[i].label.as_str()).collect();
        writeln!(writer, "{} {}", frames.join("; "), count)?;
    }
    writer.flush()?;
    Ok(())
}

fn demangle(name: &str) -> String {
    if let Ok(d) = rustc_demangle::try_demangle(name) {
        return format!("{d}");
    }
    if let Ok(d) = cpp_demangle::Symbol::new(name) {
        return format!("{d}");
    }
    name.to_string()
}
