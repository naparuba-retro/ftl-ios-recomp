import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.pcode.*;
import java.util.*;

/** Pour chaque CALLOTHER distinct : un exemple d'instruction, tailles et valeurs des arguments. */
public class ShowCallother extends GhidraScript {
    @Override
    public void run() throws Exception {
        Map<String, String> ex = new TreeMap<>();
        Map<String, Set<String>> shapes = new TreeMap<>();
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            InstructionIterator it = currentProgram.getListing().getInstructions(f.getBody(), true);
            while (it.hasNext()) {
                Instruction in = it.next();
                for (PcodeOp op : in.getPcode()) {
                    if (op.getOpcode() != PcodeOp.CALLOTHER) continue;
                    String nm = currentProgram.getLanguage().getUserDefinedOpName((int) op.getInput(0).getOffset());
                    StringBuilder sb = new StringBuilder();
                    Varnode o = op.getOutput();
                    sb.append(o == null ? "(void)" : (o.isRegister() ? "reg" : o.isUnique() ? "uniq" : "?") + o.getSize());
                    sb.append(" <-");
                    for (int i = 1; i < op.getNumInputs(); i++) {
                        Varnode v = op.getInput(i);
                        sb.append(' ');
                        sb.append(v.isConstant() ? "c" + v.getOffset() + "/" + v.getSize() : (v.isRegister() ? "reg" : v.isUnique() ? "u" : "?") + v.getSize());
                    }
                    shapes.computeIfAbsent(nm, k -> new TreeSet<>()).add(sb.toString());
                    ex.putIfAbsent(nm, in.getAddress() + " " + in.toString());
                }
            }
        }
        for (var e : ex.entrySet()) {
            println("## " + e.getKey() + "  ex: " + e.getValue());
            int n = 0;
            for (String s : shapes.get(e.getKey())) { if (n++ < 6) println("     " + s); }
        }
    }
}
