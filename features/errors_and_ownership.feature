Feature: Explicit failures and ownership
  Codec failures leave valid empty outputs empty and release temporary storage.

  Scenario Outline: Reject malformed input
    Given <input>
    When I decode the entire input as <format> into a null owner
    Then decoding returns COT_STATUS_MALFORMED_MESSAGE
    And the output owner remains null

    Examples:
      | input                                             | format   |
      | empty bytes                                       | XML      |
      | XML with a mismatched closing tag                  | XML      |
      | XML with two complete event documents              | XML      |
      | XML with a required point attribute missing        | XML      |
      | XML with a required event attribute missing        | XML      |
      | XML with duplicate point elements                  | XML      |
      | XML with duplicate detail elements                 | XML      |
      | XML with timestamp 2024-02-30T00:00:00Z              | XML      |
      | XML with latitude text "north"                     | XML      |
      | XML with invalid UTF-8                             | XML      |
      | Protobuf with a truncated length-delimited field   | Protobuf |
      | Protobuf with an unterminated varint               | Protobuf |
      | Protobuf with invalid UTF-8 in a known string field | Protobuf |

  Scenario Outline: Reject unsupported envelopes
    Given an otherwise valid Protobuf TakMessage containing <content>
    When I decode the input as Protobuf into a null owner
    Then decoding returns COT_STATUS_UNSUPPORTED_CONTENT
    And the output owner remains null

    Examples:
      | content                         |
      | neither control nor event       |
      | a control only                  |
      | an event and a control          |
      | an event and an empty control   |

  Scenario: Malformed wire data takes precedence over unsupported control content
    Given a Protobuf TakMessage with a valid control followed by a truncated event
    When I decode the input as Protobuf into a null owner
    Then decoding returns COT_STATUS_MALFORMED_MESSAGE
    And the output owner remains null

  Scenario Outline: Do not silently discard unsupported core XML content
    Given an otherwise valid XML event with <content>
    When I decode the input as XML into a null owner
    Then decoding returns COT_STATUS_UNSUPPORTED_CONTENT
    And the output owner remains null

    Examples:
      | content                                           |
      | version "3.0"                                     |
      | an unrecognized event attribute                    |
      | an unrecognized point attribute                    |
      | a child outside point and detail                   |
      | a DTD declaring an external entity                 |
      | a timestamp before epoch zero                     |
      | a timestamp with nonzero submillisecond precision  |
      | latitude 91                                       |

  Scenario Outline: Preserve Protobuf values that XML cannot express
    Given a valid Protobuf event containing <value>
    When I decode it, encode as Protobuf, and decode again
    Then the represented value is preserved
    When I encode the view as XML into an empty buffer
    Then encoding returns COT_STATUS_UNSUPPORTED_CONTENT
    And the output buffer remains empty

    Examples:
      | value                                  |
      | a NUL byte inside the uid string        |
      | empty required core strings             |
      | an infinite latitude                    |
      | a NaN HAE                               |
      | negative CE                             |
      | a typed track with NaN speed       |
      | a typed track with infinite course           |
      | a residual fragment with unclosed tags  |

  Scenario Outline: Detect invalid caller arguments
    Given <arguments>
    When I invoke the affected public codec operation
    Then it returns COT_STATUS_INVALID_ARGUMENT
    And every valid empty output remains empty

    Examples:
      | arguments                                             |
      | an unknown format selector                            |
      | a null encode event pointer                            |
      | a null output pointer                                  |
      | null decode bytes with a positive size                 |
      | a caller text span with null data and positive size     |
      | a caller text span containing invalid UTF-8             |
      | extension count 1 with a null extension array           |
      | an extension byte span with null data and positive size |

  Scenario: Reject a nonempty encode output without destroying its data
    Given a library-owned encoded buffer containing a valid message
    When I pass that nonempty buffer as the output of another encode
    Then encoding returns COT_STATUS_INVALID_ARGUMENT
    And the original pointer, size, and bytes are unchanged
    And destroying that buffer releases its allocation once

  Scenario: Reject a nonnull decode output without replacing its owner
    Given a successfully decoded owner
    When I pass its nonnull variable as the output of another decode
    Then decoding returns COT_STATUS_INVALID_ARGUMENT
    And the original owner and its view remain valid

  Scenario: A decoded view does not borrow the input bytes
    Given I successfully decoded a valid XML event
    When I overwrite and release the original input storage
    Then repeated get calls return the same immutable view
    And encoding that view as Protobuf reproduces its represented fields
    And the owner can be destroyed after encoding

  Scenario: Encoding borrows caller data only during the call
    Given a caller-owned valid event with separately allocated text and details
    When I encode it as Protobuf successfully
    And I overwrite and release all caller-owned event storage
    Then decoding the encoded buffer reproduces the original represented fields

  Scenario Outline: Release temporary storage after allocation failure
    Given a valid <operation> request
    And a test allocator that fails each library allocation position in turn
    When I perform the <operation> into a valid empty output
    Then each injected failure returns COT_STATUS_ALLOCATION_FAILURE
    And the output remains empty on each failure
    And no temporary allocation is retained
    And a later attempt without injected failure succeeds

    Examples:
      | operation |
      | encode    |
      | decode    |

  Scenario: Cleanup is safe for null and reset objects
    Given an empty buffer and a null decoded owner
    When I destroy the buffer twice and destroy the null owner
    Then the buffer remains empty
    And getting the null owner returns null

  Scenario: Output buffer cleanup resets both fields
    Given a successfully encoded buffer
    When I destroy it twice
    Then its data pointer is null and its size is zero
    And its allocation was released once

  Scenario: Status diagnostics have static lifetime
    When I request text for every defined status and an unknown status value
    Then every result is a nonnull immutable NUL-terminated string
    And the unknown status text is "unknown status"
    And later codec calls do not invalidate any returned string

  Scenario: Independent owners and immutable reads are concurrent
    Given two independently decoded event owners
    When separate threads read and encode their views concurrently
    And multiple threads read and encode one shared view before destruction
    Then all calls return their expected results without changing either view
    And both owners can be destroyed after the threads finish
