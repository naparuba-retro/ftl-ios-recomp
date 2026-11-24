import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;
import java.io.*;

/** Ecrit "adresse bloc nom" pour chaque pointeur de symbole (nl_symbol_ptr) : lit le nom des etiquettes Ghidra. */
public class DumpPtrs extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        try (PrintWriter t = new PrintWriter(new FileWriter(a[0]))) {
            for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
                if (!b.getName().contains("symbol_ptr")) continue;
                for (long off = b.getStart().getOffset(); off + 4 <= b.getStart().getOffset() + b.getSize(); off += 4) {
                    Address ad = toAddr(off);
                    StringBuilder names = new StringBuilder();
                    for (Symbol s : currentProgram.getSymbolTable().getSymbols(ad)) {
                        if (names.length() > 0) names.append(",");
                        names.append(s.getName(true));
                    }
                    String ref = "";
                    for (Reference r : currentProgram.getReferenceManager().getReferencesFrom(ad)) {
                        ref += " ->" + r.getToAddress() + (r.isExternalReference() ? "(ext)" : "");
                    }
                    t.println(String.format("%08x %s %s%s", off, b.getName(), names, ref));
                }
            }
        }
    }
}
