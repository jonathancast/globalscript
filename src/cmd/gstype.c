#include <u.h>
#include <libc.h>
#include <libglobalscript.h>

void
p9main(int argc, char **argv)
{
    gsmain(argc, argv);
}

void
gsadd_client_prim_sets()
{
}

void
gsrun(char *doc, struct gsfile_symtable *symtable, struct gspos pos, gsvalue prog, struct gstype *type)
{
    gsvalue c, s;
    gstypecode st;
    char buf[0x100], *o;
    char err[0x100];

    s = prog;
    c = 0;
    o = buf;

    do {
        if (c) {
            st = GS_SLOW_EVALUATE(c);

            switch (st) {
                case gstyunboxed: {
                    if (!(o = gsrunetochar(c, o, buf + sizeof(buf), err, err + sizeof(err)))) {
                        ace_down();
                        gsfatal("Cannot format rune %p: %s", c, err);
                    }
                    if (o >= buf + sizeof(buf) - 4) {
                        if (write(1, buf, o - buf) != o - buf) {
                            ace_down();
                            gsfatal("Output failed: %r");
                        }
                        o = buf;
                    }
                    c = 0;
                    break;
                }
                default:
                    ace_down();
                    gsfatal_unimpl(__FILE__, __LINE__, "gsrun: st = %d", st);
            }
        } else {
            st = GS_SLOW_EVALUATE(s);

            switch (st) {
                case gstywhnf: {
                    struct gsconstr *constr;

                    constr = (struct gsconstr *)s;
                    switch (constr->constrnum) {
                        case 0: { /* §gs{:} */
                            c = constr->arguments[0];
                            s = constr->arguments[1];
                            break;
                        }
                        case 1: { /* §gs{nil} */
                            if (o > buf) {
                                if (write(1, buf, o - buf) != o - buf) {
                                    ace_down();
                                    gsfatal("Output failed: %r");
                                }
                            }
                            ace_down();
                            exits("");
                        }
                        default:
                            ace_down();
                            gsfatal_unimpl(__FILE__, __LINE__, "gsrun: constrnum = %d", constr->constrnum);
                    }
                    break;
                }
                default:
                    ace_down();
                    gsfatal_unimpl(__FILE__, __LINE__, "gsrun: st = %d", st);
            }
        }
    } while (1);
}
