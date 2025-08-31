import { useEffect, useRef, useState } from "react";

export function useWS(url: string) {
  const wsRef = useRef<WebSocket | null>(null);
  const [connected, setConnected] = useState(false);
  const [lastMsg, setLastMsg] = useState<string>("");

  useEffect(() => {
    const ws = new WebSocket(url);
    wsRef.current = ws;
    ws.onopen = () => setConnected(true);
    ws.onclose = () => setConnected(false);
    ws.onmessage = e => setLastMsg(e.data as string);
    return () => ws.close();
  }, [url]);

  return { connected, lastMsg };
}