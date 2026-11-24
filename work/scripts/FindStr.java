import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.address.*;

/** Affiche les fonctions qui referencent les chaines de donnees donnees en argument. */
public class FindStr extends GhidraScript {
    @Override
    public void run() throws Exception {
        for (String s : getScriptArgs()) {
            DataIterator it = currentProgram.getListing().getDefinedData(true);
            while (it.hasNext()) {
                Data d = it.next();
                Object v = d.getValue();
                if (v instanceof String && ((String) v).equals(s)) {
                    println("STRING '" + s + "' @ " + d.getAddress());
                    for (Reference r : getReferencesTo(d.getAddress())) {
                        Function f = getFunctionContaining(r.getFromAddress());
                        println("   ref de " + r.getFromAddress() + " dans " + (f == null ? "?" : f.getName(true)));
                    }
                }
            }
        }
    }
}
