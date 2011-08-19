use MooseX::Declare;
use feature "switch";

class AssembleFile {
    use Moose::Util::TypeConstraints;

    use List::Util qw/
        sum
    /;
    
    has filename =>
        is => 'ro',
        isa => 'Str',
        required => 1,
    ;
    
    has filehandle =>
        is => 'ro',
        isa => 'FileHandle',
        required => 1,
    ;

    has shebang_line =>
        is => 'rw',
        isa => 'Str',
    ;

    enum FileType => qw/
        document
        prefix
    /;
    
    has filetype =>
        is => 'rw',
        isa => 'FileType',
        default => 'prefix',
    ;

    enum SectionLabel => qw/
        code
        data
    /;

    has section =>
        is => 'rw',
        isa => 'SectionLabel',
        predicate => 'has_section',
    ;

    method is_data { $self->has_section() && $self->section() eq 'data' }

    has code_objects =>
        traits => [qw/ Array /],
        is => 'ro',
        isa => 'ArrayRef[CodeObject]',
        default => sub { [] },
        handles => {
            num_code_objects => 'count',
            push_code_obj => 'push',
            all_code_objects => 'elements',
            map_code_objects => 'map',
        },
    ;

    has data_objects =>
        traits => [qw/ Array /],
        default => sub { [] },
        handles => {
            push_data_obj => 'push',
            map_data_objects => 'map',
        },
    ;

    has symbol_table =>
        is => 'ro',
        isa => 'SymbolTable',
        default => sub { SymbolTable->new() },
        handles => [qw/
            map_symbol
        /],
    ;
    
    method code_size()
    {
        sum 0, map { $_->size() } $self->all_code_objects()
    }

    method data_size()
    {
        sum 0, $self->map_data_objects(sub { $_->size() })
        # sum ≡ reduce { $a + $b }, so sum() = undef.
        # Because List::Util is retarded
    }

    method string_size()
    {
        sum 0, $self->map_symbol(sub { $_->size() });
    }

