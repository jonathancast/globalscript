package main;

import "bufio";
import "container/list";
import "flag";
import "fmt";
import "io";
import "os";
import "regexp";
import "strings";

var infile string;
var inline int;

var commentRe = regexp.MustCompile(" *(#.*)?\n?$");

type outfile struct {
    cursegment int;
    isdocument bool;
    curexpr *list.List;
};

const (
    unksegment = iota;
    documentsegment = iota;
    codesegment = iota;
    datasegment = iota;
);

func main() {
    defer func () {
        if r := recover(); r != nil {
            fmt.Fprintf(os.Stderr, "%s\n", r);
            os.Exit(1);
        }
    }();
    flag.Parse();
    as := flag.Args();
    outfile := outfile {
        cursegment: unksegment,
        isdocument: false,
    };
    if len(as) > 0 {
        for i := 0; i < len(as); i++ {
            f, err := os.Open(as[i], os.O_RDONLY, 0666);
            if err == nil {
                infile = as[i];
                inline = 0;
                assembleFile(f, &outfile);
                emitFile(&outfile);
            } else {
                fmt.Fprintf(os.Stderr, "%s: %s: %s\n", os.Args[0], as[i], err);
            }
        }
    } else {
        infile = "/dev/stdin";
        inline = 0;
        assembleFile(os.Stdin, &outfile);
        emitFile(&outfile);
    }
    die("main next");
}

func assembleFile(file io.Reader, outfile *outfile) {
    buf := bufio.NewReader(file);
    if buf == nil {
        die("Could not allocate buffer for input");
    }
    for {
        inline++;
        line, err := buf.ReadString('\n');
        switch {
        case err == nil:
            assembleLine(line, outfile);
        case err == os.EOF:
            assembleLine(line, outfile);
            return;
        case true:
            die(fmt.Sprint(err));
        }
    }
}

func assembleLine(line string, outfile *outfile) {
    line = commentRe.ReplaceAllString(line, "");
    switch {
    case line == "" || line == "\n":
        return;
    case true:
        fields := strings.Fields(line);
        address := "";
        if !strings.HasPrefix(line, "\t") {
            address, fields = fields[0], fields[1:];
        }
        if len(fields) < 1 {
            die("no command");
        }
        command, fields := fields[0], fields[1:];
        switch command {
        case ".document":
            expectSegment(outfile, documentsegment - 1);
            balkAtAddress(address);
            outfile.cursegment = documentsegment;
            outfile.isdocument = true;
            outfile.curexpr = nil;
        case ".lprog":
            expectBlockBreak(outfile);
            switch {
            case len(fields) < 1:
                die("not enough arguments to .lprog; expected <API type symbol>");
            case len(fields) > 1:
                die("too many arguments to .lprog; expected <API type symbol>");
            }
            var addr address;
            if outfile.isdocument && outfile.codeBlocks.Len() == 0 {
                balkAtAddress(address);
            } else {
                addr = interAdress(address);
            }
            block := block {
                blocktype: lprog,
                apitype: inter(fields[0]),
                address: addr,
            };
            outfile.codeBlocks.pushBack(&block);
            outfile.curblock = &block;
        default:
            die(fmt.Sprintf("unknown command '%s'", command));
        }
    }
}

func expectSegment(outfile *outfile, expsegment int) {
    if outfile.cursegment != expsegment {
        die(fmt.Sprintf("incorrect segment for declaration; expected: %s; got %s",
            fmtsegment(expsegment),
            fmtsegment(outfile.cursegment),
        ));
    }
}

func fmtsegment(seg int) string {
    switch seg {
    case documentsegment:
        return ".document";
    default:
        die("unknown segment #%d", seg);
    }
    return "";
}

func emitFile(outfile *outfile) {
    die("emitFile next");
}

func die(format string, a ...interface {}) {
    s := fmt.Sprintf(format, a...);
    switch {
    case infile != "":
        panic(fmt.Sprintf("%s: %s:%d: %s", os.Args[0], infile, inline, s));
    case true:
        panic(fmt.Sprintf("%s: %s", os.Args[0], s));
    }
}
