import { Effect, Console } from "effect";
import { EngineService, EngineServiceLive } from "../../src/engine/Engine";

const program = Effect.gen(function* () {
  const engine = yield* EngineService;

  yield* Console.log("Initializing Vulkan engine...");
  yield* engine.init("HXO - Hello Triangle", 800, 600);

  yield* Console.log("Running render loop (press ESC or close window to quit)...");

  let running = true;

  while (running) {
    const shouldQuit = yield* engine.pollEvents();
    if (shouldQuit) {
      running = false;
      continue;
    }

    // Render frame with dark background - triangle draws automatically
    yield* engine.renderFrame(0.1, 0.1, 0.15, 1.0).pipe(
      Effect.catchAll(() => Effect.void)
    );

    // ~60 FPS
    yield* Effect.sleep("16 millis");
  }

  yield* Console.log("Shutting down...");
  yield* engine.shutdown();
  yield* Console.log("Done!");
});

const runnable = program.pipe(Effect.provide(EngineServiceLive));

Effect.runPromise(runnable).catch((error) => {
  console.error("Fatal error:", error);
  process.exit(1);
});
