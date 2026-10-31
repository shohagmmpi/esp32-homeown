import { Link, useRouterState } from "@tanstack/react-router";
import { LayoutGrid, Settings2 } from "lucide-react";
import type { ReactNode } from "react";
import { useRelayStore } from "@/lib/relay-store";

export function Shell({ children }: { children: ReactNode }) {
  const path = useRouterState({ select: (s) => s.location.pathname });
  const mode = useRelayStore().mode;
  const onDash = path === "/";
  return (
    <div className="mx-auto flex min-h-screen max-w-lg flex-col bg-bg text-fg">
      <header className="sticky top-0 z-10 flex items-center justify-between border-b border-border bg-bg/95 px-4 py-3 backdrop-blur">
        <div>
          <p className="text-xs font-medium tracking-wide text-muted">ESP32 bench</p>
          <h1 className="text-lg font-semibold leading-tight">Relay Bench</h1>
        </div>
        <span className="rounded-full bg-surface px-3 py-1 font-mono text-xs text-primary">{mode}</span>
      </header>
      <main className="flex-1 px-4 py-4">{children}</main>
      <nav className="sticky bottom-0 grid grid-cols-2 border-t border-border bg-surface">
        <Link
          to="/"
          className={`flex min-h-14 items-center justify-center gap-2 text-sm font-medium ${onDash ? "text-primary" : "text-muted"}`}
        >
          <LayoutGrid size={18} /> Relays
        </Link>
        <Link
          to="/settings"
          className={`flex min-h-14 items-center justify-center gap-2 text-sm font-medium ${!onDash ? "text-primary" : "text-muted"}`}
        >
          <Settings2 size={18} /> Settings
        </Link>
      </nav>
    </div>
  );
}
