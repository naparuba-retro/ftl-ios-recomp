import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.pcode.*;
import java.util.*;

/** Affiche le p-code d'un exemple par mnemonique dont la sortie fait >8 octets (arguments : prefixes de mnemoniques). */
public class ShowPcode extends GhidraScript {
    @Override
    public void run() throws Exception {
        Set<String> want = new HashSet<>(Arrays.asList(getScriptArgs()));
        Set<String> done = new HashSet<>();
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            InstructionIterator it = currentProgram.getListing().getInstructions(f.getBody(), true);
            while (it.hasNext()) {
                Instruction in = it.next();
                String mn = in.getMnemonicString().toLowerCase();
                if (!want.contains(mn) || done.contains(mn)) continue;
                boolean big = false;
                for (PcodeOp op : in.getPcode()) if (op.getOutput() != null && op.getOutput().getSize() > 8) big = true;
                if (!big) continue;
                done.add(mn);
                println("### " + in.getAddress() + "  " + in);
                for (PcodeOp op : in.getPcode()) println("    " + op);
            }
        }
    }
}
