/*
Rogatia NNUE trainer config -- Phase 6, the first network.

    (768 -> 256)x2 -> 1, SCReLU, QA=255, QB=64, eval scale 400

Derived from bullet's own examples/simple.rs, which is already this exact
architecture; the changes are the hidden size, the data loader pointing at our
datagen output, and a schedule sized to the data we actually have.

This file is the authoritative copy.  bullet declares its examples explicitly
in crates/bullet_lib/Cargo.toml, so rather than editing the clone's manifest we
copy over the example it already declares:

    cp trainer/rogatia.rs ~/bullet/examples/simple.rs
    cd ~/bullet && CUDA_PATH=/opt/cuda LD_LIBRARY_PATH=/opt/cuda/lib64 \
        cargo r -r --package bullet_lib --features cuda --example simple

Output lands in ~/bullet/checkpoints/<net_id>-<superbatch>/.
*/
use bullet_lib::{
    game::inputs::Chess768,
    nn::optimiser::AdamW,
    trainer::{
        save::SavedFormat,
        schedule::{TrainingSchedule, TrainingSteps, lr, wdl},
        settings::LocalSettings,
    },
    value::{ValueTrainerBuilder, loader},
};

// Phase 6 target from docs/ROADMAP.md.  Phase 8 raises this to 1024 with
// output buckets, on roughly ten times the data.
const HIDDEN_SIZE: usize = 256;
const SCALE: i32 = 400;
const QA: i16 = 255;
const QB: i16 = 64;

// One superbatch is one pass over the data, so this is set from the dataset
// size rather than left at bullet's 6104 (which assumes exactly 100M).
const BATCH_SIZE: usize = 16_384;
const POSITIONS: usize = 112_000_683;

fn main() {
    let mut trainer = ValueTrainerBuilder::default()
        .dual_perspective()
        .optimiser(AdamW)
        .inputs(Chess768)
        // The order here is the order the engine's loader reads them in.
        .save_format(&[
            SavedFormat::id("l0w").round().quantise::<i16>(QA),
            SavedFormat::id("l0b").round().quantise::<i16>(QA),
            SavedFormat::id("l1w").round().quantise::<i16>(QB),
            SavedFormat::id("l1b").round().quantise::<i16>(QA * QB),
        ])
        .loss_fn(|output, target| output.sigmoid().squared_error(target))
        .build(|builder, stm_inputs, ntm_inputs| {
            let l0 = builder.new_affine("l0", 768, HIDDEN_SIZE);
            let l1 = builder.new_affine("l1", 2 * HIDDEN_SIZE, 1);

            let stm_hidden = l0.forward(stm_inputs).screlu();
            let ntm_hidden = l0.forward(ntm_inputs).screlu();
            l1.forward(stm_hidden.concat(ntm_hidden))
        });

    let schedule = TrainingSchedule {
        net_id: "rogatia".to_string(),
        eval_scale: SCALE as f32,
        steps: TrainingSteps {
            batch_size: BATCH_SIZE,
            batches_per_superbatch: POSITIONS / BATCH_SIZE,
            start_superbatch: 1,
            end_superbatch: 40,
        },
        // 0.3 = 30% game result, 70% search score.  bullet's example uses 0.75,
        // which fits mostly outcomes and pushes the net toward saturated evals
        // -- the scaffold net came out roughly twice the scale of the search
        // scores.  That matters here because Phase 4's pruning margins (RFP
        // 75/ply, the SEE thresholds) are calibrated to the old eval's scale,
        // and an inflated eval silently makes all of them more aggressive.
        wdl_scheduler: wdl::ConstantWDL { value: 0.3 },
        lr_scheduler: lr::StepLR { start: 0.001, gamma: 0.3, step: 15 },
        save_rate: 10,
    };

    let settings = LocalSettings {
        threads: 4,
        test_set: None,
        output_directory: "checkpoints",
        batch_queue_size: 64,
    };

    // Datagen writes one .bin per worker; they are read in sequence.
    let data_loader = loader::DirectSequentialDataLoader::new(&[
        "data/rogatia/part-1.bin",
        "data/rogatia/part-2.bin",
        "data/rogatia/part-3.bin",
        "data/rogatia/part-4.bin",
        "data/rogatia/part-5.bin",
        "data/rogatia/part-6.bin",
        "data/rogatia/part-7.bin",
        "data/rogatia/part-8.bin",
        "data/rogatia/part-9.bin",
        "data/rogatia/part-10.bin",
        "data/rogatia/part-11.bin",
        "data/rogatia/part-12.bin",
        "data/rogatia/part-13.bin",
        "data/rogatia/part-14.bin",
        "data/rogatia/part-15.bin",
        "data/rogatia/part-16.bin",
    ]);

    trainer.run(&schedule, &settings, &data_loader);
}
