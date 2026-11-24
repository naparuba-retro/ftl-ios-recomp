import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;
import java.util.*;

/**
 * Tables de commutation ARM non reconnues par Ghidra : motif
 *     adr rT,table ; ... ; ldr r0,[r0,rT] ; add pc,r0,rT
 * (table de decalages relatifs a la table). On lit la table, on desassemble les cibles,
 * on ajoute des references de saut calcule et on recree la fonction pour recalculer son corps.
 */
public class FixSwitch extends GhidraScript {
    @Override
    public void run() throws Exception {
        Listing lst = currentProgram.getListing();
        Memory mem = currentProgram.getMemory();
        List<Function> todo = new ArrayList<>();
        Map<Function, List<Address[]>> work = new LinkedHashMap<>();
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            InstructionIterator it = lst.getInstructions(f.getBody(), true);
            while (it.hasNext()) {
                Instruction in = it.next();
                if (!in.getMnemonicString().equals("add")) continue;
                if (in.getNumOperands() < 1 || !in.getRegister(0).getName().equals("pc")) continue;
                boolean resolved = false;   // on ne touche que les sauts calcules sans aucune cible connue
                for (Reference rf : in.getReferencesFrom())
                    if (rf.getReferenceType().isComputed() || rf.getReferenceType().isJump()) resolved = true;
                if (resolved) continue;
                // chercher l'adr precedent
                Instruction p = in;
                Address table = null;
                int n = -1;
                for (int i = 0; i < 8 && p.getPrevious() != null; i++) {
                    p = p.getPrevious();
                    String mn = p.getMnemonicString();
                    if (mn.equals("adr") && table == null) {
                        for (Object o : p.getOpObjects(1)) if (o instanceof Address) table = (Address) o;
                        if (table == null) {
                            java.util.regex.Matcher m = java.util.regex.Pattern.compile("0x([0-9a-fA-F]+)")
                                .matcher(p.getDefaultOperandRepresentation(1));
                            if (m.find()) table = toAddr(Long.parseLong(m.group(1), 16));
                        }
                    }
                    if (mn.equals("cmp") && n < 0) {
                        for (Object o : p.getOpObjects(1)) if (o instanceof ghidra.program.model.scalar.Scalar)
                            n = (int) ((ghidra.program.model.scalar.Scalar) o).getValue() + 1;
                    }
                }
                if (table == null) { println("add pc sans adr @ " + in.getAddress()); continue; }
                if (n <= 0 || n > 256) n = 64;
                List<Address[]> pairs = new ArrayList<>();
                for (int k = 0; k < n; k++) {
                    long w = mem.getInt(table.add(4L * k)) & 0xffffffffL;
                    long t = table.getOffset() + w;
                    if (w > 0x10000 || (t & 3) != 0) break;
                    pairs.add(new Address[] {in.getAddress(), toAddr(t)});
                }
                println("Table @ " + table + " : " + pairs.size() + " cibles pour " + in.getAddress() + " dans " + f.getName());
                work.computeIfAbsent(f, x -> new ArrayList<>()).addAll(pairs);
            }
        }
        for (var e : work.entrySet()) {
            Function f = e.getKey();
            for (Address[] pr : e.getValue()) {
                currentProgram.getReferenceManager().addMemoryReference(pr[0], pr[1], RefType.COMPUTED_JUMP, SourceType.USER_DEFINED, 0);
                disassemble(pr[1]);
            }
            Address entry = f.getEntryPoint();
            String nm = f.getName();
            removeFunction(f);
            createFunction(entry, nm);
        }
        println("FixSwitch : " + work.size() + " fonction(s) corrigee(s)");
    }
}
