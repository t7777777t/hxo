import { Effect, Console } from "effect";
import { EngineService, EngineServiceLive } from "../../src/engine/Engine";

const program = Effect.gen(function* () {
  const engine = yield* EngineService;

  yield* Console.log("Initializing engine...");
  yield* engine.init("HXO - Hello FFI", 800, 600);

  yield* Console.log("Running main loop (press ESC or close window to quit)...");

  let lastTick = yield* engine.getTicks();
  let running = true;

  while (running) {
    const shouldQuit = yield* engine.pollEvents();
    if (shouldQuit) {
      running = false;
      continue;
    }

    const currentTick = yield* engine.getTicks();
    if (currentTick - lastTick >= 1000n) {
      yield* Console.log(`Tick: ${currentTick}ms`);
      lastTick = currentTick;
    }

    // Small delay to prevent CPU spin
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
