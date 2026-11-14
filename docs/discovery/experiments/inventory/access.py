#!/usr/bin/env python3
"""List every read, write and address-take of chosen globals in weemitt/src/game, using clang's AST.

Usage:
  python3 access.py [--repo /home/user/weemitt] [--targets hand,ball,...] [--out DIR] [--files a.c,b.c]

How it works
  For each src/game/*.c it runs `clang -fsyntax-only -x c++ ... -Xclang -ast-dump` with the game's own flags
  (the Makefile's GAME_CXXFLAGS) and parses the text dump as a tree. For each DeclRefExpr of a target global
  it climbs the lvalue chain (member access, array index, parentheses) and looks at the node that uses the chain:
    write  : left side of =, +=, ..., ++/--, left side of a C++ operator= (struct copy, Ptr32 assign)
    addr   : & applied to the chain, or an array member that decays to a pointer. The callee or use is recorded.
    read   : anything else (value use, call by value, comparison, member call such as Ptr32 operator->).
  Compound assignment and ++/-- count as both a write and a read.
Limits
  * A write through a copy of the address (Actor *a = &hand; a->pos.x = 1) is seen only as 'addr' at the &.
  * A write through a pointer stored in the structure (hand.state->x = 1, hand.anim->...) counts as a READ of hand.
  * Code reached by walking actorList/collisionList (which hold &hand) is invisible. Use the identity-test list.
  * Macros are expanded, so a macro that touches a global counts at each use. Line numbers can then be those of
    the macro's use.
  * Only src/game/*.c is scanned (not src/port, not bruteforce).
Output: DIR/events.json (every event), and a text summary on stdout.
"""
import argparse, collections, concurrent.futures, glob, json, os, re, subprocess, sys

DEFAULT_TARGETS = ("hand,ball,handControl,ballControl,player,camera,cameraInfo,gameInfo,pad,saveSlot")

FLAGS = ("-fsyntax-only -x c++ -std=gnu++14 -m32 -malign-double -w -funsigned-char -DM2CTX -D_LANGUAGE_C "
         "-DF3DEX_GBI -D_FINALROM -DNDEBUG -DPORT -Igen -Isrc/port -Iinclude -Iinclude/PR -Isrc/game "
         "-include src/port/port_pre.h -fno-color-diagnostics -Xclang -ast-dump").split()

ASSIGN_OPS = {"=", "+=", "-=", "*=", "/=", "%=", "<<=", ">>=", "&=", "|=", "^="}
PASS = {"ImplicitCastExpr", "CStyleCastExpr", "ParenExpr", "CXXFunctionalCastExpr", "CXXStaticCastExpr",
        "CXXReinterpretCastExpr", "MaterializeTemporaryExpr", "ExprWithCleanups"}
KINDRE = re.compile(r"^([|` -]*)([A-Za-z]+)")
LOCRE = re.compile(r"<(?:([^:<>,]+):)?(?:line:)?(\d+)(?::\d+)?[,>]|<(line|col):(\d+)")


class Node:
    __slots__ = ("kind", "text", "nchild", "callee", "idx")

    def __init__(self, kind, text, idx):
        self.kind, self.text, self.nchild, self.callee, self.idx = kind, text, 0, None, idx


def first_loc(text, cur):
    """Return (file, line) of a node's begin location, updating the dump's running file and line."""
    f, l = cur
    m = re.search(r"<((?:[^<>,]|<[^>]*>)*?)[,>]", text)
    if not m:
        return cur
    tok = m.group(1)
    mm = re.match(r"line:(\d+)", tok)
    if mm:
        return (f, int(mm.group(1)))
    mm = re.match(r"col:\d+", tok)
    if mm:
        return (f, l)
    mm = re.match(r"(.+?):(\d+):\d+$", tok)
    if mm:
        return (mm.group(1), int(mm.group(2)))
    return cur


