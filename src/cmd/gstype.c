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
gscheck_global_gslib(struct gspos pos, struct gsfile_symtable *symtable)
{
}

void
gscheck_program(char *doc, struct gsfile_symtable *symtable, struct gspos pos, struct gstype *ty)
{
    struct gstype *tyw, *tyelem;
    char errbuf[0x100];

    tyw = ty;
    if (
        gstype_expect_app(errbuf, errbuf + sizeof(errbuf), tyw, &tyw, &tyelem) < 0
        || gstype_expect_abstract(errbuf, errbuf + sizeof(errbuf), tyw, "list.t") < 0
        || gstype_expect_abstract(errbuf, errbuf + sizeof(errbuf), tyelem, "rune.t") < 0
    ) {
        gsfatal("%s: Bad type: %s", doc, errbuf);
    }
}

void
gsrun(char *doc, struct gspos pos, gsvalue prog, int argc, char **argv)
{
    gsvalue c, s;
    gstypecode st;
    char buf[0x100], *o;

    s = prog;
    c = 0;
    o = buf;

    do {
        if (c) {
            st = GS_SLOW_EVALUATE(c);

            switch (st) {
                case gstyunboxed: {
                    o = gsrunetochar(c, o, buf + sizeof(buf));
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
                case gstystack:
                    break;
                case gstyindir:
                    s= GS_REMOVE_INDIRECTION(s);
                    break;
                case gstywhnf: {
                    struct gsconstr *constr;
                    struct gsconstr_args *args;

                    constr = (struct gsconstr *)s;
                    args = (struct gsconstr_args *)constr;
                    switch (args->constrnum) {
                        case 0: { /* §gs{:} */
                            c = args->arguments[0];
                            s = args->arguments[1];
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
                            gsfatal(UNIMPL("%P: gsrun: constrnum = %d"), constr->pos, args->constrnum);
                    }
                    break;
                }
                case gstyerr: {
                    char buf[0x100];

                    gserror_format(buf, buf + sizeof(buf), (struct gserror *)s);
                    fprint(2, "%s\n", buf);
                    exits("err");

                    break;
                }
                case gstyimplerr: {
                    char buf[0x100];

                    gsimplementation_failure_format(buf, buf + sizeof(buf), (struct gsimplementation_failure *)s);
                    fprint(2, "%s\n", buf);
                    exits("unimpl");

                    break;
                }
                default:
                    ace_down();
                    gsfatal_unimpl(__FILE__, __LINE__, "gsrun: st = %d", st);
            }
        }
    } while (1);
}
