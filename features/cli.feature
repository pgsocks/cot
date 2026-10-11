Feature: One-message command-line conversion
  The cot tool composes public decode, view, and encode operations and keeps
  diagnostics separate from result bytes.

  Scenario Outline: Display help without consuming input
    Given stdin is open and has not reached EOF
    When I run "<command>"
    Then it exits 0 without waiting for EOF
    And stdout contains usage for <scope>
    And stderr is empty
    And no input or output file is opened

    Examples:
      | command           | scope   |
      | cot --help        | cot     |
      | cot encode --help | encode  |
      | cot decode --help | decode  |

  Scenario Outline: Convert every file and standard-stream combination
    Given one valid <source> event available as "input.dat" or on stdin
    When I run "cot <command> <input> <output>" through EOF
    Then it exits 0
    And <destination> contains one complete <target> event with the same represented fields
    And stderr is empty
    And stdout contains only result bytes when selected and is otherwise empty

    Examples:
      | command | source   | target   | input     | output        | destination |
      | encode  | XML      | Protobuf |           |               | stdout      |
      | encode  | XML      | Protobuf | -         |               | stdout      |
      | encode  | XML      | Protobuf | input.dat |               | stdout      |
      | encode  | XML      | Protobuf |           | -o -          | stdout      |
      | encode  | XML      | Protobuf | -         | -o -          | stdout      |
      | encode  | XML      | Protobuf | input.dat | -o -          | stdout      |
      | encode  | XML      | Protobuf |           | -o output.dat | output.dat  |
      | encode  | XML      | Protobuf | -         | -o output.dat | output.dat  |
      | encode  | XML      | Protobuf | input.dat | -o output.dat | output.dat  |
      | decode  | Protobuf | XML      |           |               | stdout      |
      | decode  | Protobuf | XML      | -         |               | stdout      |
      | decode  | Protobuf | XML      | input.dat |               | stdout      |
      | decode  | Protobuf | XML      |           | -o -          | stdout      |
      | decode  | Protobuf | XML      | -         | -o -          | stdout      |
      | decode  | Protobuf | XML      | input.dat | -o -          | stdout      |
      | decode  | Protobuf | XML      |           | -o output.dat | output.dat  |
      | decode  | Protobuf | XML      | -         | -o output.dat | output.dat  |
      | decode  | Protobuf | XML      | input.dat | -o output.dat | output.dat  |

  Scenario: Accept the long output option before the input operand
    Given "event.xml" contains a valid XML event
    When I run "cot encode --output event.pb event.xml"
    Then it exits 0
    And "event.pb" contains the corresponding unframed Protobuf event
    And stdout and stderr are empty

  Scenario: Stop option parsing for an input filename beginning with a dash
    Given "-event.xml" contains a valid XML event
    When I run "cot encode -o result.pb -- -event.xml"
    Then it exits 0
    And "result.pb" contains the corresponding unframed Protobuf event

  Scenario: An output option consumes a filename beginning with a dash
    Given "event.xml" contains a valid XML event
    When I run "cot encode event.xml -o -result.pb"
    Then it exits 0
    And "-result.pb" contains the corresponding unframed Protobuf event

  Scenario: Delay conversion until EOF even when a complete XML event has arrived
    Given a pipe writer has sent one complete valid XML event without closing stdin
    When I start "cot encode -o result.pb"
    Then the command waits for EOF
    And stdout is empty and "result.pb" has not been created
    When the writer closes the pipe
    Then the command exits 0 with the corresponding Protobuf in "result.pb"

  Scenario: Consume trailing input instead of accepting the first XML document
    Given stdin contains a valid XML event followed by another XML event
    And "result.pb" contains the bytes "keep me"
    When I run "cot encode -o result.pb" through EOF
    Then it exits 1
    And stderr identifies a malformed message
    And stdout is empty
    And "result.pb" still contains exactly "keep me"

  Scenario: Pipe both conversion directions
    Given "event.pb" contains a valid XML-representable unframed Protobuf event
    When I run the shell pipeline "cot decode < event.pb | cot encode > roundtrip.pb"
    Then both commands exit 0
    And "roundtrip.pb" decodes to the same represented fields as "event.pb"
    And both commands have empty stderr

  Scenario Outline: Reject invalid arguments without conversion or output creation
    Given "keep.dat" contains the bytes "keep me"
    When I run "<command>"
    Then it exits 2
    And stdout is empty
    And stderr identifies the argument error
    And "keep.dat" still contains exactly "keep me"

    Examples:
      | command                                          |
      | cot                                              |
      | cot transcode                                    |
      | cot encode --unknown -o keep.dat                  |
      | cot decode -o                                    |
      | cot encode one.xml two.xml -o keep.dat            |
      | cot encode -o keep.dat --output other.dat         |
      | cot decode --output=keep.dat                      |

  Scenario Outline: Conversion failure preserves existing destinations
    Given "input.dat" contains <input>
    And "output.dat" contains the bytes "keep me"
    When I run "cot <command> input.dat -o output.dat"
    Then it exits 1
    And stderr identifies <cause>
    And stdout is empty
    And "output.dat" still contains exactly "keep me"

    Examples:
      | command | input                                       | cause                  |
      | encode  | malformed XML                               | a malformed message    |
      | decode  | truncated Protobuf                          | a malformed message    |
      | decode  | a control-only TakMessage                   | unsupported content    |
      | decode  | an event-plus-control TakMessage            | unsupported content    |
      | decode  | an event with an opaque extension           | unsupported content    |
      | decode  | an event with an XML-inexpressible timestamp | unsupported content    |

  Scenario: Conversion failure does not create a new destination
    Given "bad.xml" contains malformed XML
    And "absent.pb" does not exist
    When I run "cot encode bad.xml -o absent.pb"
    Then it exits 1
    And "absent.pb" does not exist
    And stdout is empty
    And stderr identifies a malformed message

  Scenario: Allocation failure produces no output
    Given stdin contains a valid XML event
    And the test environment injects a codec allocation failure
    And "output.pb" contains the bytes "keep me"
    When I run "cot encode -o output.pb" through EOF
    Then it exits 1
    And stderr identifies allocation failure
    And stdout is empty
    And "output.pb" still contains exactly "keep me"

  Scenario: Convert a file in place after completing the input read
    Given "event.dat" contains a valid XML event
    When I run "cot encode event.dat -o event.dat"
    Then it exits 0
    And "event.dat" contains the corresponding unframed Protobuf event

  Scenario: Preserve binary output bytes on Windows
    Given a valid XML event whose Protobuf encoding contains NUL, LF, and CR bytes
    When I run "cot encode" on Windows with that XML on stdin
    Then it exits 0
    And stdout equals the public API's counted Protobuf result byte for byte
    And no newline or terminator is appended
    And stderr is empty

  Scenario: Read binary input without text translation on Windows
    Given a valid Protobuf event containing NUL, LF, CR, and byte 1a
    When I run "cot decode" on Windows with those bytes on stdin
    Then it exits 0
    And stdout represents the complete event without early EOF or byte translation

  Scenario Outline: Report input failures without changing the destination
    Given <failure>
    And "output.dat" contains the bytes "keep me"
    When I run "cot encode input.xml -o output.dat"
    Then it exits 1
    And stderr identifies the input I/O failure
    And stdout is empty
    And "output.dat" still contains exactly "keep me"

    Examples:
      | failure                              |
      | input.xml does not exist             |
      | reading input.xml fails before EOF   |
      | closing input.xml fails after EOF    |

  Scenario Outline: Report output I/O failures
    Given "event.xml" contains a valid XML event
    And the selected output encounters <failure>
    When I run "cot encode event.xml" with that output selected
    Then it exits 1
    And stderr identifies the output I/O failure
    And any written output is only a prefix of the complete result

    Examples:
      | failure                   |
      | file creation failure     |
      | write failure             |
      | flush failure             |
      | file close failure        |
      | a closed pipe consumer    |

  Scenario: Complete short writes before declaring success
    Given stdin contains a valid XML event
    And the test output accepts only a few bytes on each write
    When I run "cot encode" through EOF
    Then it exits 0
    And stdout equals the complete public API result byte for byte
    And stderr is empty

  Scenario: Release resources after output failure
    Given a valid conversion and an output that fails during writing
    When I run the corresponding cot command
    Then it exits 1
    And all owned files, input storage, decoded owners, and buffers are released

  Scenario Outline: Named output publication failures preserve the destination
    Given "event.xml" contains a valid XML event
    And "output.pb" contains the bytes "keep me"
    And the selected output encounters <failure>
    When I run "cot encode event.xml -o output.pb"
    Then it exits 1
    And stderr identifies the output I/O failure
    And "output.pb" still contains exactly "keep me"
    And stdout is empty

    Examples:
      | failure               |
      | file creation failure |
      | write failure         |
      | flush failure         |
      | file close failure    |

  Scenario Outline: Failed atomic publication cleans up its temporary output
    Given "event.xml" contains a valid XML event
    And "output.pb" contains the bytes "keep me"
    And the selected output encounters <failure>
    When I run "cot encode event.xml -o output.pb"
    Then it exits 1
    And stderr identifies the output I/O failure
    And "output.pb" still contains exactly "keep me"
    And stdout is empty

    Examples:
      | failure             |
      | replacement failure |
      | cleanup failure     |

  Scenario: Keep an unpublished temporary output private
    Given an existing regular output with mode 0644
    When I prepare a named output transaction
    Then its temporary file has mode 0600
    And the destination is still unchanged

  Scenario: Cleanup does not remove a reused temporary pathname
    Given an existing regular output with mode 0644
    When I prepare and publish a complete named output transaction
    And another file is created at the old temporary pathname
    Then transaction cleanup preserves that other file

  Scenario: Preserve existing regular destination permissions
    Given "event.xml" contains a valid XML event
    And "output.pb" contains the bytes "keep me"
    And "output.pb" has mode 0640
    When I run "cot encode event.xml -o output.pb"
    Then it exits 0
    And "output.pb" contains the corresponding unframed Protobuf event
    And "output.pb" has mode 0640
    And no named-output temporary remains

  Scenario: New named output has restrictive permissions
    Given "event.xml" contains a valid XML event
    When I run "cot encode event.xml -o output.pb"
    Then it exits 0
    And "output.pb" has mode 0600
    And no named-output temporary remains

  Scenario Outline: Reject symlink and nonregular destinations
    Given "event.xml" contains a valid XML event
    And the output path is a <kind>
    When I run "cot encode event.xml -o output.pb"
    Then it exits 1
    And stderr identifies the output I/O failure
    And the <kind> destination remains unchanged
    And no named-output temporary remains

    Examples:
      | kind             |
      | symlink          |
      | dangling symlink |
      | directory        |
      | FIFO             |

  Scenario Outline: Failed publication does not create a new destination
    Given "event.xml" contains a valid XML event
    And the selected output encounters <failure>
    When I run "cot encode event.xml -o output.pb"
    Then it exits 1
    And "output.pb" does not exist
    And the temporary output was discarded

    Examples:
      | failure               |
      | file creation failure |
      | write failure         |
      | flush failure         |
      | file close failure    |
      | replacement failure   |

  Scenario: Treat a backslash as a literal POSIX filename byte
    Given "event.xml" contains a valid XML event
    When I run "cot encode event.xml -o literal\output.pb"
    Then it exits 0
    And "literal\output.pb" contains the corresponding unframed Protobuf event
    And no named-output temporary remains

  Scenario: Discard leaves an externally replaced temporary pathname alone
    Given an existing regular output with mode 0644
    When I prepare a named output transaction
    And another file replaces the unpublished temporary pathname
    Then publication rejects the replacement and cleanup preserves that file
    And the destination is still unchanged

  Scenario: Restrictive umask does not remove temporary owner access
    Given an existing regular output with mode 0644
    When I prepare a named output transaction with umask 0777
    Then its temporary file has mode 0600