def process_file(repo, src, targets):
    r = subprocess.run(["clang"] + FLAGS + ["-Igen"] + [src], cwd=repo, stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE, text=True, errors="replace")
    events = []
    stack = []
    cur = ("", 0)
    infunc = None
    funcdepth = 0
    gset = set()
    funcs_seen = set()
    for raw in r.stdout.split("\n"):
        m = KINDRE.match(raw)
        if not m:
            continue
        depth = len(m.group(1)) // 2
        kind = m.group(2)
        text = raw[m.end(2):]
        cur = first_loc(text, cur)
        if kind == "VarDecl" and (depth <= 2 or " extern" in text):
            mv = re.search(r"(?:\S+:\d+(?::\d+)?|col:\d+)(?:\s+(?:used|referenced|extern|static|invalid))*\s+(\w+) '", text)
            if mv:
                gset.add(mv.group(1))
        if infunc is not None and depth <= funcdepth:
            infunc = None
        if infunc is None and kind == "FunctionDecl" and depth <= 3 and cur[0] == src:
            nm = re.search(r">\s+(?:(?:line|col):[\d:]+\s+)?(?:(?:used|static|inline|referenced|implicit|invalid)\s+)*(\w+) '", text)
            infunc = nm.group(1) if nm else None
            funcdepth = depth
            if infunc:
                events.append(dict(file=src, line=cur[1], func=infunc, var="", field=None, kind="def", extra=""))
        if depth == 0:
            stack[:] = []
        parent = stack[depth - 1] if 0 < depth <= len(stack) else None
        idx = 0
        if parent is not None:
            idx = parent.nchild
            parent.nchild += 1
        node = Node(kind, text, idx)
        del stack[depth:]
        stack.append(node)
        if infunc is None or parent is None:
            continue
        if kind == "DeclRefExpr":
            mm = re.search(r"(Var|Function) 0x[0-9a-f]+ '(\w+)'", text)
            if not mm:
                continue
            what, name = mm.groups()
            if what == "Function":
                # remember the callee on the nearest enclosing CallExpr when this is its first child
                for k in range(len(stack) - 2, max(len(stack) - 6, 0), -1):
                    if stack[k].kind in ("CallExpr", "CXXOperatorCallExpr") and stack[k].callee is None:
                        stack[k].callee = name
                        break
                    if stack[k].kind not in PASS:
                        break
                continue
            if "*" in targets:
                if name not in gset:
                    continue
            elif name not in targets:
                continue
            # climb the lvalue chain
            top = len(stack) - 1
            field = None
            while top - 1 >= 0:
                p = stack[top - 1]
                n = stack[top]
                if p.kind == "MemberExpr" and " ." in p.text:
                    mf = re.search(r" \.(\w+) 0x", p.text)
                    if field is None and mf:
                        field = mf.group(1)
                    top -= 1
                    continue
                if p.kind == "ArraySubscriptExpr" and n.idx == 0:
                    top -= 1
                    continue
                if p.kind == "ParenExpr":
                    top -= 1
                    continue
                if p.kind == "ImplicitCastExpr" and "ArrayToPointerDecay" in p.text and top - 2 >= 0 \
                        and stack[top - 2].kind == "ArraySubscriptExpr" and p.idx == 0:
                    top -= 2
                    continue
                if p.kind == "UnaryOperator" and re.search(r"prefix '&'", p.text):
                    # (&x)->f is the same lvalue as x.f
                    k = top - 1
                    while k - 1 >= 0 and stack[k - 1].kind in ("ParenExpr", "ImplicitCastExpr"):
                        k -= 1
                    if k - 1 >= 0 and stack[k - 1].kind == "MemberExpr" and " ->" in stack[k - 1].text:
                        mf = re.search(r" ->(\w+) 0x", stack[k - 1].text)
                        if field is None and mf:
                            field = mf.group(1)
                        top = k - 1
                        continue
                    if k - 1 >= 0 and stack[k - 1].kind == "UnaryOperator" and "prefix '*'" in stack[k - 1].text:
                        top = k - 1
                        continue
                break
            chain = stack[top]
            par = stack[top - 1] if top - 1 >= 0 else None
            kindev = "read"
            extra = ""
            if par is not None:
                if par.kind == "BinaryOperator":
                    op = re.search(r"'(\S+)'\s*$", par.text.strip())
                    op = op.group(1) if op else ""
                    if op in ASSIGN_OPS and chain.idx == 0:
                        kindev = "write+read" if op != "=" else "write"
                elif par.kind == "CompoundAssignOperator":
                    if chain.idx == 0:
                        kindev = "write+read"
                elif par.kind == "UnaryOperator":
                    uop = re.search(r"(prefix|postfix) '(\S+)'", par.text)
                    if uop and uop.group(2) in ("++", "--"):
                        kindev = "write+read"
                    elif uop and uop.group(2) == "&":
                        kindev = "addr"
                elif par.kind == "CXXOperatorCallExpr":
                    op = re.search(r"'(\S+)'\s*$", par.text.strip())
                    op = op.group(1) if op else ""
                    if op in ASSIGN_OPS and chain.idx == 1:
                        kindev = "write" if op == "=" else "write+read"
                elif par.kind == "ImplicitCastExpr" and "ArrayToPointerDecay" in par.text:
                    kindev = "addr"
            if kindev == "addr":
                # find the consumer of the pointer
                k = top - 1
                while k - 1 >= 0 and stack[k].kind in PASS | {"UnaryOperator"}:
                    k -= 1
                cons = stack[k]
                if cons.kind in ("CallExpr", "CXXOperatorCallExpr", "CXXMemberCallExpr"):
                    extra = ("call:%s#%d" % (cons.callee, stack[k + 1].idx - 1)) if cons.callee else "store-ptr"
                elif cons.kind == "BinaryOperator":
                    op = re.search(r"'(\S+)'\s*$", cons.text.strip())
                    op = op.group(1) if op else ""
                    extra = "cmp" if op in ("==", "!=") else ("assign" if op == "=" else "binop" + op)
                elif cons.kind == "VarDecl":
                    extra = "init-local"
                elif cons.kind == "ReturnStmt":
                    extra = "return"
                else:
                    extra = cons.kind
            events.append(dict(file=src, line=cur[1], func=infunc, var=name, field=field, kind=kindev, extra=extra))
    return src, events, r.stderr[-300:] if r.returncode and not events else ""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", default="/home/user/weemitt")
    ap.add_argument("--targets", default=DEFAULT_TARGETS)
    ap.add_argument("--out", default=os.path.dirname(os.path.abspath(__file__)))
    ap.add_argument("--files", default="")
    a = ap.parse_args()
    targets = set(a.targets.split(","))
    files = sorted(glob.glob(os.path.join(a.repo, "src/game/*.c")))
    files = [os.path.relpath(f, a.repo) for f in files]
    if a.files:
        want = set(a.files.split(","))
        files = [f for f in files if os.path.basename(f) in want]
    events = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as ex:
        for src, ev, err in ex.map(lambda f: process_file(a.repo, f, targets), files):
            events += ev
            if err:
                print("clang problem in", src, err, file=sys.stderr)
    json.dump(events, open(os.path.join(a.out, "events.json"), "w"))
    print(len(events), "events from", len(files), "files")


if __name__ == "__main__":
    main()
