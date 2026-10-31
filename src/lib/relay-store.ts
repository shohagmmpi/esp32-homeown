import { useSyncExternalStore } from "react";

export type Mode = "IR" | "BLE" | "WIFI" | "MQTT";

export type RelayIr = { hex: string; dec: string; raw: string };

export type AppState = {
  mode: Mode;
  relays: boolean[];
  ir: RelayIr[];
  ble: { name: string; address: string };
  wifi: {
    ssid: string;
    password: string;
    ipMode: "dhcp" | "static";
    ip: string;
    gw: string;
    mask: string;
  };
  mqtt: {
    server: string;
    port: string;
    user: string;
    pass: string;
    topic: string;
    codes: string[];
  };
};

const KEY = "relay-bench-v1";

function blankIr(): RelayIr[] {
  return Array.from({ length: 16 }, () => ({ hex: "", dec: "", raw: "" }));
}

function blankCodes(): string[] {
  return Array.from({ length: 16 }, () => "");
}

export function defaultState(): AppState {
  return {
    mode: "WIFI",
    relays: Array.from({ length: 16 }, () => false),
    ir: blankIr(),
    ble: { name: "", address: "" },
    wifi: {
      ssid: "",
      password: "",
      ipMode: "dhcp",
      ip: "192.168.1.80",
      gw: "192.168.1.1",
      mask: "255.255.255.0",
    },
    mqtt: {
      server: "",
      port: "1883",
      user: "",
      pass: "",
      topic: "relay/control",
      codes: blankCodes(),
    },
  };
}

const SERVER_SNAPSHOT = defaultState();

function load(): AppState {
  if (typeof window === "undefined") return SERVER_SNAPSHOT;
  try {
    const raw = localStorage.getItem(KEY);
    if (!raw) return defaultState();
    const parsed = JSON.parse(raw) as Partial<AppState>;
    const base = defaultState();
    return {
      ...base,
      ...parsed,
      relays: Array.isArray(parsed.relays) && parsed.relays.length === 16 ? parsed.relays : base.relays,
      ir: Array.isArray(parsed.ir) && parsed.ir.length === 16 ? parsed.ir : base.ir,
      mqtt: {
        ...base.mqtt,
        ...(parsed.mqtt ?? {}),
        codes:
          parsed.mqtt && Array.isArray(parsed.mqtt.codes) && parsed.mqtt.codes.length === 16
            ? parsed.mqtt.codes
            : base.mqtt.codes,
      },
      ble: { ...base.ble, ...(parsed.ble ?? {}) },
      wifi: { ...base.wifi, ...(parsed.wifi ?? {}) },
    };
  } catch {
    return defaultState();
  }
}

let state = load();
const listeners = new Set<() => void>();

function emit() {
  listeners.forEach((l) => l());
  try {
    localStorage.setItem(KEY, JSON.stringify(state));
  } catch {
    /* ignore quota */
  }
}

export function getState() {
  return state;
}

export function patch(partial: Partial<AppState>) {
  state = { ...state, ...partial };
  emit();
}

export function setMode(mode: Mode) {
  patch({ mode });
}

export function toggleRelay(i: number) {
  const relays = state.relays.slice();
  relays[i] = !relays[i];
  patch({ relays });
}

export function setAll(on: boolean) {
  patch({ relays: Array.from({ length: 16 }, () => on) });
}

export function setIr(i: number, field: keyof RelayIr, value: string) {
  const ir = state.ir.map((row, idx) => (idx === i ? { ...row, [field]: value } : row));
  patch({ ir });
}

export function setIrRow(i: number, row: RelayIr) {
  const ir = state.ir.map((r, idx) => (idx === i ? row : r));
  patch({ ir });
}

export function setBle(ble: AppState["ble"]) {
  patch({ ble });
}

export function setWifi(wifi: AppState["wifi"]) {
  patch({ wifi });
}

export function setMqtt(mqtt: AppState["mqtt"]) {
  patch({ mqtt });
}

export function setMqttCode(i: number, code: string) {
  const codes = state.mqtt.codes.slice();
  codes[i] = code;
  patch({ mqtt: { ...state.mqtt, codes } });
}

function subscribe(cb: () => void) {
  listeners.add(cb);
  return () => listeners.delete(cb);
}

export function useRelayStore() {
  return useSyncExternalStore(subscribe, getState, () => SERVER_SNAPSHOT);
}
