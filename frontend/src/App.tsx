import React, { useMemo, useState } from "react";
import { useWS } from "./hooks/useWS";
import { parseLine, updateBest, Best, Trade } from "./lib/parse";
import { LineChart, Line, XAxis, YAxis, Tooltip, CartesianGrid, ResponsiveContainer } from "recharts";
import "./App.css";

const WS_URL = "ws://localhost:8081";

// tiny helper for percentiles on the latency series
function pct(arr: {rtt:number}[], p: number) {
  if (arr.length === 0) return 0;
  const copy = arr.map(x => x.rtt).slice().sort((a,b)=>a-b);
  const idx = Math.max(0, Math.min(copy.length-1, Math.floor((copy.length-1) * p)));
  return copy[idx];
}

export default function App() {
  const { connected, lastMsg } = useWS(WS_URL);
  const [best, setBest] = useState<Best>({});
  const [tape, setTape] = useState<Trade[]>([]);
  const [lat, setLat] = useState<{ t: number; rtt: number }[]>([]);

  useMemo(() => {
    if (!lastMsg) return;
    for (const lineRaw of lastMsg.split("\n")) {
      const line = lineRaw.trim();
      if (!line) continue;
      const msg = parseLine(line);
      if (msg.type === "BEST_BID" || msg.type === "BEST_ASK") {
        setBest(b => updateBest(b, msg));
      } else if (msg.type === "TRADE") {
        setTape(t => [{ side: "UNK" as const, px: msg.px!, qty: msg.qty!, ts: Date.now() }, ...t].slice(0, 200));
      } else if (line.startsWith("RTT")) {
        const parts = line.split(/\s+/);
        const rtt = Number(parts[1]);
        if (!Number.isNaN(rtt)) {
          setLat(d => [...d.slice(-299), { t: Date.now(), rtt }]); // last 300 points
        }
      }
    }
  }, [lastMsg]);

  const lastTrade = tape[0];
  const p50 = pct(lat, 0.50);
  const p95 = pct(lat, 0.95);

  return (
    <div className="shell">
      {/* Top bar */}
      <header className="topbar">
        <div className="brand">LL Trading Simulator</div>
        <div className={connected ? "pill ok" : "pill bad"}>
          {connected ? "WebSocket: Connected" : "WebSocket: Disconnected"}
        </div>
      </header>

      {/* KPI tiles */}
      <section className="kpis">
        <div className="kpi">
          <div className="kpi-label">Best Bid</div>
          <div className="kpi-value bid">
            {best.bidPx?.toFixed(2) ?? "—"} <span className="kpi-qty">{best.bidQty ?? "—"}</span>
          </div>
        </div>
        <div className="kpi">
          <div className="kpi-label">Best Ask</div>
          <div className="kpi-value ask">
            {best.askPx?.toFixed(2) ?? "—"} <span className="kpi-qty">{best.askQty ?? "—"}</span>
          </div>
        </div>
        <div className="kpi">
          <div className="kpi-label">Last Trade</div>
          <div className="kpi-value mono">{lastTrade ? `${lastTrade.px.toFixed(2)} × ${lastTrade.qty}` : "—"}</div>
        </div>
        <div className="kpi">
          <div className="kpi-label">RTT p50 / p95 (µs)</div>
          <div className="kpi-value mono">{p50} / {p95}</div>
        </div>
      </section>

      {/* Main grid */}
      <main className="grid">
        <section className="card">
          <div className="card-title">Top of Book</div>
          <table className="book">
            <thead>
              <tr><th>Bid Qty</th><th>Bid Px</th><th>Ask Px</th><th>Ask Qty</th></tr>
            </thead>
            <tbody>
              <tr>
                <td className="bid mono">{best.bidQty ?? "—"}</td>
                <td className="bid mono">{best.bidPx?.toFixed(2) ?? "—"}</td>
                <td className="ask mono">{best.askPx?.toFixed(2) ?? "—"}</td>
                <td className="ask mono">{best.askQty ?? "—"}</td>
              </tr>
            </tbody>
          </table>
        </section>

        <section className="card">
          <div className="card-title">Recent Trades</div>
          <div className="trade-table">
            <table>
              <thead>
                <tr><th>Time</th><th>Side</th><th>Price</th><th>Qty</th></tr>
              </thead>
              <tbody>
                {tape.slice(0, 50).map((tr, i) => (
                  <tr key={i} className={(tr.side ?? "UNK").toLowerCase()}>
                    <td className="mono">{new Date(tr.ts).toLocaleTimeString()}</td>
                    <td className="mono">{tr.side}</td>
                    <td className="mono">{tr.px.toFixed(2)}</td>
                    <td className="mono">{tr.qty}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        </section>

        <section className="card">
          <div className="card-title">Latency (RTT µs)</div>
          <div className="chart">
            <ResponsiveContainer width="100%" height="100%">
              <LineChart data={lat}>
                <CartesianGrid strokeDasharray="3 3" />
                <XAxis dataKey="t" tickFormatter={() => ""} />
                <YAxis />
                <Tooltip />
                <Line type="monotone" dataKey="rtt" dot={false} />
              </LineChart>
            </ResponsiveContainer>
          </div>
        </section>
      </main>
    </div>
  );
}