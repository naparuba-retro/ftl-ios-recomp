import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import ghidra.program.model.symbol.*;
import java.util.*;

/** Statistiques sur les fonctions du programme (taille, mode Thumb, familles). */
public class CountFns extends GhidraScript {
    @Override
    public void run() throws Exception {
        int total = 0, thunk = 0, ext = 0, thumb = 0;
        long bytes = 0;
        Map<String, Integer> fam = new TreeMap<>();
        int[] buckets = new int[6]; // <32, <128, <512, <2048, <8192, >=8192
        var tm = currentProgram.getRegister("TMode");
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            total++;
            if (f.isThunk()) thunk++;
            long sz = f.getBody().getNumAddresses();
            bytes += sz;
            int b = sz < 32 ? 0 : sz < 128 ? 1 : sz < 512 ? 2 : sz < 2048 ? 3 : sz < 8192 ? 4 : 5;
            buckets[b]++;
            if (tm != null) {
                var v = currentProgram.getProgramContext().getRegisterValue(tm, f.getEntryPoint());
                if (v != null && v.getUnsignedValue().intValue() == 1) thumb++;
            }
            String ns = f.getParentNamespace().getName();
            String key;
            String n = f.getName();
            if (n.startsWith("_") && !ns.equals("Global") == false) key = "C(_)";
            else if (ns.equals("Global")) key = "global";
            else if (ns.equals("std") || ns.equals("__gnu_cxx") || ns.equals("boost") || ns.equals("rapidxml")) key = "stl/boost/xml";
            else key = "classes-jeu";
            fam.merge(key, 1, Integer::sum);
        }
        println("TOTAL " + total + " thunks " + thunk + " thumb " + thumb + " octets " + bytes);
        println("tailles <32:" + buckets[0] + " <128:" + buckets[1] + " <512:" + buckets[2]
                + " <2048:" + buckets[3] + " <8192:" + buckets[4] + " >=8192:" + buckets[5]);
        println("familles " + fam);
        println("langage " + currentProgram.getLanguageID());
        for (MemoryBlockHolder h : new MemoryBlockHolder[0]) {}
        for (var blk : currentProgram.getMemory().getBlocks())
            println("bloc " + blk.getName() + " " + blk.getStart() + " " + blk.getSize());
    }
    static class MemoryBlockHolder {}
}
