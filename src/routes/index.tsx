import { createFileRoute } from "@tanstack/react-router";
import { Shell } from "@/components/shell";
import { setAll, toggleRelay, useRelayStore } from "@/lib/relay-store";

export const Route = createFileRoute("/")({ component: Home });

function Home() {
  const s = useRelayStore();
  const link =
    s.mode === "BLE"
      ? s.ble.address || "No BLE address saved"
      : s.mode === "WIFI"
        ? s.wifi.ssid || "No SSID saved"
        : s.mode === "MQTT"
          ? s.mqtt.server || "No broker saved"
          : `${s.ir.filter((r) => r.hex || r.dec || r.raw).length} IR codes stored`;

  return (
    <Shell>
      <p className="mb-3 text-sm text-muted">
        Link · <span className="font-mono text-fg">{link}</span>
      </p>
      <div className="mb-4 grid grid-cols-2 gap-2">
        <button
          type="button"
          onClick={() => setAll(true)}
          className="min-h-12 rounded-xl bg-primary font-semibold text-primary-fg"
        >
          All on
        </button>
        <button
          type="button"
          onClick={() => setAll(false)}
          className="min-h-12 rounded-xl bg-danger font-semibold text-danger-fg"
        >
          All off
        </button>
      </div>
      <div className="grid grid-cols-2 gap-2 sm:grid-cols-4">
        {s.relays.map((on, i) => (
          <button
            key={i}
            type="button"
            onClick={() => toggleRelay(i)}
            className={`flex min-h-20 flex-col items-center justify-center rounded-xl border ${
              on ? "border-primary bg-surface-2 text-primary" : "border-border bg-surface text-muted"
            }`}
          >
            <span className={`mb-2 h-2.5 w-2.5 rounded-full ${on ? "bg-primary" : "bg-border"}`} />
            <span className="text-sm font-semibold text-fg">Relay {i + 1}</span>
            <span className="font-mono text-xs">{on ? "ON" : "OFF"}</span>
          </button>
        ))}
      </div>
    </Shell>
  );
}
