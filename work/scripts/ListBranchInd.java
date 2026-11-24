import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.pcode.*;
import ghidra.program.model.symbol.*;
import java.util.*;

/** Liste les sauts indirects sans cible (BRANCHIND non-retour) : mnemonique et 3 instructions precedentes. */
public class ListBranchInd extends GhidraScript {
    @Override
    public void run() throws Exception {
        Map<String, Integer> kinds = new TreeMap<>();
        int shown = 0;
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            InstructionIterator it = currentProgram.getListing().getInstructions(f.getBody(), true);
            while (it.hasNext()) {
                Instruction in = it.next();
                boolean bi = false;
                for (PcodeOp op : in.getPcode()) if (op.getOpcode() == PcodeOp.BRANCHIND) bi = true;
                if (!bi) continue;
                if (in.getFlowType().isTerminal() && !in.getFlowType().isJump()) continue;
                boolean has = false;
                for (Reference r : in.getReferencesFrom())
                    if (r.getReferenceType().isComputed() || r.getReferenceType().isJump()) has = true;
                if (has) continue;
                String mn = in.getMnemonicString();
                kinds.merge(mn, 1, Integer::sum);
                if (shown++ < 12) {
                    println("## " + f.getName() + " @ " + in.getAddress() + " : " + in);
                    Instruction p = in;
                    for (int i = 0; i < 4 && p.getPrevious() != null; i++) { p = p.getPrevious(); println("     " + p.getAddress() + " " + p); }
                }
            }
        }
        println("TYPES " + kinds);
    }
}
