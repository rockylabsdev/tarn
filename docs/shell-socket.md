# The Shell socket: how a shell or bar shows a Tarn Recording

The `tarn daemon` reports a running Recording on a Unix socket, so a shell or bar (Waybar, Quickshell, eww, a script) can show it and stop it. Why the contract is a socket: [ADR 0002](adr/0002-island-connects-to-tarn.md) and [ADR 0004](adr/0004-shell-contracts-are-files-and-a-socket.md). Tarn owns this document; a client implements the other side. [`examples/waybar/`](../examples/waybar/) is a complete client in a few lines of shell.

## The connection

- The socket is `$XDG_RUNTIME_DIR/tarn/shell.sock` (Qt `QLocalServer`, a stream socket, private to the user). `$XDG_RUNTIME_DIR` is the per-login folder Linux clears at logout, so the socket never outlives the session. There is no other name for it.
- A client connects as a plain client. If the socket isn't there, or the connection drops (the daemon quit, crashed or restarted), the client shows nothing and tries again, every 2 s is plenty.
- Messages are one JSON object per line (UTF-8, `\n`-terminated), in both directions. Each side ignores keys and values it doesn't know, and lines that aren't JSON objects, so either can add to the protocol without breaking the other.
- **Presence comes from the connection.** While at least one client is connected, Tarn knows a shell is listening; when the last one disconnects, it knows none is. Tarn behaves the same either way: its own Recording bar shows with or without a client. Any number of clients may connect at once.
- On every new connection Tarn sends the current state straight away, so a client that starts or restarts mid-recording picks it up.

## Tarn to the client

One line on every change of state, and on connect:

```json
{"recording": "recording", "elapsedMs": 7012, "format": "gif"}
{"recording": "encoding", "elapsedMs": 15230, "format": "gif", "progress": 0.42}
```

| `recording` | Meaning | A client would show |
|---|---|---|
| `idle` | Nothing is recording. Sent on connect when idle. | Nothing. |
| `starting` | The recorder is starting up (about 0.45 s), after the Record panel or a restart. | Nothing at first start. After a restart, the same as before, dimmed. |
| `recording` | The recorder is writing. | An indicator (a red dot) and the time, counted up from `elapsedMs`. |
| `paused` | Paused from Tarn's Recording bar. | A pause mark; the time holds at `elapsedMs`. |
| `encoding` | Stopped, and the GIF is being made. `progress` runs from 0 to 1, sent at most 10 times a second. Never sent for an MP4. | Progress ("GIF 42%"). Stopping is no longer possible. |
| `done` | Finished. The Thumbnail is on screen. | Nothing: the activity is over. |
| `deleted` | Thrown away from the Recording bar. | Nothing: the activity is over. |

Fields:

- `recording` (string): one of the states above. Always present.
- `elapsedMs` (integer): the recorded time so far, not counting pauses. Present in every state but `idle`.
- `format` (`"gif"` or `"mp4"`): present in every state but `idle`.
- `progress` (number, 0 to 1): `encoding` only.

A client treats an unknown `recording` value as `idle`, so a state added later hides the indicator instead of breaking it.

## Client to Tarn

```json
{"stop": true}
```

Sent while `recording` or `paused`, this finishes the Recording exactly as the record key would. Sent in any other state it does nothing, and it is not queued: a stop sent during `starting` is ignored. Pause, restart and delete live only on Tarn's Recording bar; a client sends nothing else.

A client that only wants to stop needs no read loop: connect, write `{"stop":true}` and a newline, close.

## Island, the reference client

Island, the author's Quickshell shell, is the first client. It shows a Recording as a circle with a red dot, a pause glyph while paused and a progress ring while a GIF is made, and a click on the circle sends the stop line.

## Not in v1

- The Timer's Countdown on the socket. A later `{"timer": …}` message could add it without changing anything above.
- Pause, restart or delete from a client.
