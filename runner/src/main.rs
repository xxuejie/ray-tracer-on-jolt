use jolt_sdk::{guest::program::Program, MemoryConfig};
use std::time::Instant;

pub fn main() {
    tracing_subscriber::fmt::init();

    let prove = std::env::args().any(|a| a == "prove");

    let elf = std::fs::read("./build/raytracer").expect("failed to read ELF");

    let max_output_size: u64 = 16 << 20;
    let memory_config = MemoryConfig {
        max_output_size,
        ..MemoryConfig::default()
    };
    let mut program = Program::new(&elf, &memory_config);

    let (_, _, program_size, _) = program.decode();
    program.memory_config.program_size = Some(program_size);

    let t = Instant::now();
    let (_, trace, _, io) = program.trace(&[], &[], &[]);
    let execution_secs = t.elapsed().as_secs_f64();
    assert!(!io.panic, "guest panicked during execution");

    println!(
        "execution:   {:.2}s ({} cycles)",
        execution_secs,
        trace.len()
    );
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

    if prove {
        prove_and_verify(&program, trace.len(), max_output_size);
    }
}

fn prove_and_verify(program: &Program, trace_len: usize, max_output_size: u64) {
    use jolt_sdk::{
        Curve, ProofTranscript, VerifierField, VerifierPCS, VerifierTranscript, VerifierVC, F, PCS,
    };

    // max_trace_length must be a power of two >= the actual (padded) trace length.
    // jolt pads to (len+1).next_power_of_two(), so match that exactly.
    let max_trace_length = (trace_len + 1).next_power_of_two();
    println!(
        "\n== Prove + verify (max_trace_length = {}, ~{:.1} MiB trace) ==",
        max_trace_length,
        (max_trace_length as f64) * 96.0 / (1024.0 * 1024.0)
    );

    let t = Instant::now();
    let prover_pp =
        jolt_sdk::guest::prover::preprocess(program, max_trace_length).expect("preprocess failed");
    println!("preprocess:  {:.2}s", t.elapsed().as_secs_f64());

    let verifier_pp = jolt_sdk::jolt_prover_legacy::zkvm::proof::verifier_preprocessing_from_prover::<
        F,
        Curve,
        PCS,
    >(&prover_pp);

    let mut output_buf = vec![0u8; max_output_size as usize];
    let t = Instant::now();
    let (proof, prove_io, _) = jolt_sdk::guest::prover::prove::<F, Curve, PCS, ProofTranscript>(
        program,
        &[],
        &[],
        &[],
        None,
        None,
        &mut output_buf,
        &prover_pp,
    )
    .expect("prove failed");
    println!("prove:       {:.2}s", t.elapsed().as_secs_f64());

    let proof_bytes =
        jolt_sdk::serialize_verifier_object(&proof).expect("failed to serialize proof");
    println!(
        "proof size:  {:.1} KiB ({} bytes)",
        proof_bytes.len() as f64 / 1024.0,
        proof_bytes.len()
    );

    let is_zk = proof.claims.is_zk();
    let t = Instant::now();
    jolt_sdk::jolt_verifier::verify::<VerifierField, VerifierPCS, VerifierVC, VerifierTranscript>(
        &verifier_pp,
        &prove_io,
        &proof,
        None,
        is_zk,
    )
    .expect("verify failed");
    println!("verify:      {:.2}s", t.elapsed().as_secs_f64());
    println!("✓ proof verified");
}
