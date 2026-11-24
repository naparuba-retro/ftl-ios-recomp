"""Correctifs appliques au code C genere (a rejouer apres tout relift de genj).
1. TextInput::OnLoop (0x11c0f0) : le filtre "lettres/chiffres/espace" rejetait les octets >= 0x80 ; on les laisse passer
   pour que les noms accentues (UTF-8 envoye octet par octet par l'hote) soient acceptes."""
import re, sys
d = sys.argv[1] if len(sys.argv) > 1 else "genj"
f = d + "/gen_funcs_023.c"
s = open(f, errors="replace").read()
marker = "L_0011c18c:;"
i = s.index(marker)
old = "if (t3e00_1) { goto L_0011c224; }"
j = s.index(old, i)
assert j - i < 400, "motif inattendu"
if "G4(0x38) < 0x80u" in s[i:j + 120]:
    print("already applied")
else:
    s = s[:j] + "if (t3e00_1 && G4(0x38) < 0x80u) { goto L_0011c224; }" + s[j + len(old):]
    open(f, "w", errors="replace").write(s)
    print("fix applied in", f)
