export class SignalingRoom {
    constructor(state, env) {
        this.state = state;
        this.sessions = [];
    }

    async fetch(request) {
        const upgradeHeader = request.headers.get("Upgrade");
        if (!upgradeHeader || upgradeHeader.toLowerCase() !== "websocket") {
            return new Response("Expected Upgrade: websocket", { status: 426 });
        }

        const { 0: client, 1: server } = new WebSocketPair();
        server.accept();

        let role = "offerer";
        if (this.sessions.length > 0) {
            role = "answerer";
        }

        // Limit room to 2 peers
        if (this.sessions.length >= 2) {
            server.send(JSON.stringify({ type: "error", message: "Room is full" }));
            server.close();
            return new Response(null, { status: 101, webSocket: client });
        }

        const session = { ws: server, role: role };
        this.sessions.push(session);

        server.send(JSON.stringify({ type: "role", role: role }));

        if (role === "answerer" && this.sessions.length > 1) {
            this.sessions[0].ws.send(JSON.stringify({ type: "peer_joined" }));
        }

        let timeoutId;
        const resetTimeout = () => {
            if (timeoutId) clearTimeout(timeoutId);
            timeoutId = setTimeout(() => {
                try { server.close(1000, "Activity timeout"); } catch (e) { }
            }, 60000); // 60 seconds timeout
        };
        resetTimeout();

        server.addEventListener("message", event => {
            resetTimeout();
            try {
                // Relay message to the other peer(s)
                for (const s of this.sessions) {
                    if (s.ws !== server) {
                        s.ws.send(event.data);
                    }
                }
            } catch (err) {
                console.error("Relay error:", err);
            }
        });

        server.addEventListener("close", () => {
            if (timeoutId) clearTimeout(timeoutId);
            this.sessions = this.sessions.filter(s => s.ws !== server);
            for (const s of this.sessions) {
                try {
                    s.ws.send(JSON.stringify({ type: "peer_left" }));
                } catch (e) { }
            }
        });

        server.addEventListener("error", () => {
            if (timeoutId) clearTimeout(timeoutId);
            this.sessions = this.sessions.filter(s => s.ws !== server);
        });

        return new Response(null, { status: 101, webSocket: client });
    }
}

export default {
    async fetch(request, env, ctx) {
        try {
            const url = new URL(request.url);
            const roomId = url.pathname || "/default-room";

            if (!env.SIGNALING_ROOM) {
                return new Response("Durable Object binding 'SIGNALING_ROOM' not found. Ensure wrangler.toml is configured correctly.", { status: 500 });
            }

            // Route the request to the specific Durable Object instance for this room
            const id = env.SIGNALING_ROOM.idFromName(roomId);
            const stub = env.SIGNALING_ROOM.get(id);

            return stub.fetch(request);
        } catch (err) {
            return new Response(err.message, { status: 500 });
        }
    }
};
