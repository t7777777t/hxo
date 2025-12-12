import { dlopen, ptr, CString } from "bun:ffi";
import { resolve, dirname } from "path";
import { engineSymbols } from "./types";

function getLibraryPath(): string {
  const scriptDir = dirname(Bun.main);
  const projectRoot = resolve(scriptDir, "../..");

  // Check common locations
  const candidates = [
    resolve(projectRoot, "native/build/libengine.so"),
    resolve(projectRoot, "../native/build/libengine.so"),
  ];

  for (const candidate of candidates) {
    if (Bun.file(candidate).size > 0) {
      return candidate;
    }
  }

  // Default path
  return resolve(projectRoot, "native/build/libengine.so");
}

const libPath = getLibraryPath();

let lib: ReturnType<typeof dlopen<typeof engineSymbols>> | null = null;

function getLib() {
  if (!lib) {
    lib = dlopen(libPath, engineSymbols);
  }
  return lib;
}

export const Bridge = {
  init(title: string, width: number, height: number): number {
    const encoder = new TextEncoder();
    const titleBuf = encoder.encode(title + "\0");
    return getLib().symbols.engine_init(ptr(titleBuf), width, height);
  },

  shutdown(): void {
    getLib().symbols.engine_shutdown();
  },

  pollEvents(): boolean {
    return getLib().symbols.engine_poll_events();
  },

  getTicks(): bigint {
    return getLib().symbols.engine_get_ticks();
  },

  close(): void {
    if (lib) {
      lib.close();
      lib = null;
    }
  },
} as const;
