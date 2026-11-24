import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.util.*;

/** Decompile les fonctions dont le nom (symbole) est donne en argument de script. */
public class DecompFns extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] names = getScriptArgs();
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        for (String n : names) {
            List<Function> found = new ArrayList<>();
            for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
                if (f.getName().equals(n) || f.getName(true).equals(n)) found.add(f);
            }
            if (found.isEmpty()) {
                println("##### " + n + " : introuvable");
                continue;
            }
            for (Function f : found) {
                DecompileResults r = di.decompileFunction(f, 60, monitor);
                println("##### " + n + " @ " + f.getEntryPoint());
                if (r.decompileCompleted()) println(r.getDecompiledFunction().getC());
                else println("echec : " + r.getErrorMessage());
            }
        }
    }
}
