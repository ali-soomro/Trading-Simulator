import React, { useMemo, useState } from "react";
import { useWS } from "./hooks/useWS";
import { parseLine, updateBest, Best, Trade } from "./lib/parse";
import { LineChart, Line, XAxis, YAxis, Tooltip, CartesianGrid } from "recharts";
import "./App.css";

const WS_URL = "ws://localhost:8081";

export default function App() {
  const { connected, lastMsg } = useWS(WS_URL);
  const [best, setBest] = useState<Best>({});
  const [tape, setTape] = useState<Trade[]>([]);
  const [lat, setLat] = useState<{ t: number; rtt: number }[]>([]);

  useMemo(() => {
    if (!lastMsg) return;
    for (const line of lastMsg.split("\n")) {
      const msg = parseLine(line.trim());
      if (msg.type === "BEST_BID" || msg.type === "BEST_ASK") {
        setBest(b => updateBest(b, msg));
      } else if (msg.type === "TRADE") {
        setTape(t => [{ side: "UNK", px: msg.px, qty: msg.qty, ts: Date.now() }, ...t].slice(0, 200));
      } else if (line.startsWith("RTT")) {
        // if you later send "RTT <micros>" messages from bot
        const parts = line.split(/\s+/);
        const rtt = Number(parts[1]);
        setLat(d => [...d.slice(-199), { t: Date.now(), rtt }]);
      }
    }
  }, [lastMsg]);

  return (
    <div className="container">
      <header>
        <h1>Trading Simulator Dashboard</h1>
        <div className={connected ? "pill ok" : "pill bad"}>
          {connected ? "Connected" : "Disconnected"}
        </div>
      </header>

      <main className="grid">
        <section className="card">
          <h2>Top of Book</h2>
          <table className="book">
            <thead>
              <tr><th>Bid Qty</th><th>Bid Px</th><th>Ask Px</th><th>Ask Qty</th></tr>
            </thead>
            <tbody>
              <tr>
                <td className="bid">{best.bidQty ?? "-"}</td>
                <td className="bid">{best.bidPx?.toFixed(2) ?? "-"}</td>
                <td className="ask">{best.askPx?.toFixed(2) ?? "-"}</td>
                <td className="ask">{best.askQty ?? "-"}</td>
              </tr>
            </tbody>
          </table>
        </section>

        <section className="card">
          <h2>Trades (last 50)</h2>
          <div className="tape">
            {tape.slice(0, 50).map((tr, i) => (
              <div key={i} className={tr.side === "BUY" ? "buy" : "sell"}>
                {tr.qty} @ {tr.px.toFixed(2)}
              </div>
            ))}
          </div>
        </section>

        <section className="card">
          <h2>Latency (RTT μs)</h2>
          <LineChart width={420} height={220} data={lat}>
            <CartesianGrid strokeDasharray="3 3" />
            <XAxis dataKey="t" tickFormatter={() => ""} />
            <YAxis />
            <Tooltip />
            <Line type="monotone" dataKey="rtt" dot={false} />
          </LineChart>
        </section>
      </main>
    </div>
  );
}