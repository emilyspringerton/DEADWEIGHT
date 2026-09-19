package industrial.einhorn.deadweight.core;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/** Client-side view of one draft (mutated only by {@link Session}): the current offer, the bucket counts left, the picks so far. */
public final class DraftModel {
    public static final class Pick {
        public final int card, mult;
        Pick(int card, int mult) { this.card = card; this.mult = mult; }
    }

    public int pickNo, total = Protocol.DRAFT_PICKS;
    public final int[] offer = {-1, -1};
    public final int[] left = new int[3];            // buckets remaining: [0] 1-ofs, [1] 2-ofs, [2] 3-ofs
    private final List<Pick> picks = new ArrayList<>();
    private volatile int pendingCard = -1, pendingMult;

    public List<Pick> picks() { return Collections.unmodifiableList(picks); }

    /** UI gate: a copy count is pickable only while its bucket still has room. */
    public boolean canPick(int mult) { return mult >= 1 && mult <= 3 && left[mult - 1] > 0; }

    public void notePick(int index, int mult) { pendingCard = offer[index]; pendingMult = mult; }

    public void onOffer(Msg m) {
        if (m.pickNo == 0) picks.clear();
        else if (m.pickNo == picks.size() + 1 && pendingCard >= 0) picks.add(new Pick(pendingCard, pendingMult)); // the server accepted our pick
        pendingCard = -1;
        pickNo = m.pickNo; total = m.pickTotal; offer[0] = m.offer[0]; offer[1] = m.offer[1];
        for (int i = 0; i < 3; i++) left[i] = m.left[i];
    }

    /** DRAFT_DONE arrives instead of a 17th offer: it confirms the final pick. */
    public void onDone() {
        if (pendingCard >= 0 && picks.size() < total) picks.add(new Pick(pendingCard, pendingMult));
        pendingCard = -1; pickNo = total;
    }
}
