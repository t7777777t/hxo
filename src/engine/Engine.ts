import { Context, Effect, Layer } from "effect";
import { Bridge } from "../ffi/Bridge";

export class EngineError extends Error {
  readonly _tag = "EngineError";
  constructor(message: string, readonly code: number) {
    super(message);
  }
}

export interface EngineService {
  readonly init: (
    title: string,
    width: number,
    height: number
  ) => Effect.Effect<void, EngineError>;
  readonly shutdown: () => Effect.Effect<void>;
  readonly pollEvents: () => Effect.Effect<boolean>;
  readonly getTicks: () => Effect.Effect<bigint>;
}

export const EngineService = Context.GenericTag<EngineService>("EngineService");

export const EngineServiceLive = Layer.succeed(
  EngineService,
  EngineService.of({
    init: (title, width, height) =>
      Effect.sync(() => Bridge.init(title, width, height)).pipe(
        Effect.flatMap((result) =>
          result === 0
            ? Effect.void
            : Effect.fail(new EngineError("Failed to initialize engine", result))
        )
      ),

    shutdown: () => Effect.sync(() => Bridge.shutdown()),

    pollEvents: () => Effect.sync(() => Bridge.pollEvents()),

    getTicks: () => Effect.sync(() => Bridge.getTicks()),
  })
);
