import type { FFIType } from "bun:ffi";

export const engineSymbols = {
  engine_init: {
    args: ["cstring", "i32", "i32"] as const,
    returns: "i32" as FFIType,
  },
  engine_shutdown: {
    args: [] as const,
    returns: "void" as FFIType,
  },
  engine_poll_events: {
    args: [] as const,
    returns: "bool" as FFIType,
  },
  engine_get_ticks: {
    args: [] as const,
    returns: "u64" as FFIType,
  },
  engine_render_frame: {
    args: ["f32", "f32", "f32", "f32"] as const,
    returns: "i32" as FFIType,
  },
  engine_handle_resize: {
    args: [] as const,
    returns: "i32" as FFIType,
  },
} as const;

export type EngineSymbols = typeof engineSymbols;
