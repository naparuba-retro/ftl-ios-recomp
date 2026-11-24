import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.pcode.*;
import ghidra.program.model.address.*;
import ghidra.program.model.symbol.*;
import java.util.*;

/** Compte les operations p-code (et les CALLOTHER par nom) utilisees dans toutes les fonctions. */
public class PcodeStats extends GhidraScript {
    @Override
    public void run() throws Exception {
        Map<String, Integer> ops = new TreeMap<>();
        Map<String, Integer> other = new TreeMap<>();
        Map<String, Integer> otherFns = new TreeMap<>();
        int insns = 0, nofn = 0, indirect = 0, indirectNoRef = 0;
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            Set<String> seen = new HashSet<>();
            InstructionIterator it = currentProgram.getListing().getInstructions(f.getBody(), true);
            while (it.hasNext()) {
                Instruction in = it.next();
                insns++;
                for (PcodeOp op : in.getPcode()) {
                    String m = op.getMnemonic();
                    ops.merge(m, 1, Integer::sum);
                    if (op.getOpcode() == PcodeOp.CALLOTHER) {
                        long idx = op.getInput(0).getOffset();
                        String nm = ((ghidra.app.plugin.processors.sleigh.SleighLanguage)
                                currentProgram.getLanguage()).getUserDefinedOpName((int) idx);
                        other.merge(nm, 1, Integer::sum);
                        if (seen.add(nm)) otherFns.merge(nm, 1, Integer::sum);
                    }
                    if (op.getOpcode() == PcodeOp.BRANCHIND || op.getOpcode() == PcodeOp.CALLIND) {
                        indirect++;
                        Reference[] refs = in.getReferencesFrom();
                        boolean has = false;
                        for (Reference r : refs) if (r.getReferenceType().isFlow()) has = true;
                        if (!has && op.getOpcode() == PcodeOp.BRANCHIND && in.getFlowType().isJump()) indirectNoRef++;
                    }
                }
            }
        }
        println("insns " + insns);
        println("ops " + ops);
        println("callother " + other);
        println("callother_fns " + otherFns);
        println("indirect " + indirect + " branchind_sans_ref " + indirectNoRef);
    }
}
