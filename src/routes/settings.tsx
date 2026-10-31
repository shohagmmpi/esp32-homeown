import { createFileRoute } from "@tanstack/react-router";
import { useMemo, useState } from "react";
import { Bluetooth, Radio, Router, Satellite } from "lucide-react";
import { Shell } from "@/components/shell";
import {
  setBle,
  setIr,
  setIrRow,
  setMode,
  setMqtt,
  setMqttCode,
  setWifi,
  useRelayStore,
  type Mode,
} from "@/lib/relay-store";

export const Route = createFileRoute("/settings")({ component: SettingsPage });

const MODES: { id: Mode; label: string; icon: typeof Radio }[] = [
  { id: "IR", label: "IR", icon: Radio },
  { id: "BLE", label: "BLE", icon: Bluetooth },
  { id: "WIFI", label: "WiFi", icon: Router },
  { id: "MQTT", label: "MQTT", icon: Satellite },
];

type BleHit = { name: string; address: string; rssi: number; esp: boolean };
type ApHit = { ssid: string; rssi: number; secure: boolean };

const BLE_POOL: BleHit[] = [
  { name: "ESP32-Relay", address: "A4:CF:12:8B:21:04", rssi: -42, esp: true },
  { name: "ESP32-Relay-2", address: "3C:61:05:11:9E:7A", rssi: -61, esp: true },
  { name: "ESP32-S3-Bench", address: "24:0A:C4:66:D0:19", rssi: -73, esp: true },
  { name: "Galaxy Buds", address: "88:C6:26:10:AA:01", rssi: -55, esp: false },
  { name: "Mi Band", address: "E4:AA:EC:02:33:90", rssi: -68, esp: false },
];

const AP_POOL: ApHit[] = [
  { ssid: "Home-2.4G", rssi: -38, secure: true },
  { ssid: "Office-WiFi", rssi: -52, secure: true },
  { ssid: "ESP32-Relay-Control", rssi: -29, secure: true },
  { ssid: "Guest", rssi: -71, secure: false },
  { ssid: "BTCL-Fiber", rssi: -64, secure: true },
];

function randNec() {
  const n = Math.floor(Math.random() * 0xffffffff);
  const hex = n.toString(16).toUpperCase().padStart(8, "0");
  const pulses = Array.from({ length: 12 }, () => (Math.random() > 0.5 ? 560 : 1690));
  const raw = `9000,4500,${pulses.join(",")},560`;
  return { hex, dec: String(n >>> 0), raw };
}

