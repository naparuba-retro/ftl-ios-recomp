import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.pcode.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

/**
 * Recompilation statique ARM -> C a partir du p-code de Ghidra.
 *
 * Arguments : <dossier_sortie> <racine1> [racine2 ...]
 *   racine = nom de fonction (ex. _decompress_to) ou adresse 0xXXXX.
 * Produit dans <dossier_sortie> :
 *   gen_funcs.c   : une fonction C f_<adresse>() par fonction ARM/Thumb atteinte
 *   gen_tab.c     : table adresse -> fonction (appels indirects)
 *   gen_ext.h     : declaration des fonctions externes (a fournir en C par le runtime)
 *   regs.h        : offsets des registres dans le tableau REGS
 *   mem_*.bin + blocks.txt : contenu initial de la memoire du programme
 *
 * Modele : memoire invitee = MEM + adresse 32 bits ; registres = tableau REGS
 * (espace "register" de Ghidra, octet par octet) ; temporaires p-code = variables
 * locales de l'instruction.
 */
public class Lift extends GhidraScript {
    PrintWriter out;
    Listing listing;
    FunctionManager fm;
    Deque<Function> queue = new ArrayDeque<>();
    Set<Long> seen = new HashSet<>();
    TreeMap<Long, Function> lifted = new TreeMap<>();
    TreeSet<String> externs = new TreeSet<>();
    Map<String, Integer> unsup = new TreeMap<>();
    Map<Integer, String> userOps = new HashMap<>();
    List<String> overridePrefixes = new ArrayList<>();
    Set<Long> probes = new HashSet<>();
    Set<String> overridden = new TreeSet<>();

    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        File dir = new File(a[0]);
        dir.mkdirs();
        listing = currentProgram.getListing();
        fm = currentProgram.getFunctionManager();
        for (int i = 1; i < a.length; i++) {
            if (a[i].startsWith("OVERRIDE:")) {
                overridePrefixes.add(a[i].substring(9));
                continue;
            }
            if (a[i].startsWith("PROBE:")) {
                probes.add(Long.parseLong(a[i].substring(6).replace("0x", ""), 16));
                continue;
            }
            if (a[i].equals("ALL")) {
                for (Function f : fm.getFunctions(true)) if (!f.isExternal()) enqueue(f);
                continue;
            }
            Function f = findFn(a[i]);
            if (f == null) { println("RACINE INTROUVABLE: " + a[i]); continue; }
            enqueue(f);
        }
        initFlags();
        TreeMap<Long, String> texts = new TreeMap<>();
        while (!queue.isEmpty()) {
            Function f = queue.poll();
            StringWriter sw = new StringWriter();
            out = new PrintWriter(sw);
            liftFunction(f);
            out.flush();
            texts.put(f.getEntryPoint().getOffset(), sw.toString());
            lifted.put(f.getEntryPoint().getOffset(), f);
        }
        // fichiers de ~120 fonctions pour que le compilateur reste raisonnable
        try (PrintWriter p = new PrintWriter(new FileWriter(new File(dir, "gen_protos.h")))) {
            p.println("#include \"rt.h\"");
            for (long ad : lifted.keySet()) p.println("void f_" + hex(ad) + "(void);");
        }
        int chunk = 0, inChunk = 0;
        PrintWriter cw = null;
        for (var e : texts.entrySet()) {
            if (cw == null || inChunk >= 120) {
                if (cw != null) cw.close();
                cw = new PrintWriter(new FileWriter(new File(dir, String.format("gen_funcs_%03d.c", chunk++))));
                cw.println("/* Genere par Lift.java : ne pas editer. */");
                cw.println("#include \"gen_protos.h\"");
                cw.println("#include \"gen_ext.h\"");
                inChunk = 0;
            }
            cw.print(e.getValue());
            inChunk++;
        }
        if (cw != null) cw.close();
        out = null;

