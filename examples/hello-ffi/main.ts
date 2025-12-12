import { Effect, Console } from "effect";
import { EngineService, EngineServiceLive } from "../../src/engine/Engine";

const program = Effect.gen(function* () {
  const engine = yield* EngineService;

  yield* Console.log("Initializing Vulkan engine...");
  yield* engine.init("HXO - Vulkan Clear Color", 800, 600);

  yield* Console.log("Running render loop (press ESC or close window to quit)...");

  let running = true;
  let hue = 0;

  while (running) {
    const shouldQuit = yield* engine.pollEvents();
    if (shouldQuit) {
      running = false;
      continue;
    }

    // Cycle through colors (HSV to RGB with S=1, V=1)
    hue = (hue + 0.5) % 360;
    const h = hue / 60;
    const i = Math.floor(h);
    const f = h - i;
    const q = 1 - f;

    let r = 0, g = 0, b = 0;
    switch (i % 6) {
      case 0: r = 1; g = f; b = 0; break;
      case 1: r = q; g = 1; b = 0; break;
      case 2: r = 0; g = 1; b = f; break;
      case 3: r = 0; g = q; b = 1; break;
      case 4: r = f; g = 0; b = 1; break;
      case 5: r = 1; g = 0; b = q; break;
    }

    // Render frame with cycling clear color
    yield* engine.renderFrame(r, g, b, 1.0).pipe(
      Effect.catchAll(() => Effect.void) // Ignore swapchain recreation
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
