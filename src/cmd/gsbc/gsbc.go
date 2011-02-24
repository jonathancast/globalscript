package main;

import "flag";
import "fmt";
import "io";
import "os";

var infile string;
var inline int;

func main() {
    defer func () {
        if r := recover(); r != nil {
            fmt.Fprintf(os.Stderr, "%s\n", r);
        }
    }();
    flag.Parse();
    as := flag.Args();
    if len(as) > 0 {
        for i := 0; i < len(as); i++ {
            f, err := os.Open(as[i], os.O_RDONLY, 0666);
            if err == nil {
                infile = as[i];
                inline = 0;
                assembleFile(f);
            } else {
                fmt.Fprintf(os.Stderr, "%s: %s: %s\n", os.Args[0], as[i], err);
            }
        }
    } else {
        infile = "/dev/stdin";
        inline = 0;
        assembleFile(os.Stdin);
    }
    die("main next");
}

func assembleFile(file io.Reader) {
    die("assembleFile next");
}

func die(s string) {
    switch {
    case infile != "":
        panic(fmt.Sprintf("%s: %s:%d: %s", os.Args[0], infile, inline, s));
    case true:
        panic(fmt.Sprintf("%s: %s", os.Args[0], s));
    }
}
