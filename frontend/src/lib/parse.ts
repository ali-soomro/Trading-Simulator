export type Best = { bidPx?: number; bidQty?: number; askPx?: number; askQty?: number };
export type Trade = { side: "BUY" | "SELL" | "UNK"; px: number; qty: number; ts: number };

export function parseLine(line: string) {
  if (line.startsWith("BEST_BID")) {
    const [, px, , qty] = line.split(/\s+/);
    return { type: "BEST_BID", px: parseFloat(px), qty: parseInt(qty) };
  }
  if (line.startsWith("BEST_ASK")) {
    const [, px, , qty] = line.split(/\s+/);
    return { type: "BEST_ASK", px: parseFloat(px), qty: parseInt(qty) };
  }
  if (line.startsWith("TRADE")) {
    const m = line.match(/^TRADE\s+(\d+)\s+@\s+([\d.]+)/);
    if (m) {
      const qty = parseInt(m[1], 10);
      const px = parseFloat(m[2]);
      return { type: "TRADE", qty, px };
    }
  }
  return { type: "OTHER", raw: line };
}

export function updateBest(prev: Best, msg: any): Best {
  if (msg.type === "BEST_BID") return { ...prev, bidPx: msg.px, bidQty: msg.qty };
  if (msg.type === "BEST_ASK") return { ...prev, askPx: msg.px, askQty: msg.qty };
  return prev;
}