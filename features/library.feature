Feature: Complete CoT event conversion through the public API
  Callers construct events or inspect immutable decoded views without
  depending on generated Protobuf types.

  Background:
    Given a caller-owned event with type "a-f-G-U-C", uid "alpha-1", and how "m-g"
    And its send, start, and stale times are 1704067200123, 1704067200123, and 1704067260123 milliseconds
    And its point is latitude 38, longitude -77, HAE 10, CE 2, and LE 3
    And it has no detail

  Scenario Outline: Construct and parse either supported representation
    When I encode the event as <format> into an empty buffer
    And I decode the entire result as <format> into a null owner
    Then both calls return COT_STATUS_OK
    And the decoded view has the same represented core fields
    And its detail pointer is null

    Examples:
      | format   |
      | XML      |
      | Protobuf |

  Scenario Outline: Convert in either direction
    Given a <source> message representing the event
    When I decode it and encode its view as <destination>
    And I decode that result as <destination>
    Then the represented core fields equal the original event

    Examples:
      | source   | destination |
      | XML      | Protobuf    |
      | Protobuf | XML         |

  Scenario Outline: Preserve every optional core string
    Given the event has <field> set to counted UTF-8 text "team-é"
    When I encode as XML, decode, encode as Protobuf, and decode again
    Then the decoded <field> is exactly the counted UTF-8 text "team-é"

    Examples:
      | field         |
      | access        |
      | qos           |
      | opex          |
      | caveat        |
      | releasable_to |

  Scenario Outline: Normalize the undefined access default
    Given XML for the event with <access>
    When I decode it, encode as Protobuf, decode, and encode as XML
    Then all calls return COT_STATUS_OK
    And the decoded access is empty
    And the output XML has no access attribute

    Examples:
      | access                       |
      | no access attribute          |
      | an empty access attribute    |
      | access set to "Undefined"    |

  Scenario: Count UTF-8 bytes without reading a terminator
    Given the uid points to the first 7 bytes of "alpha-1ignored"
    When I encode as Protobuf and decode the result
    Then the decoded uid is exactly 7 bytes containing "alpha-1"

  Scenario Outline: Preserve timestamps at the XML boundaries
    Given all three event timestamps are <milliseconds>
    When I encode as XML and decode the result
    Then all three timestamps are <milliseconds>
    And the XML timestamps are "<text>"

    Examples:
      | milliseconds    | text                     |
      | 0               | 1970-01-01T00:00:00.000Z |
      | 1704067200123   | 2024-01-01T00:00:00.123Z |
      | 253402300799999 | 9999-12-31T23:59:59.999Z |

  Scenario: Normalize a timezone offset without losing milliseconds
    Given XML for the event with all timestamps "2024-01-01T01:00:00.123+01:00"
    When I decode the XML
    Then all three timestamps are 1704067200123

  Scenario: Preserve the full Protobuf timestamp domain
    Given all three event timestamps are 18446744073709551615
    When I encode as Protobuf and decode the result
    Then all three timestamps are 18446744073709551615
    When I encode the decoded view as XML into an empty buffer
    Then encoding returns COT_STATUS_UNSUPPORTED_CONTENT
    And the output buffer remains empty

  Scenario: Do not impose temporal ordering or read the clock
    Given the send, start, and stale times are 1000, 2000, and 0 milliseconds
    When I encode as XML and decode the result
    Then the send, start, and stale times are 1000, 2000, and 0 milliseconds

  Scenario Outline: Preserve zero coordinates and unknown-value sentinels
    Given the point is latitude 0, longitude 0, HAE <hae>, CE <ce>, and LE <le>
    When I encode as XML, decode, encode as Protobuf, and decode again
    Then the decoded point is latitude 0, longitude 0, HAE <hae>, CE <ce>, and LE <le>

    Examples:
      | hae    | ce     | le     |
      | 0      | 0      | 0      |
      | 999999 | 999999 | 999999 |
      | -25    | 0      | 999999 |

  Scenario Outline: Preserve detail message presence
    Given the event has <presence>
    When I encode as Protobuf and decode the result
    Then the decoded detail is <result>

    Examples:
      | presence                    | result                    |
      | no detail                   | absent                    |
      | a present empty detail      | present with no children  |

  Scenario: Preserve empty XML detail presence
    Given XML for the event with a self-closing detail element
    When I decode it, encode as Protobuf, and decode again
    Then the decoded detail is present with no children

  Scenario Outline: Preserve default-valued nested messages
    Given the event has a detail with a present all-zero <message> struct
    When I encode as Protobuf and decode the result
    Then the decoded <message> pointer is nonnull
    And every scalar in that message has its default value

    Examples:
      | message            |
      | contact            |
      | group              |
      | precision_location |
      | status             |
      | takv               |
      | track              |

  Scenario: Semantic round trips do not require identical serialization
    Given XML for the event using single quotes, entities, and reordered attributes
    When I decode it, encode as Protobuf, decode, and encode as XML
    Then the output XML has the same represented values
    And no assertion depends on XML formatting or Protobuf field order
