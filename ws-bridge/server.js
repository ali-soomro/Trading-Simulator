const dgram = require("dgram");
const WebSocket = require("ws");

const udpPort = 9001;
const wsPort = 8081;

// UDP listener
const udpServer = dgram.createSocket("udp4");
udpServer.bind(udpPort, () => console.log(`Listening for UDP on ${udpPort}`));

// WebSocket server
const wss = new WebSocket.Server({ port: wsPort });
console.log(`WebSocket server on ws://localhost:${wsPort}`);

// Forward UDP → WebSocket
udpServer.on("message", (msg) => {
  const data = msg.toString();
  for (const client of wss.clients) {
    if (client.readyState === WebSocket.OPEN) {
      client.send(data);
    }
  }
});