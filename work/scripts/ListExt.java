import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.*;
import ghidra.program.model.listing.*;
import java.util.*;

public class ListExt extends GhidraScript {
    @Override
    public void run() throws Exception {
        Map<String, Map<String, Integer>> byLib = new TreeMap<>();
        SymbolTable st = currentProgram.getSymbolTable();
        ReferenceManager rm = currentProgram.getReferenceManager();
        for (Symbol s : st.getExternalSymbols()) {
            String lib = s.getParentNamespace().getName();
            int refs = 0;
            for (Reference r : s.getReferences()) refs++;
            byLib.computeIfAbsent(lib, k -> new TreeMap<>()).put(s.getName(), refs);
        }
        for (var e : byLib.entrySet()) {
            println("=== " + e.getKey() + " (" + e.getValue().size() + " symbols)");
            if (!e.getKey().contains("libSystem") && !e.getKey().contains("libstdc++")) {
                for (var s : e.getValue().entrySet()) println("   " + s.getKey() + " x" + s.getValue());
            }
        }
        int fn = currentProgram.getFunctionManager().getFunctionCount();
        println("TOTAL functions: " + fn);
    }
}
