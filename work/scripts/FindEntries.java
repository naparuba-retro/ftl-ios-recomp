import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import java.io.*;
import java.util.*;

/**
 * Cherche les pointeurs de code (mots de donnees) qui visent une instruction qui n'est pas
 * le debut d'une fonction Ghidra : points d'entree manques (callbacks, tables de pointeurs).
 * Ecrit la liste (hex) dans le fichier donne en argument.
 */
public class FindEntries extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        Listing lst = currentProgram.getListing();
        FunctionManager fm = currentProgram.getFunctionManager();
        Memory mem = currentProgram.getMemory();
        TreeSet<Long> found = new TreeSet<>();
        for (MemoryBlock b : mem.getBlocks()) {
            if (!b.isInitialized() || b.isOverlay()) continue;
            long start = b.getStart().getOffset(), end = start + b.getSize();
            for (long off = start; off + 4 <= end; off += 4) {
                Address ad = toAddr(off);
                if (lst.getInstructionAt(ad) != null) continue;      // c'est du code, pas une donnee
                long v = mem.getInt(ad) & 0xffffffffL;
                long t = v & ~1L;
                if (t < 0x4000 || t >= 0x23e4cc) continue;
                Address ta = toAddr(t);
                if (lst.getInstructionAt(ta) == null) continue;
                if (fm.getFunctionAt(ta) != null) continue;
                if (fm.getFunctionContaining(ta) == null) continue;
                found.add(t);
            }
        }
        try (PrintWriter w = new PrintWriter(new FileWriter(a[0]))) {
            for (long t : found) w.println(Long.toHexString(t));
        }
        println("ENTREES trouvees : " + found.size());
    }
}