        try (PrintWriter t = new PrintWriter(new FileWriter(new File(dir, "gen_tab.c")))) {
            t.println("#include \"rt.h\"");
            for (long ad : lifted.keySet()) t.println("void f_" + hex(ad) + "(void);");
            t.println("const FnEntry GEN_FNTAB[] = {");
            for (long ad : lifted.keySet()) t.println("  {0x" + Long.toHexString(ad) + ", f_" + hex(ad) + "},");
            t.println("};");
            t.println("const unsigned GEN_FNTAB_N = " + lifted.size() + ";");
        }
        try (PrintWriter t = new PrintWriter(new FileWriter(new File(dir, "gen_ext.h")))) {
            for (String e : externs) t.println("void " + e + "(void);");
        }
        try (PrintWriter t = new PrintWriter(new FileWriter(new File(dir, "regs.h")))) {
            for (String r : new String[] {"r0","r1","r2","r3","r4","r5","r6","r7","r8","r9","r10","r11","r12","sp","lr","pc","NG","ZR","CY","OV","TB"}) {
                var reg = currentProgram.getRegister(r);
                if (reg != null) t.println("#define REG_" + r + " 0x" + Integer.toHexString(reg.getOffset()));
            }
            for (int i = 0; i < 32; i++) {
                var reg = currentProgram.getRegister("d" + i);
                if (reg != null) t.println("#define REG_d" + i + " 0x" + Integer.toHexString(reg.getOffset()));
            }
            t.println("#define REGS_SIZE 0x4000");
        }
        try (PrintWriter t = new PrintWriter(new FileWriter(new File(dir, "blocks.txt")))) {
            for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
                if (!b.isInitialized() || b.isOverlay()) continue;
                String fn = "mem_" + Long.toHexString(b.getStart().getOffset()) + ".bin";
                byte[] buf = new byte[(int) b.getSize()];
                b.getBytes(b.getStart(), buf);
                try (FileOutputStream fo = new FileOutputStream(new File(dir, fn))) { fo.write(buf); }
                t.println(b.getName() + " " + Long.toHexString(b.getStart().getOffset()) + " " + b.getSize() + " " + fn);
            }
        }
        try (PrintWriter t = new PrintWriter(new FileWriter(new File(dir, "gen_names.c")))) {
            t.println("#include \"rt.h\"");
            t.println("const FnName GEN_NAMES[] = {");
            for (var e : lifted.entrySet()) {
                String nm = e.getValue().getName(true).replace("\\", "\\\\").replace("\"", "'");
                t.println("  {0x" + Long.toHexString(e.getKey()) + ", \"" + nm + "\"},");
            }
            t.println("};");
            t.println("const unsigned GEN_NAMES_N = " + lifted.size() + ";");
        }
        // gen_syms.h : noms de fonctions C-like -> f_<adresse>
        try (PrintWriter t = new PrintWriter(new FileWriter(new File(dir, "gen_syms.h")))) {
            Map<String, Integer> count = new HashMap<>();
            for (Function f : lifted.values()) count.merge(f.getName(), 1, Integer::sum);
            for (Function f : lifted.values()) {
                String nm = f.getName();
                if (count.get(nm) == 1 && nm.matches("^_[A-Za-z0-9_]+$"))
                    t.println("#define S" + nm + " f_" + hex(f.getEntryPoint().getOffset()));
            }
        }
        // dataptrs.txt : emplacements de pointeurs de donnees vers des symboles externes
        try (PrintWriter t = new PrintWriter(new FileWriter(new File(dir, "dataptrs.txt")))) {
            var rm = currentProgram.getReferenceManager();
            for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
                if (!b.getName().contains("symbol_ptr")) continue;
                for (long off = b.getStart().getOffset(); off + 4 <= b.getStart().getOffset() + b.getSize(); off += 4) {
                    for (Reference r : rm.getReferencesFrom(toAddr(off))) {
                        if (r instanceof ExternalReference) {
                            ExternalLocation el = ((ExternalReference) r).getExternalLocation();
                            String nm = el.getOriginalImportedName() != null ? el.getOriginalImportedName() : el.getLabel();
                            t.println(hex(off) + " " + b.getName() + " " + nm);
                        }
                    }
                }
            }
        }
        try (PrintWriter t = new PrintWriter(new FileWriter(new File(dir, "overridden.txt")))) {
            for (String n : overridden) t.println(n);
        }
        println("LIFT fonctions=" + lifted.size() + " externes=" + externs.size());
        println("LIFT ops p-code gardees=" + keptOps + " supprimees=" + removedOps);
        println("LIFT externes: " + externs);
        println("LIFT non supporte: " + unsup);
    }

    Function findFn(String s) {
        if (s.startsWith("0x")) {
            return fm.getFunctionAt(toAddr(Long.parseLong(s.substring(2), 16)));
        }
        for (Function f : fm.getFunctions(true))
            if (f.getName().equals(s) || f.getName(true).equals(s)) return f;
        return null;
    }

    void enqueue(Function f) {
        if (seen.add(f.getEntryPoint().getOffset())) queue.add(f);
    }

    static String hex(long v) { return String.format("%08x", v); }

    static String ctype(int sz) {
        switch (sz) {
            case 1: return "uint8_t";
            case 2: return "uint16_t";
            case 4: return "uint32_t";
            case 8: return "uint64_t";
            default: return null;
        }
    }

    static boolean scalar(int sz) { return sz == 1 || sz == 2 || sz == 4 || sz == 8; }

    String tname(Varnode v) { return "t" + Long.toHexString(v.getOffset()) + "_" + v.getSize(); }

    // temporaires uniques de l'instruction courante qui se chevauchent : un tampon d'octets commun
    Map<String, String> ucExpr = new HashMap<>();
    Map<String, String> ucPtr = new HashMap<>();

    static boolean uniqOverlap(PcodeOp[] ops) {
        List<long[]> r = new ArrayList<>();
        for (PcodeOp op : ops) {
            List<Varnode> vs = new ArrayList<>();
            if (op.getOutput() != null) vs.add(op.getOutput());
            for (int i = 0; i < op.getNumInputs(); i++) vs.add(op.getInput(i));
            for (Varnode v : vs) if (v.isUnique()) r.add(new long[] {v.getOffset(), v.getSize()});
        }
        for (int i = 0; i < r.size(); i++)
            for (int j = i + 1; j < r.size(); j++) {
                long[] a = r.get(i), b = r.get(j);
                boolean same = a[0] == b[0] && a[1] == b[1];
                boolean ov = a[0] < b[0] + b[1] && b[0] < a[0] + a[1];
                if (ov && !same) return true;
            }
        return false;
    }

    String space(Varnode v) { return v.getAddress().getAddressSpace().getName(); }

    static String cconst(Varnode v) {
        int n = v.getSize();
        long m = n >= 8 ? -1L : ((1L << (8 * n)) - 1);
        long c = v.getOffset() & m;
        return n == 8 ? "0x" + Long.toHexString(c) + "ULL" : "0x" + Long.toHexString(c) + "u";
    }

    /** Valeur non signee (uintN) d'un varnode scalaire. */
    String rd(Varnode v) {
        int n = v.getSize();
        String t = ctype(n);
        if (t == null) return "/*taille " + n + "*/0";
        if (v.isConstant()) return cconst(v);
        if (v.isUnique()) return ucExpr.getOrDefault(tname(v), tname(v));
        if (v.isRegister()) return "G" + n + "(0x" + Long.toHexString(v.getOffset()) + ")";
        if (v.getAddress().isMemoryAddress())
            return "MG" + n + "(0x" + Long.toHexString(v.getOffset()) + ")";
        return "/*varnode inconnu*/0";
    }

    String lv(Varnode v) { return rd(v); }

    /** Adresse (void*) d'un varnode non constant. */
    String ptr(Varnode v) {
        if (v.isUnique()) {
            String q = ucPtr.get(tname(v));
            if (q != null) return q;
            return scalar(v.getSize()) ? "(&" + tname(v) + ")" : tname(v);
        }
        if (v.isRegister()) return "(REGS+0x" + Long.toHexString(v.getOffset()) + ")";
        if (v.getAddress().isMemoryAddress()) return "(MEM+0x" + Long.toHexString(v.getOffset()) + ")";
        return "0";
    }

    String u64(Varnode v) {
        if (v.isConstant() && scalar(v.getSize())) {
            int n = v.getSize();
            long m = n >= 8 ? -1L : ((1L << (8 * n)) - 1);
            return "0x" + Long.toHexString(v.getOffset() & m) + "ULL";
        }
        return "((uint64_t)" + rd(v) + ")";
    }

    String sx(Varnode v) { return "SX(" + u64(v) + "," + (8 * v.getSize()) + ")"; }

    String curOp = "", curMn = "";

    void assign(Varnode o, String expr) {
        String t = ctype(o.getSize());
        if (t == null) {
            unsup("sortie_taille_" + o.getSize() + "_" + curOp + "_" + curMn);
            out.println("    UNSUP(\"sortie taille " + o.getSize() + " " + curOp + " " + curMn + "\");");
            return;
        }
        out.println("    " + lv(o) + " = (" + t + ")(" + expr + ");");
    }

    void unsup(String s) { unsup.merge(s, 1, Integer::sum); }

    void declareTemps(PcodeOp[] ops, boolean[] keep) {
        ucExpr.clear();
        ucPtr.clear();
        Map<String, Varnode> seenv = new LinkedHashMap<>();
        for (int oi = 0; oi < ops.length; oi++) {
            if (!keep[oi]) continue;
            PcodeOp op = ops[oi];
            if (op.getOutput() != null && op.getOutput().isUnique()) seenv.put(tname(op.getOutput()), op.getOutput());
            for (int i = 0; i < op.getNumInputs(); i++) {
                Varnode v = op.getInput(i);
                if (v.isUnique()) seenv.put(tname(v), v);
            }
        }
        // regroupement par intervalles qui se chevauchent
        List<Varnode> list = new ArrayList<>(seenv.values());
        list.sort((x, y) -> Long.compare(x.getOffset(), y.getOffset()));
        List<List<Varnode>> clusters = new ArrayList<>();
        long curEnd = -1;
        for (Varnode v : list) {
            if (clusters.isEmpty() || v.getOffset() >= curEnd) {
                clusters.add(new ArrayList<>());
                curEnd = v.getOffset() + v.getSize();
            } else {
                curEnd = Math.max(curEnd, v.getOffset() + v.getSize());
            }
            clusters.get(clusters.size() - 1).add(v);
        }
        for (List<Varnode> cl : clusters) {
            if (cl.size() == 1) {
                Varnode v = cl.get(0);
                if (scalar(v.getSize())) out.println("    " + ctype(v.getSize()) + " " + tname(v) + " = 0;");
                else out.println("    uint8_t " + tname(v) + "[" + v.getSize() + "] = {0};");
                continue;
            }
            long start = cl.get(0).getOffset(), end = start;
            for (Varnode v : cl) end = Math.max(end, v.getOffset() + v.getSize());
            String buf = "uc" + Long.toHexString(start);
            out.println("    uint8_t " + buf + "[" + (end - start) + "] = {0};");
            for (Varnode v : cl) {
                String at = "(" + buf + "+" + (v.getOffset() - start) + ")";
                ucPtr.put(tname(v), at);
                if (scalar(v.getSize())) ucExpr.put(tname(v), "(*(" + ctype(v.getSize()) + "*)" + at + ")");
            }
        }
    }

    String callTarget(Address ta) {
        Function g = fm.getFunctionAt(ta);
        if (g == null) {
            // adresse sans fonction : essayer de creer une cible externe via les symboles
            Symbol s = currentProgram.getSymbolTable().getPrimarySymbol(ta);
            if (s != null && ta.isExternalAddress()) {
                String nm = "ext_" + sanitize(s.getName());
                externs.add(nm);
                return nm;
            }
            unsup("appel_sans_fonction");
            return "rt_missing_0x" + Long.toHexString(ta.getOffset());
        }
        // un remplacement explicite (OVERRIDE:) a priorite sur le suivi des thunks
        for (String pre : overridePrefixes) {
            if (g.getName().startsWith(pre)) {
                String nm = "ext_" + sanitize(g.getName());
                externs.add(nm);
                overridden.add(g.getName());
                return nm;
            }
        }
        for (int guard = 0; guard < 4 && g.isThunk(); guard++) {
            Function t = g.getThunkedFunction(false);
            if (t == null) break;
            g = t;
            for (String pre : overridePrefixes) {
                if (g.getName().startsWith(pre)) {
                    String nm = "ext_" + sanitize(g.getName());
                    externs.add(nm);
                    overridden.add(g.getName());
                    return nm;
                }
            }
        }
        if (g.isExternal()) {
            String nm = "ext_" + sanitize(extName(g));
            externs.add(nm);
            return nm;
        }
        enqueue(g);
        return "f_" + hex(g.getEntryPoint().getOffset());
    }

    /** Nom d'origine (mange) d'une fonction externe : evite les collisions de noms demanteles. */
    static String extName(Function g) {
        ExternalLocation el = g.getExternalLocation();
        if (el != null) {
            if (el.getOriginalImportedName() != null) return el.getOriginalImportedName();
            if (el.getLabel() != null) return el.getLabel();
        }
        return g.getName();
    }

    static String sanitize(String s) {
        StringBuilder b = new StringBuilder();
        for (char c : s.toCharArray()) b.append(Character.isLetterOrDigit(c) ? c : '_');
        return b.toString();
    }

    void liftFunction(Function f) throws Exception {
        long entry = f.getEntryPoint().getOffset();
        out.println();
        out.println("/* " + f.getName(true) + " @ " + f.getEntryPoint() + " */");
        out.println("void f_" + hex(entry) + "(void)");
        out.println("{");
        for (String pre : overridePrefixes) {
            if (f.getName().startsWith(pre)) {
                String nm = "ext_" + sanitize(f.getName());
                externs.add(nm);
                overridden.add(f.getName());
                out.println("  " + nm + "();");
                out.println("}");
                return;
            }
        }
        if (f.isThunk()) {
            Function t = f;
            for (int guard = 0; guard < 4 && t.isThunk(); guard++) {
                Function n = t.getThunkedFunction(false);
                if (n == null) break;
                t = n;
            }
            if (t.isExternal()) {
                String nm = "ext_" + sanitize(extName(t));
                externs.add(nm);
                out.println("  " + nm + "();");
                out.println("}");
                return;
            }
        }
        out.println("  RT_ENTER(0x" + Long.toHexString(entry) + "u);");
        AddressSetView body = f.getBody();
        List<Instruction> insns = new ArrayList<>();
        InstructionIterator it = listing.getInstructions(body, true);
        while (it.hasNext()) insns.add(it.next());
        int[] liveOut = flagLiveness(f, insns);
        for (int ii = 0; ii < insns.size(); ii++) {
            Instruction in = insns.get(ii);
            long ia = in.getAddress().getOffset();
            PcodeOp[] ops = in.getPcode();
            boolean[] keep = dce(ops, liveOut[ii]);
            out.println("  L_" + hex(ia) + ":; /* " + in.toString().replace("*/", "* /") + " */");
            if (probes.contains(entry)) out.println("  RT_PROBE(0x" + Long.toHexString(ia) + "u);");
            out.println("  {");
            if (emitVmovF32(in, ops) || emitVmovLane32(in) || emitExclusive(in)) { out.println("  }"); continue; }
            declareTemps(ops, keep);
            // cibles relatives
            Set<Integer> targets = new TreeSet<>();
            for (int i = 0; i < ops.length; i++) {
                PcodeOp op = ops[i];
                int oc = op.getOpcode();
                if ((oc == PcodeOp.BRANCH || oc == PcodeOp.CBRANCH) && op.getInput(0).isConstant())
                    targets.add(i + (int) op.getInput(0).getOffset());
            }
            for (int i = 0; i <= ops.length; i++) {
                if (targets.contains(i)) out.println("   P_" + hex(ia) + "_" + i + ":;");
                if (i == ops.length) break;
                if (!keep[i]) { removedOps++; continue; }
                keptOps++;
                emitOp(f, in, ops[i], i);
            }
            out.println("  }");
        }
        out.println("  return;");
        out.println("}");
    }

    // ---------------------------------------------------------------
    // Elimination des drapeaux morts (NG, ZR, CY, OV, shift_carry, Q, GE*).
    // Selon l'ABI ARM, les drapeaux ne vivent pas au-dela d'un appel ou d'un retour :
    // on calcule, par fonction, quels drapeaux sont lus avant d'etre reecrits.
    // ---------------------------------------------------------------
    Map<Long, Integer> flagBit = new HashMap<>();
    int removedOps = 0, keptOps = 0;

    void initFlags() {
        String[] names = {"NG", "ZR", "CY", "OV", "tmpNG", "tmpZR", "tmpCY", "tmpOV", "shift_carry", "Q", "GE1", "GE2", "GE3", "GE4"};
        int bit = 0;
        for (String n : names) {
            var r = currentProgram.getRegister(n);
            if (r != null && r.getMinimumByteSize() == 1) flagBit.put((long) r.getOffset(), 1 << bit++);
        }
        println("LIFT drapeaux elimines: " + flagBit.size() + " registres");
    }

    int fbit(Varnode v) {
        if (v == null || !v.isRegister()) return 0;
        int m = 0;
        for (var e : flagBit.entrySet()) {
            long off = e.getKey();
            if (off >= v.getOffset() && off < v.getOffset() + v.getSize()) m |= e.getValue();
        }
        return m;
    }

    boolean exactFlag(Varnode v) {
        return v != null && v.isRegister() && v.getSize() == 1 && flagBit.containsKey(v.getOffset());
    }

    static boolean branchy(PcodeOp[] ops) {
        for (PcodeOp op : ops) {
            int oc = op.getOpcode();
            if (oc == PcodeOp.CBRANCH || (oc == PcodeOp.BRANCH && op.getInput(0).isConstant())) return true;  // CBRANCH vers une adresse = instruction conditionnelle (cmpeq...) : ses ecritures ne tuent pas la vivacite
        }
        return false;
    }

    int[] flagLiveness(Function f, List<Instruction> insns) {
        int n = insns.size();
        Map<Long, Integer> idxOf = new HashMap<>();
        for (int i = 0; i < n; i++) idxOf.put(insns.get(i).getAddress().getOffset(), i);
        int[] use = new int[n], kill = new int[n];
        int[][] succ = new int[n][];
        for (int i = 0; i < n; i++) {
            Instruction in = insns.get(i);
            PcodeOp[] ops = in.getPcode();
            boolean br = branchy(ops);
            int written = 0;
            for (PcodeOp op : ops) {
                for (int k = 0; k < op.getNumInputs(); k++) {
                    int m = fbit(op.getInput(k));
                    if (m != 0) use[i] |= br ? m : (m & ~written);
                }
                Varnode o = op.getOutput();
                if (exactFlag(o)) {
                    int b = flagBit.get(o.getOffset());
                    written |= b;
                    if (!br) kill[i] |= b;
                }
            }
            Set<Integer> ss = new TreeSet<>();
            Address ft = in.getFallThrough();
            if (ft != null && idxOf.containsKey(ft.getOffset())) ss.add(idxOf.get(ft.getOffset()));
            for (Address fa : in.getFlows())
                if (idxOf.containsKey(fa.getOffset())) ss.add(idxOf.get(fa.getOffset()));
            for (Reference r : in.getReferencesFrom()) {
                if ((r.getReferenceType().isComputed() || r.getReferenceType().isJump())
                        && idxOf.containsKey(r.getToAddress().getOffset()))
                    ss.add(idxOf.get(r.getToAddress().getOffset()));
            }
            succ[i] = ss.stream().mapToInt(Integer::intValue).toArray();
        }
        int[] liveIn = new int[n], liveOut = new int[n];
        boolean ch = true;
        while (ch) {
            ch = false;
            for (int i = n - 1; i >= 0; i--) {
                int lo = 0;
                for (int s : succ[i]) lo |= liveIn[s];
                int li = use[i] | (lo & ~kill[i]);
                if (lo != liveOut[i] || li != liveIn[i]) { liveOut[i] = lo; liveIn[i] = li; ch = true; }
            }
        }
        if (System.getenv("FTL_LIVEFN") != null && Long.toHexString(f.getEntryPoint().getOffset()).equals(System.getenv("FTL_LIVEFN"))) {
            for (int i = 0; i < n; i++) println("LIVEDBG " + insns.get(i) + " use=" + Integer.toHexString(use[i]) + " kill=" + Integer.toHexString(kill[i]) + " liveIn=" + Integer.toHexString(liveIn[i]) + " liveOut=" + Integer.toHexString(liveOut[i]) + " succ=" + java.util.Arrays.toString(succ[i]));
        }
        return liveOut;
    }

    /** Marque les operations p-code inutiles d'une instruction (sorties mortes). */
    boolean[] dce(PcodeOp[] ops, int flagsLiveOut) {
        boolean[] keep = new boolean[ops.length];
        Arrays.fill(keep, true);
        if (uniqOverlap(ops)) return keep;
        boolean br = branchy(ops);
        Set<String> liveT = new HashSet<>();
        int liveF = flagsLiveOut;
        for (int k = ops.length - 1; k >= 0; k--) {
            PcodeOp op = ops[k];
            Varnode o = op.getOutput();
            boolean tracked = false, dead = false;
            int bit = 0;
            if (o != null) {
                if (o.isUnique()) { tracked = true; dead = !liveT.contains(tname(o)); }
                else if (exactFlag(o)) { tracked = true; bit = flagBit.get(o.getOffset()); dead = (liveF & bit) == 0; }
            }
            boolean pure = o != null && (op.getOpcode() != PcodeOp.CALLOTHER
                || currentProgram.getLanguage().getUserDefinedOpName((int) op.getInput(0).getOffset()).equals("hasExclusiveAccess"));
            if (tracked && dead && pure) { keep[k] = false; continue; }
            if (tracked && !br) {
                if (o.isUnique()) liveT.remove(tname(o)); else liveF &= ~bit;
            }
            for (int i = 0; i < op.getNumInputs(); i++) {
                Varnode v = op.getInput(i);
                if (v.isUnique()) liveT.add(tname(v));
                liveF |= fbit(v);
            }
        }
        return keep;
    }

    /**
     * vmov.f32 Dd/Qd,#imm (NEON, cmode=1111) : Ghidra n'expanse pas correctement l'immediat
     * (ex. imm8=0 -> 2.0 devient 0). On decode imm8 depuis les octets de l'instruction.
     */
    boolean emitVmovF32(Instruction in, PcodeOp[] ops) {
        if (!in.getMnemonicString().toLowerCase().equals("vmov.f32")) return false;
        if (!in.toString().contains("#") || in.getLength() != 4) return false;
        Varnode dst = null;
        for (PcodeOp op : ops) {
            Varnode o = op.getOutput();
            if (o != null && o.isRegister() && (o.getSize() == 8 || o.getSize() == 16)) dst = o;
        }
        if (dst == null) return false;
        try {
            byte[] b = in.getBytes();
            long w = (b[0] & 0xffL) | ((b[1] & 0xffL) << 8) | ((b[2] & 0xffL) << 16) | ((b[3] & 0xffL) << 24);
            var tm = currentProgram.getRegister("TMode");
            var rv = currentProgram.getProgramContext().getRegisterValue(tm, in.getAddress());
            boolean thumb = rv != null && rv.getUnsignedValue().intValue() == 1;
            long imm8, cmode;
            if (thumb) {
                long hw1 = w & 0xffff, hw2 = (w >> 16) & 0xffff;
                imm8 = (((hw1 >> 12) & 1) << 7) | ((hw1 & 7) << 4) | (hw2 & 0xf);
                cmode = (hw2 >> 8) & 0xf;
            } else {
                imm8 = (((w >> 24) & 1) << 7) | (((w >> 16) & 7) << 4) | (w & 0xf);
                cmode = (w >> 8) & 0xf;
            }
            if (cmode != 0xf) return false;
            long sign = (imm8 >> 7) & 1, b6 = (imm8 >> 6) & 1, b54 = (imm8 >> 4) & 3, frac = imm8 & 0xf;
            long bits = (sign << 31) | ((b6 == 0 ? 1L : 0L) << 30) | ((b6 == 1 ? 0x1fL : 0L) << 25) | (b54 << 23) | (frac << 19);
            int lanes = dst.getSize() / 4;
            out.println("    { uint32_t c_ = 0x" + Long.toHexString(bits) + "u; for (int i_ = 0; i_ < " + lanes
                + "; i_++) memcpy(REGS + 0x" + Long.toHexString(dst.getOffset()) + " + 4 * i_, &c_, 4); }");
            return true;
        } catch (Exception e) {
            return false;
        }
    }

    /**
     * vmov.32 Dn[i],Rt : le p-code de Ghidra masque seulement 16 bits (0xffff) de la voie visee ;
     * l'insertion corrompt la voie si ses 16 bits hauts n'etaient pas nuls. Ecriture directe de 4 octets.
     */
    boolean emitVmovLane32(Instruction in) {
        if (!in.getMnemonicString().toLowerCase().equals("vmov.32")) return false;
        java.util.regex.Matcher m = java.util.regex.Pattern
            .compile("^vmov\\.32 (d\\d+)\\[0x([0-9a-fA-F]+)\\],(\\w+)$").matcher(in.toString().trim());
        if (!m.find()) return false;
        var d = currentProgram.getRegister(m.group(1));
        var r = currentProgram.getRegister(m.group(3));
        if (d == null || r == null || r.getMinimumByteSize() != 4) return false;
        long off = d.getOffset() + 4L * Long.parseLong(m.group(2), 16);
        out.println("    memcpy(REGS + 0x" + Long.toHexString(off) + ", REGS + 0x" + Long.toHexString(r.getOffset()) + ", 4);");
        return true;
    }

    /**
     * ldrex / strex : moniteur exclusif reel (compare-and-swap) au lieu de "toujours reussi",
     * indispensable pour les compteurs de references partages entre threads (std::string COW, boost).
     */
    boolean emitExclusive(Instruction in) {
        String t = in.toString().trim();
        java.util.regex.Matcher m = java.util.regex.Pattern.compile("^ldrex (\\w+),\\[(\\w+)\\]$").matcher(t);
        if (m.find()) {
            var rt = currentProgram.getRegister(m.group(1)); var rn = currentProgram.getRegister(m.group(2));
            if (rt == null || rn == null) return false;
            out.println("    G4(0x" + Long.toHexString(rt.getOffset()) + ") = rt_ldrex(G4(0x" + Long.toHexString(rn.getOffset()) + "));");
            return true;
        }
        m = java.util.regex.Pattern.compile("^strex (\\w+),(\\w+),\\[(\\w+)\\]$").matcher(t);
        if (m.find()) {
            var rd = currentProgram.getRegister(m.group(1)); var rt = currentProgram.getRegister(m.group(2)); var rn = currentProgram.getRegister(m.group(3));
            if (rd == null || rt == null || rn == null) return false;
            out.println("    G4(0x" + Long.toHexString(rd.getOffset()) + ") = rt_strex(G4(0x" + Long.toHexString(rn.getOffset()) + "), G4(0x" + Long.toHexString(rt.getOffset()) + "));");
            return true;
        }
        return false;
    }

    String branchTo(Function f, Instruction in, PcodeOp op, int idx, Varnode dest) {
        if (dest.isConstant()) {
            int ti = idx + (int) dest.getOffset();
            return "goto P_" + hex(in.getAddress().getOffset()) + "_" + ti + ";";
        }
        Address ta = dest.getAddress();
        if (f.getBody().contains(ta) && listing.getInstructionAt(ta) != null)
            return "goto L_" + hex(ta.getOffset()) + ";";
        return callTarget(ta) + "(); return;";
    }

    void emitOp(Function f, Instruction in, PcodeOp op, int idx) {
        int oc = op.getOpcode();
        Varnode o = op.getOutput();
        Varnode a = op.getNumInputs() > 0 ? op.getInput(0) : null;
        Varnode b = op.getNumInputs() > 1 ? op.getInput(1) : null;
        curOp = op.getMnemonic();
        curMn = in.getMnemonicString().toLowerCase();
        switch (oc) {
            case PcodeOp.COPY:
                if (scalar(o.getSize()) && scalar(a.getSize())) assign(o, rd(a));
                else if (a.isConstant()) {
                    // constante > 8 octets (vmov.i32 q,#imm) : seuls les 64 bits bas sont connus
                    if (a.getOffset() != 0) unsup("copy_const_grande_non_nulle");
                    out.println("    { uint64_t c_ = 0x" + Long.toHexString(a.getOffset()) + "ULL; memset(" + ptr(o) + ", 0, "
                        + o.getSize() + "); memcpy(" + ptr(o) + ", &c_, 8); }");
                }
                else out.println("    memcpy(" + ptr(o) + ", " + ptr(a) + ", " + o.getSize() + ");");
                break;
            case PcodeOp.LOAD: {
                if (currentProgram.getAddressFactory().getAddressSpace((int) a.getOffset()).isConstantSpace()) {
                    // idiome SLEIGH "export *[const]:N tmp" : la valeur de tmp, sans acces memoire
                    if (scalar(o.getSize()) && scalar(b.getSize())) assign(o, rd(b));
                    else out.println("    memcpy(" + ptr(o) + ", " + ptr(b) + ", " + o.getSize() + ");");
                    break;
                }
                String t = ctype(o.getSize());
                String ad = "(uint32_t)" + rd(b);
                String bs = currentProgram.getAddressFactory().getAddressSpace((int) a.getOffset()).isRegisterSpace() ? "REGS" : "MEM";
                if (t != null) out.println("    " + lv(o) + " = *(" + t + "*)(" + bs + " + " + ad + ");");
                else out.println("    memcpy(" + ptr(o) + ", " + bs + " + " + ad + ", " + o.getSize() + ");");
                break;
            }
            case PcodeOp.STORE: {
                String t = ctype(b.getSize() == 0 ? 4 : op.getInput(2).getSize());
                Varnode val = op.getInput(2);
                String ad = "(uint32_t)" + rd(op.getInput(1));
                String bs = currentProgram.getAddressFactory().getAddressSpace((int) a.getOffset()).isRegisterSpace() ? "REGS" : "MEM";
                if (ctype(val.getSize()) != null) out.println("    *(" + ctype(val.getSize()) + "*)(" + bs + " + " + ad + ") = " + rd(val) + ";");
                else if (val.isConstant()) { unsup("store_const_grande"); out.println("    UNSUP(\"store const grande\");"); }
                else out.println("    memcpy(" + bs + " + " + ad + ", " + ptr(val) + ", " + val.getSize() + ");");
                break;
            }
            case PcodeOp.INT_ADD: assign(o, u64(a) + " + " + u64(b)); break;
            case PcodeOp.INT_SUB: assign(o, u64(a) + " - " + u64(b)); break;
            case PcodeOp.INT_MULT: assign(o, u64(a) + " * " + u64(b)); break;
            case PcodeOp.INT_AND:
                if (o.getSize() > 8) bigop("and", o, a, b); else assign(o, u64(a) + " & " + u64(b)); break;
            case PcodeOp.INT_OR:
                if (o.getSize() > 8) bigop("or", o, a, b); else assign(o, u64(a) + " | " + u64(b)); break;
            case PcodeOp.INT_XOR:
                if (o.getSize() > 8) bigop("xor", o, a, b); else assign(o, u64(a) + " ^ " + u64(b)); break;
            case PcodeOp.INT_NEGATE:
                if (o.getSize() > 8) bigop("not", o, a, null); else assign(o, "~" + u64(a)); break;
            case PcodeOp.INT_2COMP: assign(o, "-" + u64(a)); break;
            case PcodeOp.INT_LEFT:
                if (o.getSize() > 8) bigshift("shl", o, a, b); else assign(o, "SHL64(" + u64(a) + "," + u64(b) + ")"); break;
            case PcodeOp.INT_RIGHT:
                if (o.getSize() > 8) bigshift("shr", o, a, b); else assign(o, "SHR64(" + u64(a) + "," + u64(b) + ")"); break;
            case PcodeOp.INT_SRIGHT: assign(o, "SAR64(" + sx(a) + "," + u64(b) + ")"); break;
            case PcodeOp.INT_EQUAL: assign(o, u64(a) + " == " + u64(b)); break;
            case PcodeOp.INT_NOTEQUAL: assign(o, u64(a) + " != " + u64(b)); break;
            case PcodeOp.INT_LESS: assign(o, u64(a) + " < " + u64(b)); break;
            case PcodeOp.INT_LESSEQUAL: assign(o, u64(a) + " <= " + u64(b)); break;
            case PcodeOp.INT_SLESS: assign(o, "(int64_t)" + sx(a) + " < (int64_t)" + sx(b)); break;
            case PcodeOp.INT_SLESSEQUAL: assign(o, "(int64_t)" + sx(a) + " <= (int64_t)" + sx(b)); break;
            case PcodeOp.INT_CARRY: assign(o, "CARRY(" + u64(a) + "," + u64(b) + "," + (8 * a.getSize()) + ")"); break;
            case PcodeOp.INT_SCARRY: assign(o, "SCARRY(" + u64(a) + "," + u64(b) + "," + (8 * a.getSize()) + ")"); break;
            case PcodeOp.INT_SBORROW: assign(o, "SBORROW(" + u64(a) + "," + u64(b) + "," + (8 * a.getSize()) + ")"); break;
            case PcodeOp.INT_ZEXT:
                if (o.getSize() > 8) {
                    out.println("    {");
                    String src = bigptr(a, Math.max(a.getSize(), 8), 1);
                    out.println("    uint8_t z_[32] = {0}; memcpy(z_, " + src + ", " + Math.min(a.getSize(), 32) + "); memcpy(" + ptr(o) + ", z_, " + o.getSize() + ");");
                    out.println("    }");
                } else assign(o, u64(a));
                break;
            case PcodeOp.INT_SEXT: assign(o, sx(a)); break;
            case PcodeOp.BOOL_AND: assign(o, rd(a) + " & " + rd(b)); break;
            case PcodeOp.BOOL_OR: assign(o, rd(a) + " | " + rd(b)); break;
            case PcodeOp.BOOL_XOR: assign(o, rd(a) + " ^ " + rd(b)); break;
            case PcodeOp.BOOL_NEGATE: assign(o, "!" + rd(a)); break;
            case PcodeOp.SUBPIECE: {
                int k = (int) b.getOffset();
                if (scalar(o.getSize()) && scalar(a.getSize()) && !a.isConstant()) assign(o, u64(a) + " >> " + (8 * k));
                else if (a.isConstant()) assign(o, "(" + u64(a) + " >> " + (8 * k) + ")");
                else out.println("    memcpy(" + ptr(o) + ", (uint8_t*)" + ptr(a) + " + " + k + ", " + o.getSize() + ");");
                break;
            }
            case PcodeOp.FLOAT_ADD: fl(o, a, b, "+"); break;
            case PcodeOp.FLOAT_SUB: fl(o, a, b, "-"); break;
            case PcodeOp.FLOAT_MULT: fl(o, a, b, "*"); break;
            case PcodeOp.FLOAT_DIV: fl(o, a, b, "/"); break;
            case PcodeOp.FLOAT_EQUAL: flc(o, a, b, "=="); break;
            case PcodeOp.FLOAT_LESS: flc(o, a, b, "<"); break;
            case PcodeOp.FLOAT_LESSEQUAL: flc(o, a, b, "<="); break;
            case PcodeOp.FLOAT_NAN:
                assign(o, "isnan(" + (a.getSize() == 4 ? "F32(" : "F64(") + rd(a) + "))");
                break;
            case PcodeOp.FLOAT_ABS:
                if (a.getSize() == 4) assign(o, "U32F(fabsf(F32(" + rd(a) + ")))"); else assign(o, "U64F(fabs(F64(" + rd(a) + ")))");
                break;
            case PcodeOp.FLOAT_SQRT:
                if (a.getSize() == 4) assign(o, "U32F(sqrtf(F32(" + rd(a) + ")))"); else assign(o, "U64F(sqrt(F64(" + rd(a) + ")))");
                break;
            case PcodeOp.FLOAT_FLOAT2FLOAT:
                if (a.getSize() == 4 && o.getSize() == 8) assign(o, "U64F((double)F32(" + rd(a) + "))");
                else if (a.getSize() == 8 && o.getSize() == 4) assign(o, "U32F((float)F64(" + rd(a) + "))");
                else assign(o, rd(a));
                break;
            case PcodeOp.FLOAT_TRUNC:
                assign(o, "(int64_t)" + (a.getSize() == 4 ? "F32(" : "F64(") + rd(a) + ")");
                break;
            case PcodeOp.FLOAT_INT2FLOAT:
                if (o.getSize() == 4) assign(o, "U32F((float)(int64_t)" + sx(a) + ")"); else assign(o, "U64F((double)(int64_t)" + sx(a) + ")");
                break;
            case PcodeOp.CBRANCH: {
                String cond = rd(b);
                out.println("    if (" + cond + ") { " + branchTo(f, in, op, idx, a) + " }");
                break;
            }
            case PcodeOp.BRANCH:
                out.println("    " + branchTo(f, in, op, idx, a));
                break;
            case PcodeOp.CALL:
                out.println("    " + callTarget(a.getAddress()) + "();");
                break;
            case PcodeOp.CALLIND:
                out.println("    rt_call_ind(" + rd(a) + ");");
                break;
            case PcodeOp.RETURN:
                out.println("    return;");
                break;
            case PcodeOp.BRANCHIND: {
                if (in.getFlowType().isTerminal() && !in.getFlowType().isJump()) {
                    out.println("    return;");
                    break;
                }
                List<Address> targets = new ArrayList<>();
                for (Reference r : in.getReferencesFrom()) {
                    if (r.getReferenceType().isComputed() || r.getReferenceType().isJump()) {
                        Address ta = r.getToAddress();
                        if (f.getBody().contains(ta) && listing.getInstructionAt(ta) != null) targets.add(ta);
                    }
                }
                String sel = "(uint32_t)" + rd(a);
                if (!targets.isEmpty()) {
                    out.println("    switch (" + sel + " & ~1u) {");
                    Set<Long> done = new HashSet<>();
                    for (Address ta : targets)
                        if (done.add(ta.getOffset()))
                            out.println("      case 0x" + Long.toHexString(ta.getOffset()) + ": goto L_" + hex(ta.getOffset()) + ";");
                    out.println("      default: rt_tail_ind(" + sel + "); return;");
                    out.println("    }");
                } else {
                    unsup("branchind_sans_cible");
                    out.println("    rt_tail_ind(" + sel + "); return;");
                }
                break;
            }
            case PcodeOp.CALLOTHER: {
                int idx2 = (int) a.getOffset();
                String nm = currentProgram.getLanguage().getUserDefinedOpName(idx2);
                switch (nm) {
                    case "DataMemoryBarrier":
                    case "setISAMode":
                        out.println("    /* " + nm + " */");
                        break;
                    case "hasExclusiveAccess":
                        if (o != null) assign(o, "1");
                        break;
                    default:
                        if (NEON.contains(nm) && o != null && o.getSize() > 0) {
                            emitNeon(in, o, op, nm);
                        } else {
                            unsup("callother_" + nm);
                            out.println("    UNSUP(\"callother " + nm + "\");");
                        }
                }
                break;
            }
            default:
                unsup("op_" + op.getMnemonic());
                out.println("    UNSUP(\"" + op.getMnemonic() + "\");");
        }
    }

    static final Set<String> NEON = new HashSet<>(Arrays.asList(
        "FloatVectorAdd", "FloatVectorSub", "FloatVectorMult", "FloatVectorMax", "FloatVectorMin",
        "FloatVectorAbsolute", "FloatVectorNeg", "FloatVectorCompareGreaterThan",
        "FloatVectorCompareGreaterThanOrEqual", "VectorAdd", "VectorSub", "VectorMultiply",
        "VectorMultiplyAccumulate", "VectorAbsolute", "VectorCompareEqual", "VectorCompareGreaterThan",
        "VectorCompareGreaterThanOrEqual", "VectorBitwiseSelect", "VectorShiftLeft", "VectorShiftRight",
        "VectorShiftRightAccumulate", "VectorCopyLong", "VectorCopyNarrow", "VectorGetElement",
        "VectorSignedToFloat", "VectorUnsignedToFloat", "FixedToFP", "FPToFixed", "SignedSaturate",
        "SignedDoesSaturate", "VectorSignedFixedToFloat", "VectorUnsignedFixedToFloat", "vrev"));

    /** Operation vectorielle : appel de rt_neon avec les operandes, plus le type du mnemonique. */
    void emitNeon(Instruction in, Varnode o, PcodeOp op, String nm) {
        String mn = in.getMnemonicString().toLowerCase();
        int mb = 0, mu = 0, mf = 0;
        String[] parts = mn.split("\\.");
        if (parts.length > 1) {
            String ty = parts[parts.length - 1];
            if (ty.matches("[a-z][0-9]+")) {
                int bits = Integer.parseInt(ty.substring(1));
                mb = bits / 8;
                mu = ty.charAt(0) == 'u' ? 1 : 0;
                mf = ty.charAt(0) == 'f' ? 1 : 0;
            }
        }
        if (nm.equals("vrev")) {
            // "vrev64.32" : octets par bloc = 8, octets par element = 4 (passes dans mb et mu)
            String[] pp = mn.split("\\.");
            int blk = Integer.parseInt(pp[0].substring(4)) / 8;
            int el = pp.length > 1 ? Integer.parseInt(pp[1].replaceAll("[^0-9]", "")) / 8 : 1;
            mb = blk;
            mu = el;
        }
        StringBuilder sb = new StringBuilder();
        sb.append("    { NIn ni_[] = {");
        for (int i = 1; i < op.getNumInputs(); i++) {
            Varnode v = op.getInput(i);
            if (i > 1) sb.append(",");
            if (v.isConstant()) sb.append(" {0," + v.getSize() + ",0x" + Long.toHexString(v.getOffset()) + "ULL,1}");
            else sb.append(" {(const uint8_t*)" + ptr(v) + "," + v.getSize() + ",0,0}");
        }
        sb.append(" }; rt_neon(NEON_" + nm + ", (uint8_t*)" + ptr(o) + ", " + o.getSize() + ", ni_, "
            + (op.getNumInputs() - 1) + ", " + mb + ", " + mu + ", " + mf + "); }");
        out.println(sb);
    }

    /** Operande de plus de 8 octets : pointeur ; une constante est etendue par des zeros. */
    String bigptr(Varnode v, int n, int idx) {
        if (!v.isConstant()) return "(uint8_t*)" + ptr(v);
        out.println("    uint8_t k" + idx + "_[" + n + "] = {0}; { uint64_t c_ = 0x" + Long.toHexString(v.getOffset()) + "ULL; memcpy(k" + idx + "_, &c_, " + Math.min(8, n) + "); }");
        return "k" + idx + "_";
    }

    void bigop(String op, Varnode o, Varnode a, Varnode b) {
        int n = o.getSize();
        out.println("    {");
        String pa = bigptr(a, n, 1);
        String pb = b == null ? "0" : bigptr(b, n, 2);
        out.println("    big_" + op + "((uint8_t*)" + ptr(o) + ", " + pa + ", " + pb + ", " + n + ");");
        out.println("    }");
    }

    void bigshift(String op, Varnode o, Varnode a, Varnode b) {
        int n = o.getSize();
        out.println("    {");
        String pa = bigptr(a, n, 1);
        out.println("    big_" + op + "((uint8_t*)" + ptr(o) + ", " + pa + ", (uint32_t)" + u64(b) + ", " + n + ");");
        out.println("    }");
    }

    void fl(Varnode o, Varnode a, Varnode b, String sym) {
        if (a.getSize() == 4) assign(o, "U32F(F32(" + rd(a) + ") " + sym + " F32(" + rd(b) + "))");
        else assign(o, "U64F(F64(" + rd(a) + ") " + sym + " F64(" + rd(b) + "))");
    }

    void flc(Varnode o, Varnode a, Varnode b, String sym) {
        if (a.getSize() == 4) assign(o, "F32(" + rd(a) + ") " + sym + " F32(" + rd(b) + ")");
        else assign(o, "F64(" + rd(a) + ") " + sym + " F64(" + rd(b) + ")");
    }
}