function SettingsPage() {
  const s = useRelayStore();
  const [bleHits, setBleHits] = useState<BleHit[] | null>(null);
  const [apHits, setApHits] = useState<ApHit[] | null>(null);
  const [scanning, setScanning] = useState<"ble" | "wifi" | null>(null);
  const [learning, setLearning] = useState<number | null>(null);
  const [note, setNote] = useState("");

  const modeHint = useMemo(() => {
    if (s.mode === "IR") return "Store HEX, DEC and raw dump for every relay.";
    if (s.mode === "BLE") return "Scan nearby BLE and keep the ESP32 address.";
    if (s.mode === "WIFI") return "Scan SSIDs, then save the password and IP.";
    return "Save the broker, then a payload code for each relay.";
  }, [s.mode]);

  function pickMode(id: Mode) {
    setMode(id);
    setNote("");
  }

  function scanBle() {
    setScanning("ble");
    setBleHits(null);
    window.setTimeout(() => {
      const shuffled = [...BLE_POOL].sort(() => Math.random() - 0.5);
      setBleHits(shuffled.slice(0, 4));
      setScanning(null);
    }, 900);
  }

  function scanWifi() {
    setScanning("wifi");
    setApHits(null);
    window.setTimeout(() => {
      setApHits([...AP_POOL].sort((a, b) => b.rssi - a.rssi));
      setScanning(null);
    }, 800);
  }

  function learn(i: number) {
    if (learning !== null) return;
    setLearning(i);
    window.setTimeout(() => {
      const code = randNec();
      setIrRow(i, code);
      setLearning(null);
      setNote(`Relay ${i + 1} learned ${code.hex}`);
    }, 1100);
  }

  return (
    <Shell>
      <div className="grid grid-cols-4 gap-2">
        {MODES.map(({ id, label, icon: Icon }) => {
          const on = s.mode === id;
          return (
            <button
              key={id}
              type="button"
              onClick={() => pickMode(id)}
              className={`flex min-h-16 flex-col items-center justify-center gap-1 rounded-xl text-sm font-semibold ${
                on ? "bg-primary text-primary-fg" : "bg-surface text-muted"
              }`}
            >
              <Icon size={18} />
              {label}
            </button>
          );
        })}
      </div>
      <p className="mt-3 mb-4 text-sm text-muted">{modeHint}</p>
      {note ? <p className="mb-3 rounded-lg bg-surface px-3 py-2 text-sm text-primary">{note}</p> : null}

      {s.mode === "IR" ? (
        <div className="space-y-3">
          {s.ir.map((row, i) => (
            <section key={i} className="rounded-xl border border-border bg-surface p-3">
              <h2 className="mb-2 text-sm font-semibold">Relay {i + 1}</h2>
              <label className="mb-2 block text-xs text-muted">
                HEX
                <input
                  value={row.hex}
                  onChange={(e) => setIr(i, "hex", e.target.value.toUpperCase())}
                  className="mt-1 w-full rounded-lg border border-border bg-bg px-3 py-2 font-mono text-sm text-fg"
                  placeholder="00FFA857"
                  spellCheck={false}
                />
              </label>
              <label className="mb-2 block text-xs text-muted">
                DEC
                <input
                  value={row.dec}
                  onChange={(e) => setIr(i, "dec", e.target.value.replace(/[^\d]/g, ""))}
                  className="mt-1 w-full rounded-lg border border-border bg-bg px-3 py-2 font-mono text-sm text-fg"
                  placeholder="16724175"
                  inputMode="numeric"
                />
              </label>
              <label className="mb-2 block text-xs text-muted">
                RAW dump
                <textarea
                  value={row.raw}
                  onChange={(e) => setIr(i, "raw", e.target.value)}
                  rows={2}
                  className="mt-1 w-full resize-y rounded-lg border border-border bg-bg px-3 py-2 font-mono text-xs text-fg"
                  placeholder="9000,4500,560,1690,..."
                />
              </label>
              <button
                type="button"
                onClick={() => learn(i)}
                className={`min-h-11 w-full rounded-lg bg-surface-2 font-semibold text-fg ${learning === i ? "learning text-warn" : ""}`}
              >
                {learning === i ? "Listening…" : "Learn from remote"}
              </button>
            </section>
          ))}
        </div>
      ) : null}

      {s.mode === "BLE" ? (
        <div className="space-y-3">
          <button
            type="button"
            onClick={scanBle}
            disabled={scanning === "ble"}
            className="min-h-12 w-full rounded-xl bg-primary font-semibold text-primary-fg disabled:opacity-60"
          >
            {scanning === "ble" ? "Scanning…" : "Scan BLE devices"}
          </button>
          {s.ble.address ? (
            <p className="rounded-xl border border-primary bg-surface px-3 py-3 text-sm">
              Saved · <span className="font-semibold">{s.ble.name}</span>
              <span className="mt-1 block font-mono text-primary">{s.ble.address}</span>
            </p>
          ) : (
            <p className="text-sm text-muted">No ESP32 address stored yet.</p>
          )}
          {bleHits?.map((d) => (
            <button
              key={d.address}
              type="button"
              disabled={!d.esp}
              onClick={() => {
                setBle({ name: d.name, address: d.address });
                setNote(`Stored ${d.address}`);
              }}
              className="flex w-full items-center justify-between rounded-xl border border-border bg-surface px-3 py-3 text-left disabled:opacity-40"
            >
              <span>
                <span className="block font-semibold">{d.name}</span>
                <span className="font-mono text-xs text-muted">{d.address}</span>
              </span>
              <span className="font-mono text-xs text-muted">{d.rssi} dBm</span>
            </button>
          ))}
        </div>
      ) : null}

      {s.mode === "WIFI" ? (
        <div className="space-y-3">
          <button
            type="button"
            onClick={scanWifi}
            disabled={scanning === "wifi"}
            className="min-h-12 w-full rounded-xl bg-primary font-semibold text-primary-fg disabled:opacity-60"
          >
            {scanning === "wifi" ? "Scanning…" : "Scan Wi-Fi networks"}
          </button>
          {apHits?.map((ap) => (
            <button
              key={ap.ssid}
              type="button"
              onClick={() => setWifi({ ...s.wifi, ssid: ap.ssid })}
              className={`flex w-full items-center justify-between rounded-xl border px-3 py-3 text-left ${
                s.wifi.ssid === ap.ssid ? "border-primary bg-surface-2" : "border-border bg-surface"
              }`}
            >
              <span className="font-semibold">{ap.ssid}</span>
              <span className="font-mono text-xs text-muted">
                {ap.rssi} dBm {ap.secure ? "· lock" : "· open"}
              </span>
            </button>
          ))}
          <label className="block text-xs text-muted">
            SSID
            <input
              value={s.wifi.ssid}
              onChange={(e) => setWifi({ ...s.wifi, ssid: e.target.value })}
              className="mt-1 w-full rounded-lg border border-border bg-bg px-3 py-2 text-sm text-fg"
            />
          </label>
          <label className="block text-xs text-muted">
            Password
            <input
              type="password"
              value={s.wifi.password}
              onChange={(e) => setWifi({ ...s.wifi, password: e.target.value })}
              className="mt-1 w-full rounded-lg border border-border bg-bg px-3 py-2 text-sm text-fg"
            />
          </label>
          <label className="block text-xs text-muted">
            IP mode
            <select
              value={s.wifi.ipMode}
              onChange={(e) => setWifi({ ...s.wifi, ipMode: e.target.value as "dhcp" | "static" })}
              className="mt-1 w-full rounded-lg border border-border bg-bg px-3 py-2 text-sm text-fg"
            >
              <option value="dhcp">DHCP</option>
              <option value="static">Static IP</option>
            </select>
          </label>
          {s.wifi.ipMode === "static" ? (
            <div className="grid gap-2">
              {(
                [
                  ["ip", "Static IP"],
                  ["gw", "Gateway"],
                  ["mask", "Subnet mask"],
                ] as const
              ).map(([key, label]) => (
                <label key={key} className="block text-xs text-muted">
                  {label}
                  <input
                    value={s.wifi[key]}
                    onChange={(e) => setWifi({ ...s.wifi, [key]: e.target.value })}
                    className="mt-1 w-full rounded-lg border border-border bg-bg px-3 py-2 font-mono text-sm text-fg"
                  />
                </label>
              ))}
            </div>
          ) : null}
          {s.wifi.ssid ? (
            <p className="text-sm text-primary">Saved network {s.wifi.ssid}</p>
          ) : null}
        </div>
      ) : null}

      {s.mode === "MQTT" ? (
        <div className="space-y-3">
          {(
            [
              ["server", "Broker", "text"],
              ["port", "Port", "text"],
              ["user", "Username", "text"],
              ["pass", "Password", "password"],
              ["topic", "Base topic", "text"],
            ] as const
          ).map(([key, label, type]) => (
            <label key={key} className="block text-xs text-muted">
              {label}
              <input
                type={type}
                value={s.mqtt[key]}
                onChange={(e) => setMqtt({ ...s.mqtt, [key]: e.target.value })}
                className="mt-1 w-full rounded-lg border border-border bg-bg px-3 py-2 font-mono text-sm text-fg"
              />
            </label>
          ))}
          <h2 className="pt-2 text-sm font-semibold">Relay payload codes</h2>
          {s.mqtt.codes.map((code, i) => (
            <label key={i} className="block text-xs text-muted">
              Relay {i + 1}
              <input
                value={code}
                onChange={(e) => setMqttCode(i, e.target.value)}
                placeholder={`${s.mqtt.topic || "relay"}/${i + 1}`}
                className="mt-1 w-full rounded-lg border border-border bg-bg px-3 py-2 font-mono text-sm text-fg"
              />
            </label>
          ))}
        </div>
      ) : null}
    </Shell>
  );
}
