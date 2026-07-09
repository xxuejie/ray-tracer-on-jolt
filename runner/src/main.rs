use jolt_sdk::{guest::program::Program, MemoryConfig};

pub fn main() {
    tracing_subscriber::fmt::init();

    let elf = std::fs::read("./build/raytracer").expect("failed to read ELF");

    let memory_config = MemoryConfig {
        max_output_size: 16 << 20,
        ..MemoryConfig::default()
    };
    let mut program = Program::new(&elf, &memory_config);

    let (_, _, program_size, _) = program.decode();
    program.memory_config.program_size = Some(program_size);

    let (_, trace, _, io) = program.trace(&[], &[], &[]);
    assert!(!io.panic, "guest panicked during execution");

    println!("Trace length: {} cycles", trace.len());
    println!("Output size: {} bytes", io.outputs.len());

    // The guest copies the image into the output region via memcpy. jolt's MMU
    // byte-splits multi-byte stores, which can commit a stray trailing NUL past
    // the intended length. Strip trailing NULs so the PPM is clean.
    let mut end = io.outputs.len();
    while end > 0 && io.outputs[end - 1] == 0 {
        end -= 1;
    }
    std::fs::write("image_jolt.ppm", &io.outputs[..end]).expect("failed to write image");
    println!("Saved image_jolt.ppm ({} bytes)", end);
}

