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

    enum 'SectionLabel' => qw/
        code
        data
    /;

    has section =>
        is => 'rw',
        isa => 'SectionLabel',
        predicate => 'has_section',
    ;
    
    has code_objects =>
        traits => [qw/ Array /],
        is => 'ro',
        isa => 'ArrayRef[CodeObject]',
        default => sub { [] },
        handles => {
            num_code_objects => 'count',
            push_code_obj => 'push',
            all_code_objects => 'elements',
        },
    ;
    
    method code_size()
    {
        sum map { $_->size() } $self->all_code_objects()
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
                when('.document') {
                    $self->assert(!$label, "illegal label $label on directive $directive");
                    $self->start_document();
                }
                when('.lprog') {
                    $self->assert(!$label || $self->expecting_label(), "illegal label $label on directive $directive");
                    $self->assert(!$self->expecting_label() || $label, "label required on directive $directive");
                    $self->assert(@args >= 1, "API type required on $directive");
                    $self->assert(@args <= 1, "too many arguments to $directive");
                    $code_obj = LProg->new(apitype => $args[0]);
                }
                when('.arg') {
                    $self->assert($code_obj, "illegal directive $directive outside a basic block");
                    #$self->assert(@args >= 1, "type required on $directive");
                    $self->assert(@args <= 1, "too many arguments to $directive");
                    $self->assert($code_obj->expecting_arguments(), "un-expected directive $directive");
                    $code_obj->push_argument($label => $args[0]);
                }
                when('CHCONST') {
                    $self->assert($code_obj, "illegal directive $directive outside a basic block");
                    $self->assert(@args >= 1, "missing argument to $directive");
                    $self->assert(@args <= 1, "too many arguments to $directive");
                    my ($contents) = $args[0] =~ m/^'([^'\\]+|\\')'$/;
                    $self->assert($contents, "mal-formed argument to $directive");
                    given($contents) {
                        when('\\\'') {
                            $contents = "'";
                        }
                        when(/^[^\\']$/) {}
                        default { $self->assert('', "mal-formed argument to $directive"); }
                    }
                    $code_obj->push_statement($label => ChConst->new(arg => $contents));
                }
                when('JEPRIM') {
                    $self->assert(!$label, "illegal label $label on directive $directive");
                    $self->assert(@args >= 1, "missing argument to $directive");
                    my $stmt = JEPrim->new(
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
    
    method finish_code_obj($code_obj)
    {
        $code_obj->finish() if $code_obj->can('finish');
        $self->push_code_obj($code_obj);
    }

    method assert($cond, $message)
    {
        die sprintf("%s:%s:%d: %s\n", $0, $self->filename(), $., $message)
            unless $cond
        ;
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
            my $line = '#! '.$self->shebang_line();
            print pack "U*", length($line), map { ord($_) } split //, $line;
        }
        my $magic = {
            document => '!gsdocbc',
            prefix => '!gsprebc',
        }->{$self->filetype()}
        or die "could not find magic number for ".$self->filetype()
        ;
        print pack "U8", map { ord($_) } split //, $magic;
        print pack "N", 0;
        print pack "N4", 0x1c, $self->code_size(), $self->data_size(), $self->string_size();
        $self->output_code();
        $self->output_data();
        $self->output_strings();
    }
}

role Lambda {
    requires 'missing_statements';

    has arguments =>
        traits => [qw/ Array /],
        is => 'ro',
        isa => 'ArrayRef',
        default => sub { [] },
        handles => {
            _push_argument => 'push',
        },
    ;

    method expecting_arguments()
    {
        $self->missing_statements()
    }

    method push_argument($label, $type)
    {
        $self->_push_argument({ label => $label, type => $type});
        $self->_remember_label($label);
    }
}

class CodeObject {
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
    
    method _remember_label($label)
    {
        my $num_labels = $self->_num_labels();
        $self->_push_label($label);
        $self->_store_label($label, $num_labels + 1);
        return $num_labels + 1;
    }
}

class LProg extends CodeObject with Lambda {
    use List::Util qw/
        sum
    /;

    has statements =>
        traits => [qw/ Array /],
        is => 'ro',
        isa => 'ArrayRef[Statement]',
        default => sub { [] },
        handles => {
            missing_statements => 'is_empty',
            _push_statement => 'push',
        },
    ;

    method push_statement($label, $statement)
    {
        $self->_remember_label($label) if defined($label);
        $self->_push_statement($statement);
    }

    around word_size()
    {
        my $typesize = $self->sizeof($self->type_symbol());
        return max $typesize, $self->$orig();
    }

    method size()
    {
        1 + 3 * $self->word_size() + sum map { $_->size() } $self->all_statements()
    }
}

class Statement {
}

class ChConst extends Statement {
}

class JEPrim extends Statement {
}