    method assemble()
    {
        my $filename = $self->filename();
        my $die = $SIG{__DIE__} || \&die;
        local $SIG{__DIE__} = sub {
            my ($err) = @_;
            $err =~ s{, \<\$fh\> line }{, $filename:}g;
            $die->($err);
        };

        my $fh = $self->filehandle();
        binmode($fh, q{:utf8});
        my ($code_obj, $data_obj);
        while (my $line = <$fh>) {
            chomp($line);
            if ($line =~ m/^#! +(.*)/) {
                $self->shebang_line($1);
                next;
            }
            $line =~ s/#.*//;
            next unless $line =~ m/\S/;
            my ($label, $directive, @args) = split qr/\s+/, $line;
            given ($directive) {
                when($self->is_data()) {
                    $self->assert($label, "label required on data entry");
                    unless (defined $self->data_size()) {
                        $self->panic(qq{
                            data_size somehow undefined!
                            The data segment looks like:@{[ join("\n", $self->map_data_objects(sub { $_->dump() })) ]}
                        });
                    }
                    $self->symbol_table()->find_or_create_symbol('data', $label, $self->data_size());
                    $self->push_data_obj(DataObj->new(
                        symbol_table => $self->symbol_table(),
                        code_symbol => $self->symbol_table->find_symbol('code', $directive),
                        arguments => [ @args ],
                    ));
                }
                when('.document') {
                    $self->assert(!$label, "illegal label $label on directive $directive");
                    $self->start_document();
                }
                when('.lprog') {
                    $self->code_label($label, $directive);
                    $self->assert(@args >= 1, "API type required on $directive");
                    $self->assert(@args <= 1, "too many arguments to $directive");
                    $code_obj = LProg->new(symbol_table => $self->symbol_table(), apitype => $args[0]);
                }
                when('.expr') {
                    $self->code_label($label, $directive);
                    $self->assert(@args <= 1, "too many arguments to $directive");
                    $code_obj = Expr->new(symbol_table => $self->symbol_table());
                }
                when('.arg') {
                    $self->assert($code_obj, "illegal directive $directive outside a basic block");
                    #$self->assert(@args >= 1, "type required on $directive");
                    $self->assert(@args <= 1, "too many arguments to $directive");
                    $self->assert($code_obj->expecting_arguments(), "un-expected directive $directive");
                    $code_obj->push_argument($label => $args[0]);
                }
                when('.global') {
                    $self->assert($code_obj, "illegal directive $directive outside a basic block");
                    $self->assert($label, "label required on directive $directive");
                    $self->assert(@args <= 0, "too many arguments to $directive");
                    $code_obj->push_global($label);
                }
                when('CHCONST') {
                    $self->assert($code_obj, "illegal directive $directive outside a basic block");
                    $self->assert(@args >= 1, "missing argument to $directive");
                    $self->assert(@args <= 1, "too many arguments to $directive");
                    my ($contents) = $args[0] =~ m/^'([^'\\]+|\\.)'$/;
                    $self->assert($contents, "mal-formed argument to $directive");
                    given($contents) {
                        when('\\\'') {
                            $contents = "'";
                        }
                        when('\\s') {
                            $contents = ' ';
                        }
                        when('\\n') {
                            $contents = "\n";
                        }
                        when(/^[^\\']$/) {}
                        default { $self->assert('', "mal-formed argument to $directive"); }
                    }
                    $code_obj->push_statement($label => ChConst->new(
                        symbol_table => $self->symbol_table(),
                        arg => $contents,
                    ));
                }
                when('ALLOC') {
                    $self->assert($code_obj, "illegal directive $directive outside a basic block");
                    $self->assert(@args >= 1, "missing argument to $directive");
                    my $stmt = Alloc->new(
                        symbol_table => $self->symbol_table(),
                        code_ref => $self->symbol_table()->find_or_create_symbol('code', $args[0]),
                        arguments => [ map {
                            $code_obj->lookup_label($_)
                            || $self->assert('', "unknown label $_")
                        } @args[1..$#args] ],
                    );
                    $code_obj->push_statement($label => $stmt);
                }
                when('EPRIM') {
                    $self->assert($code_obj, "illegal directive $directive outside a basic block");
                    $self->assert(@args >= 1, "missing argument to $directive");
                    my $stmt = EPrim->new(
                        symbol_table => $self->symbol_table(),
                        primitive => $args[0],
                        arguments => [ map {
                            $code_obj->lookup_label($_)
                            || $self->assert('', "unknown label $_")
                        } @args[1..$#args] ],
                    );
                    $code_obj->push_statement($label => $stmt);
                }
                when('JEPRIM') {
                    $self->assert($code_obj, "illegal directive $directive outside a basic block");
                    $self->assert(!$label, "illegal label $label on directive $directive");
                    $self->assert(@args >= 1, "missing argument to $directive");
                    my $stmt = JEPrim->new(
                        symbol_table => $self->symbol_table(),
                        primitive => $args[0],
                        arguments => [ map {
                            $code_obj->lookup_label($_)
                            || $self->assert('', "unknown label $_")
                        } @args[1..$#args] ],
                    );
                    $code_obj->push_statement(undef, $stmt);
                    $self->finish_code_obj($code_obj) if $code_obj;
                    undef $code_obj;
                }
                when('RETURN') {
                    $self->assert($code_obj, "illegal directive $directive outside a basic block");
                    $self->assert(!$label, "illegal label $label on directive $directive");
                    $self->assert(@args >= 1, "missing argument to $directive");
                    $self->assert(@args <= 1, "too many arguments to $directive");
                    my $stmt = Return->new(
                        symbol_table => $self->symbol_table(),
                        value => $code_obj->lookup_label($args[0]),
                    );
                    $code_obj->push_statement(undef, $stmt);
                    $self->finish_code_obj($code_obj) if $code_obj;
                    undef $code_obj;
                }
                when('YIELD') {
                    $self->assert($code_obj, "illegal directive $directive outside a basic block");
                    $self->assert(!$label, "illegal label $label on directive $directive");
                    $self->assert(@args >= 1, "missing argument to $directive");
                    $self->assert(@args <= 1, "too many arguments to $directive");
                    my $stmt = Yield->new(
                        symbol_table => $self->symbol_table(),
                        value => $code_obj->lookup_label($args[0]),
                    );
                    $code_obj->push_statement(undef, $stmt);
                    $self->finish_code_obj($code_obj) if $code_obj;
                    undef $code_obj;
                }
                when('.data') {
                    $self->assert(!$label, "illegal label $label on directive $directive");
                    $self->assert(!$code_obj, "illegal directive $directive inside a basic block");
                    $self->start_data();
                }
                default {
                    $self->assert('', "Unknown directive '$directive'");
                }
            }
        }
    }

    method start_document()
    {
        $self->assert(!$self->has_section(), ".document must be the first declaration in the file");
        $self->section('code');
        $self->filetype('document');
    }

    method start_data()
    {
        $self->section('data');
    }

    method code_label(Maybe[Str] $label, Str $directive)
    {
        $self->assert(!$label || $self->expecting_label(), "illegal label $label on directive $directive");
        $self->assert(!$self->expecting_label() || $label, "label required on directive $directive");
        $self->symbol_table()->find_or_create_symbol('code', $label, $self->code_size());
    }

    method finish_code_obj($code_obj)
    {
        $code_obj->finish() if $code_obj->can('finish');
        $self->push_code_obj($code_obj);
    }

    method assert($cond, $message)
    {
        die sprintf("%s: %s:%d: %s\n", $0, $self->filename(), $., $message)
            unless $cond
        ;
    }

    method panic(Str $message)
    {
        $message =~ s/^\s+//mg;
        die sprintf("%s: %s:%d: Panic: %spanicing", $0, $self->filename(), $., $message);
    }

    method expecting_label()
    {
        return 1 if $self->filetype() ne 'document';
        return 1 if $self->section() ne 'code';
        return 1 if $self->num_code_objects() > 0;
        return '';
    }

    method output()
    {
        if ($self->shebang_line()) {
            my $line = '#! '.$self->shebang_line()."\n";
            print pack "U*", map { ord($_) } split //, $line;
        }
        my $magic = {
            document => '!gsdocbc',
            prefix => '!gsprebc',
        }->{$self->filetype()}
        or die "could not find magic number for ".$self->filetype()
        ;
        print pack "U8", map { ord($_) } split //, $magic;
        print pack "C4", 0x00, 0x00, 0x00, 0x00;
        print pack "N4", 0x1c, $self->code_size(), $self->data_size(), $self->string_size();
        $self->output_code();
        $self->output_data();
        $self->output_strings();
    }

    method output_code()
    {
        $self->map_code_objects(sub { $_->output() });
    }

    method output_data
    {
        $self->map_data_objects(sub { $_->output() });
    }

    method output_strings
    {
        $self->map_symbol(sub { $_->finalize() });
        $self->map_symbol(sub { $_->output() });
    }
}

role Lambda {
    use List::Util qw/ max /;

    requires 'missing_statements';

    has arguments =>
        traits => [qw/ Array /],
        is => 'ro',
        isa => 'ArrayRef',
        default => sub { [] },
        handles => {
            _push_argument => 'push',
            num_arguments => 'count',
        },
    ;

    method expecting_arguments()
    {
        $self->missing_statements()
    }

    method push_argument($label, Maybe $type)
    {
        $self->_push_argument({ label => $label, type => $type});
        $self->_remember_label($label);
    }

    around word_size
    {
        my $argsize = $self->sizeof($self->num_arguments());
        return max $argsize, $self->$orig();
    }
}

role SizeBasics {
    method sizeof(Int $n)
    {
        return 1 unless $n >> 8;
        return 2 unless $n >> 16;
        return 4 unless $n >> 32;
        return 8 unless $n >> 64;
        die "Panic: Need a word size for $n, which is too large";
    }

    method size_to_bitfield(Int $n)
    {
        return 0 unless $n > 8;
        return 1 unless $n > 16;
        return 2 unless $n > 32;
        return 3 unless $n > 64;
        die "Panic: Need a word size bitfield for $n, which is too large";
    }

    method packed_int(Int $n)
    {
        given ($self->word_size()) {
            when ($_ <= 8) { return pack "C", $n }
            when ($_ <= 16) { return pack "n", $n }
            when ($_ <= 32) { return pack "N", $n }
            when ($_ <= 64) {
                my $hw = $n >> 32;
                my $lw = $n & ((2 << 32) - 1);
                return pack "NN", $hw, $lw;
            }
        }
        die "Invalid word size @{[ $self->word_size() ]}";
    }
}

class CodeObject with SizeBasics {
    use constant EXPR => 0x4;
    use constant LPROG => 0xe;

    has symbol_table =>
        is => 'ro',
        isa => 'SymbolTable',
        required => 1,
    ;

    has _labels =>
        traits => [qw/ Array /],
        is => 'ro',
        isa => 'ArrayRef[Str]',
        default => sub { [] },
        handles => {
            _num_labels => 'count',
            _push_label => 'push',
        },
    ;
    
    has _label_xref =>
        traits => [qw/ Hash /],
        is => 'ro',
        isa => 'HashRef[Int]',
        default => sub { {} },
        handles => {
            lookup_label => 'get',
            _store_label => 'set',
        },
    ;

    has free_variables =>
        traits => ['Array'],
        is => 'ro',
        isa => 'ArrayRef[Str]',
        default => sub { [] },
        handles => {
            num_free_variables => 'count',
        },
    ;

    has global_variables =>
        traits => ['Array'],
        is => 'ro',
        isa => 'ArrayRef[HashRef]',
        default => sub { [] },
        handles => {
            num_global_variables => 'count',
            _push_global => 'push',
        },
    ;

    has statements =>
        traits => [qw/ Array /],
        is => 'ro',
        isa => 'ArrayRef[Statement]',
        default => sub { [] },
        handles => {
            missing_statements => 'is_empty',
            _push_statement => 'push',
            all_statements => 'elements',
            map_statements => 'map',
        },
    ;

    method _remember_label($label)
    {
        my $num_labels = $self->_num_labels();
        $self->_push_label($label);
        $self->_store_label($label, $num_labels + 1);
        return $num_labels + 1;
    }

    method word_size
    {
        $self->sizeof($self->num_free_variables());
    }

    method push_global($label)
    {
        my $symbol = $self->symbol_table()->find_or_create_symbol('data', $label);
        $self->_push_global({ label => $label, value => $symbol});
        $self->_remember_label($label);
    }

    method push_statement(Maybe $label, $statement)
    {
        $self->_remember_label($label) if defined($label);
        $self->_push_statement($statement);
    }
}

role APIBlock
{
    has apitype =>
        is => 'ro',
        isa => 'Str',
        required => 1,
    ;

    method type_symbol
    {
        return $self->symbol_table()->find_or_create_symbol('api_type', $self->apitype());
    }
}

class LProg extends CodeObject with APIBlock with Lambda
{
    use List::Util qw/
        max
        sum
    /;

    around word_size()
    {
        my $typesize = $self->sizeof($self->type_symbol()->offset());
        return max $typesize, $self->$orig();
    }

    method size()
    {
        1 + 3 * $self->word_size() + sum map { $_->size() } $self->all_statements()
    }

    method output()
    {
        my $size = $self->size_to_bitfield($self->word_size());
        print pack "C", ($self->LPROG() << 2 | $size);
        print $self->packed_int($self->num_free_variables());
        print $self->packed_int($self->num_arguments());
        print $self->packed_int($self->type_symbol()->offset());
        $self->map_statements(sub { $_->output() });
    }
}

class Expr extends CodeObject
{
    use List::Util qw/
        sum
    /;

    method size()
    {
        1 + 2 * $self->word_size() + sum map { $_->size() } $self->all_statements()
    }

    method output()
    {
        my $size = $self->size_to_bitfield($self->word_size());
        print pack "C", ($self->EXPR() << 2 | $size);
        print $self->packed_int($self->num_global_variables());
        print $self->packed_int($self->num_free_variables());
        $self->map_statements(sub { $_->output() });
    }
}

class Statement with SizeBasics
{
    use constant ALLOC => 0x00;
    use constant CHCONST => 0x06;
    use constant EPRIM => 0x09;
    use constant JEPRIM => 0x0e;
    use constant YIELD => 0x13;

    has symbol_table =>
        is => 'ro',
        isa => 'SymbolTable',
        required => 1,
    ;
}

class ChConst extends Statement
{
    use Encode;
    use Moose::Util::TypeConstraints;

    subtype 'Char'
        => as 'Str'
        => where { length($_) == 1 }
        => message { "Characters must be single characters..." }
    ;

    has arg =>
        is => 'ro',
        isa => 'Char',
        required => 1,
    ;

    method size
    {
        return 1 + length(encode_utf8($self->arg()));
    }

    method output
    {
        print pack "C", ($self->CHCONST << 2);
        print encode_utf8($self->arg());
    }
}

class Alloc extends Statement
{
    use Encode;
    use List::Util qw/
        max
    /;

    has code_ref =>
        is => 'ro',
        isa => 'Symbol',
        required => 1,
    ;

    has arguments =>
        traits => [qw/ Array /],
        is => 'ro',
        isa => 'ArrayRef[Int]',
        required => 1,
        handles => {
            num_arguments => 'count',
            all_arguments => 'elements',
            map_arguments => 'map',
        },
    ;

    method word_size()
    {
        max map { $self->sizeof($_) } $self->code_ref()->offset(), $self->all_arguments()
    }

    method size()
    {
        (2 + $self->num_arguments()) * $self->word_size()
    }

    method output()
    {
        my $size = $self->size_to_bitfield($self->word_size());
        print pack "C", ($self->ALLOC << 2 | $size);
        print $self->packed_int(1 + $self->num_arguments());
        print $self->packed_int($self->code_ref()->offset());
        $self->map_arguments(sub { print $self->packed_int($_) });
    }
}

class EPrim extends Statement {
    use List::Util qw/ max /;

    has primitive =>
        is => 'ro',
        isa => 'Str',
        required => 1,
    ;

    has arguments =>
        traits => [qw/ Array /],
        is => 'ro',
        isa => 'ArrayRef[Int]',
        required => 1,
        handles => {
            all_arguments => 'elements',
            num_arguments => 'count',
            map_arguments => 'map',
        },
    ;

    method primitive_symbol
    {
        return $self->symbol_table()->find_or_create_symbol('api_type', $self->primitive());
    }

    method word_size()
    {
        return max map { $self->sizeof($_) }
            $self->num_arguments(),
            $self->primitive_symbol()->offset(),
            $self->all_arguments(),
        ;
    }

    method size
    {
        1 + $self->word_size() + $self->word_size() + $self->num_arguments() * $self->word_size()
    }

    method output
    {
        my $size = $self->size_to_bitfield($self->word_size());
        print pack "C", ($self->EPRIM << 2 | $size);
        print $self->packed_int(1 + $self->num_arguments());
        print $self->packed_int($self->primitive_symbol()->offset());
        $self->map_arguments(sub { print $self->packed_int($_) });
    }
}

class JEPrim extends Statement {
    use List::Util qw/ max /;

    has primitive =>
        is => 'ro',
        isa => 'Str',
        required => 1,
    ;

    has arguments =>
        traits => [qw/ Array /],
        is => 'ro',
        isa => 'ArrayRef[Int]',
        required => 1,
        handles => {
            all_arguments => 'elements',
            num_arguments => 'count',
            map_arguments => 'map',
        },
    ;

    method primitive_symbol
    {
        return $self->symbol_table()->find_or_create_symbol('api_type', $self->primitive());
    }

    method word_size()
    {
        return max map { $self->sizeof($_) }
            $self->num_arguments(),
            $self->primitive_symbol()->offset(),
            $self->all_arguments(),
        ;
    }

    method size
    {
        1 + $self->word_size() + $self->word_size() + $self->num_arguments() * $self->word_size()
    }

    method output
    {
        my $size = $self->size_to_bitfield($self->word_size());
        print pack "C", ($self->JEPRIM << 2 | $size);
        print $self->packed_int(1 + $self->num_arguments());
        print $self->packed_int($self->primitive_symbol()->offset());
        $self->map_arguments(sub { print $self->packed_int($_) });
    }
}

class Return extends Statement {
    has value =>
        is => 'ro',
        isa => 'Int',
        required => 1,
    ;

    method word_size
    {
        $self->sizeof($self->value())
    }

    method size
    {
        1 + $self->word_size()
    }
}

class Yield extends Statement {
    has value =>
        is => 'ro',
        isa => 'Int',
        required => 1,
    ;

    method word_size
    {
        $self->sizeof($self->value())
    }

    method size
    {
        1 + $self->word_size()
    }

    method output
    {
        my $size = $self->size_to_bitfield($self->word_size());
        print pack "C", ($self->YIELD << 2 | $size);
        print $self->packed_int($self->value());
    }
}

class SymbolTable {
    has symbols =>
        traits => ['Array'],
        is => 'ro',
        isa => 'ArrayRef[Symbol]',
        default => sub { [] },
        handles => {
            first_symbol => 'first',
            _push_symbol => 'push',
            map_symbol => 'map',
        },
    ;

    has size =>
        traits => ['Counter'],
        is => 'ro',
        isa => 'Int',
        default => 0,
        handles => {
            _inc_size => 'inc',
        },
    ;

    method find_or_create_symbol(Str $type, Str $name, Int $value?)
    {
        $type = $self->_fix_type($type, $name);
        my $symbol = $self->first_symbol(sub { $_->type() eq $type && $_->name() eq $name });
        $symbol->value($value) if $symbol && defined($value);
        return $symbol ||= $self->_create_symbol($type, $name, $value);
    }

    method find_symbol(Str $type, Str $name)
    {
        $type = $self->_fix_type($type, $name);
        my $symbol = $self->first_symbol(sub { $_->type() eq $type && $_->name() eq $name });
        die "Unknown symbol $name of type $type" unless $symbol;
        return $symbol
    }

    method _create_symbol(Str $type, Str $name, Maybe[Int] $value)
    {
        my $symbol = Symbol->new(type => $type, offset => $self->size(), name => $name, value => $value);
        $self->_inc_size($symbol->size());
        $self->_push_symbol($symbol);
        return $symbol;
    }

    method _fix_type(Str $type, Str $name)
    {
        given($type) {
            when([qw/ code data /]) {
                $type = $name =~ m/^\./
                    ? "private_$type"
                    : "public_$type"
                ;
            }
        }
        return $type;
    }
}

class Symbol {
    use Encode;
    use Moose::Util::TypeConstraints;

    use List::Util qw/ sum /;

    enum SymbolType => qw/
        external_code
        external_data
        api_type
        private_code
        private_data
        public_code
        public_data
    /;

    has type =>
        is => 'ro',
        isa => 'SymbolType',
        required => 1,
        writer => '_type',
    ;

    has name =>
        is => 'ro',
        isa => 'Str',
        required => 1,
    ;

    has offset =>
        is => 'ro',
        isa => 'Int',
        required => 1,
    ;

    method _value_set(Maybe[Int] $new_value, Maybe[Int] $old_value?)
    {
        confess "Panic! @{[ $self->name() ]} set when it already has a value"
            if defined($old_value)
        ;
    }

    has value =>
        is => 'rw',
        isa => 'Maybe[Int]',
        trigger => \&_value_set,
    ;

    method size
    {
        return 1 + 4 + 1 + length(encode("utf8", $self->name()));
    }

    method finalize
    {
        return if defined $self->value();
        given ($self->type()) {
            when ('public_code') {
                $self->_type('external_code');
            }
            when ('public_data') {
                $self->_type('external_data');
            }
            when ('api_type') {}
            default {
                die "Un-known value for symbol ".$self->name()." of type ".$self->type()
            }
        }
    }

    method expecting_value
    {
        !grep { $_ eq $self->type() } qw/
            external_code
            external_data
            api_type
        /
    }

    method type_code
    {
        my $type_code = {
            external_code => 0,
            public_code => 4,
            private_code => 5,
            public_data => 6,
            private_data => 7,
            api_type => 9,
        }->{$self->type()};
        die "Panic!  Unknown type ".$self->type() unless defined $type_code;
        return $type_code;
    }

    method value_required
    {
        given ($self->type()) {
            when (m/^private_(?:code|data)$/) { return 1 }
            default { return '' }
        }
    }

    method output
    {
        print pack "C", $self->type_code();
        my @bytes = map { ord($_) } split //, encode("utf8", $self->name());
        die "Name @{[ $self->name() ]} exceeds 255 bytes; you need to extend the bytcode format to support this"
            if @bytes >= 256;
        my $checksum = sum(0, @bytes) % 256;
        print pack "C", $checksum;
        if ($self->expecting_value()) {
            print pack "N", $self->value();
        }
        print pack "C", scalar(@bytes);
        print pack "C*", @bytes;
    }
}

class DataObj {
    has code_symbol =>
        is => 'ro',
        isa => 'Symbol',
        required => 1,
        handles => {
            code_symbol_offset => 'offset',
        },
    ;

    has arguments =>
        traits => [qw/ Array /],
        is => 'ro',
        isa => 'ArrayRef[Symbol]',
        required => 1,
        handles => {
            num_arguments => 'count',
            map_arguments => 'map',
        },
    ;

    method size
    {
        4 + 4 * $self->num_arguments()
    }

    method output
    {
        print pack "N", $self->code_symbol_offset();
        $self->map_arguments(sub { print pack "N", $_->offset() });
    }
}
