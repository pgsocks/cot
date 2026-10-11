# CoT command-line contract

The `cot_cli` CMake target produces the executable `cot`, controlled by
`BUILD_CLI` (default `ON`).

```text
cot encode [INPUT|-] [-o OUTPUT|-]   # XML -> Protobuf
cot decode [INPUT|-] [-o OUTPUT|-]   # Protobuf -> XML
```

CoT XML is the editable representation. Each invocation reads one complete
message through EOF, then converts it according to the [public API](api.md).
Protobuf means an unframed official `TakMessage` containing only an event.
Continuous streams, transport framing, timing, JSON archives, and recording
are outside this command's contract.

Omitted input or output and the literal `-` select stdin or stdout.
`-o PATH` and `--output PATH` are equivalent; either may precede or follow the
single input operand after the subcommand. The separated argument form is
supported; `--output=PATH` is not. Duplicate output options, a missing option
value, unknown options or commands, or multiple input operands are argument
errors. `cot --help`, `cot encode --help`, and `cot decode --help` print usage
to stdout and exit successfully without reading input or opening files.
A valid command with `--help` ignores conversion operands; otherwise argument
errors still fail. No command is an argument error.

`--` ends option parsing; subsequent arguments are input operands even when
they begin with a dash. For example, `cot encode -o result.pb -- -event.xml`
reads a file named `-event.xml`. Output option values are consumed literally,
including values beginning with a dash; `-` still selects stdout. No shell
expansion is performed by the tool.

Examples (a POSIX shell supplies the pipes and redirects):

```sh
cat event.xml | cot encode -o event.pb
cot decode event.pb > decoded.xml
cot decode < event.pb | cot encode > roundtrip.pb
cot encode event.xml --output event.pb
cot decode event.pb -o decoded.xml
cot encode - -o - < event.xml > event.pb
```

For each `cot encode`, the tool reads bytes, calls
`cot_decode(COT_FORMAT_XML, ...)`, obtains the event using
`cot_decoded_event_get`, then calls `cot_encode(COT_FORMAT_PROTOBUF, ...)`.
For `cot decode`, swap those format selectors. The [C++ conversion
example](api.md#parsing-and-conversion-in-c17) shows these exact public calls
and owner cleanup. The [C construction example](api.md#c-construction-and-encoding)
shows how to create the equivalent input without XML. Argument parsing and
file I/O belong to the tool; message validation and conversion belong to the
library.

Conversion must finish successfully before writing result bytes to stdout or
preparing a named destination. Named output uses an exclusively created
sibling temporary file. On POSIX it stays mode `0600` while conversion bytes
are written, flushed, and its stream closed. Only then does the tool apply
an existing regular destination's permission bits and atomically replace the
destination. New destinations retain mode `0600`. The input may equal the
output path: all input reading and conversion finish first.

Conversion, temporary creation, write, flush, close, and replacement failures
leave an existing named destination unchanged and do not create an absent
one. The tool attempts to remove unpublished temporaries on every failure;
if cleanup itself fails it reports that failure and a temporary may remain.
Successful publication does not delete a subsequently reused temporary
pathname. Symlink destinations (including dangling symlinks) and nonregular
files are rejected. Replacement changes the destination's file identity;
file ownership, ACLs, extended attributes, and hard-link relationships are
not preserved. Permissions are captured when preparing the transaction.
Callers must coordinate simultaneous writers and directory/path changes.
The guarantee is atomic visibility, not durability across power loss: the
tool does not synchronize files or directories to stable storage.

Shell redirection can truncate a file before `cot` starts; named-destination
guarantees apply only to files selected with `-o`/`--output`. Stdout receives
a complete staged conversion, but a later pipe or stream I/O failure may
leave a prefix visible. It has no atomic publication guarantee.

All I/O is binary-safe, including stdin/stdout on Windows. No terminator,
newline, progress message, or diagnostic is appended to result bytes.
Short writes are retried. Read, open, write, flush, close, replacement,
cleanup, and broken-pipe failures produce an I/O diagnostic on stderr and
exit 1. Broken pipes use the ordinary error path instead of an unhandled
signal exit. Every invocation closes its owned streams and descriptors and
releases owners, buffers, transactions, and input storage on every path.

| Exit code | Result |
| --- | --- |
| `0` | Conversion completed or help printed |
| `2` | Invalid command arguments; diagnostic on stderr |
| `1` | Codec, allocation, or I/O failure; diagnostic on stderr |

Except for help, stdout contains result bytes only and is empty on argument,
read, or conversion failure. Successful conversion emits no diagnostic.
Help does no codec or input work, but a failure writing help still exits 1.
Diagnostic wording is not stable; it must identify the failed operation and
its cause. Each invocation is independent and carries no event state into a
later command.
