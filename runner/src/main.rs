use jolt_sdk::{guest::program::Program, MemoryConfig};

pub fn main() {
    tracing_subscriber::fmt::init();

    let elf = std::fs::read("./build/minimal").expect("failed to read ELF");

    let memory_config = MemoryConfig::default();
    let mut program = Program::new(&elf, &memory_config);

    let (_, _, program_size, _) = program.decode();
    program.memory_config.program_size = Some(program_size);

    let (_, _, _, io) = program.trace(&[], &[], &[]);
    assert!(!io.panic, "guest panicked during execution");

    println!("Program finished! Outputs: {:?}", io.outputs);
}
