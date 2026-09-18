package industrial.einhorn.deadweight.core;

import java.io.IOException;

/** Framed byte pipe to the server. Implementations must be safe for one reader thread + concurrent senders. */
public interface Transport {
    void connect() throws IOException;
    /** Send one complete frame (as built by {@link Protocol}). */
    void send(byte[] frame) throws IOException;
    /** Block for the next frame; returns type byte + body (no length prefix), or null on clean EOF. */
    byte[] readFrame() throws IOException, ProtocolException;
    void close();
}
