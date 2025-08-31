export type Best  = { bidPx?: number; bidQty?: number; askPx?: number; askQty?: number };
export type Trade = { side: "BUY" | "SELL" | "UNK"; px: number; qty: number; ts: number };

export function parseLine(line: string) {
  if (line.startsWith("BEST_BID")) {
    const [, px, , qty] = line.split(/\s+/);
    return { type: "BEST_BID", px: parseFloat(px), qty: parseInt(qty, 10) };
  }
  if (line.startsWith("BEST_ASK")) {
    const [, px, , qty] = line.split(/\s+/);
    return { type: "BEST_ASK", px: parseFloat(px), qty: parseInt(qty, 10) };
  }
  // e.g. "TRADE BUY 35 @ 50.21" (optionally "... against id 123")
  const m = line.match(/^TRADE\s+(BUY|SELL)\s+(\d+)\s+@\s+([\d.]+)/);
  if (m) {
    const side = m[1] as "BUY" | "SELL";
    const qty  = parseInt(m[2], 10);
    const px   = parseFloat(m[3]);
    return { type: "TRADE", side, qty, px } as const;
  }
  return { type: "OTHER", raw: line };
}

export function updateBest(prev: Best, msg: any): Best {
  if (msg.type === "BEST_BID") return { ...prev, bidPx: msg.px, bidQty: msg.qty };
  if (msg.type === "BEST_ASK") return { ...prev, askPx: msg.px, askQty: msg.qty };
  return prev;
}