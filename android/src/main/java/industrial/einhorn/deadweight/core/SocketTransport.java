package industrial.einhorn.deadweight.core;

import java.io.DataInputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.Socket;

/** Plain java.net.Socket transport with frame reassembly (TCP may split/merge frames arbitrarily). */
public final class SocketTransport implements Transport {
    private final String host;
    private final int port;
    private final int connectTimeoutMs;
    private Socket socket;
    private DataInputStream in;
    private OutputStream out;

    public SocketTransport(String host, int port, int connectTimeoutMs) {
        this.host = host; this.port = port; this.connectTimeoutMs = connectTimeoutMs;
    }

    @Override public void connect() throws IOException {
        Socket s = new Socket();
        s.setTcpNoDelay(true);
        s.connect(new InetSocketAddress(host, port), connectTimeoutMs);
        synchronized (this) { socket = s; in = new DataInputStream(s.getInputStream()); out = s.getOutputStream(); }
    }

    @Override public void send(byte[] frame) throws IOException {
        OutputStream o;
        synchronized (this) { o = out; }
        if (o == null) throw new IOException("not connected");
        synchronized (o) { o.write(frame); o.flush(); }
    }

    @Override public byte[] readFrame() throws IOException, ProtocolException {
        int lo = in.read();
        if (lo < 0) return null;
        int hi = in.read();
        if (hi < 0) throw new IOException("EOF inside frame header");
        int len = lo | (hi << 8);
        if (len < 1 || len > Protocol.MAX_FRAME) throw new ProtocolException("bad frame length " + len);
        byte[] p = new byte[len];
        in.readFully(p);
        return p;
    }

    @Override public synchronized void close() {
        try { if (socket != null) socket.close(); } catch (IOException ignored) { }
    }
}
