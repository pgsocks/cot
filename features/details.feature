Feature: Standard details and opaque extension fidelity
  Detail conversion follows the pinned upstream whole-element mapping.

  Background:
    Given a valid XML CoT event with an otherwise empty detail

  Scenario Outline: Map each complete standard detail
    Given the detail contains exactly <element>
    When I decode the XML, encode as Protobuf, and decode again
    Then the <message> message is present with <values>
    And residual XML contains no <tag> element
    When I encode the view as XML
    Then the detail contains an equivalent <tag> element

    Examples:
      | element                                                                | message            | values                                               | tag               |
      | <contact callsign="Alpha" endpoint="a:1:tcp" altendpoints="b:2:udp"/>     | contact            | callsign Alpha, endpoint a:1:tcp, altendpoints b:2:udp | contact           |
      | <__group name="Blue" role="Team Member"/>                                | group              | name Blue and role Team Member                       | __group           |
      | <precisionlocation geopointsrc="GPS" altsrc="DTED"/>                      | precision_location | geopointsrc GPS and altsrc DTED                       | precisionlocation |
      | <status battery="0"/>                                                  | status             | battery 0                                            | status            |
      | <takv device="phone" platform="ATAK" os="Android" version="5.5.1.8"/>    | takv               | device phone, platform ATAK, os Android, version 5.5.1.8 | takv            |
      | <track speed="0" course="0"/>                                           | track              | speed 0 and course 0                                 | track             |

  Scenario: Optional contact attributes may be absent
    Given the detail contains exactly <contact callsign="Alpha"/>
    When I decode the XML
    Then contact is present with callsign "Alpha"
    And its endpoint and altendpoints are empty

  Scenario: Do not clamp the battery value
    Given the detail contains exactly <status battery="4294967295"/>
    When I decode the XML, encode as Protobuf, and decode again
    Then status is present with battery 4294967295

  Scenario Outline: Retain the whole element when typed mapping is incomplete
    Given the detail contains exactly <element>
    When I decode the XML, encode as Protobuf, and decode again
    Then the <message> pointer is null
    And residual XML contains the entire original element semantically

    Examples:
      | element                                                | message            |
      | <contact endpoint="a:1:tcp"/>                           | contact            |
      | <contact callsign="Alpha" custom="keep"/>                | contact            |
      | <__group name="Blue"/>                                  | group              |
      | <precisionlocation geopointsrc="GPS"/>                   | precision_location |
      | <status battery="not-a-number"/>                        | status             |
      | <status battery="4294967296"/>                          | status             |
      | <takv device="phone" platform="ATAK" os="Android"/>      | takv               |
      | <track speed="1" course="90"><extra/></track>            | track              |
      | <track speed="bad" course="90"/>                         | track              |
      | <track speed="1"/>                         | track              |

  Scenario: Duplicate recognized elements remain together in residual XML
    Given the detail contains <contact callsign="Alpha"/> followed by <contact callsign="Bravo"/>
    When I decode the XML, encode as Protobuf, and decode again
    Then the contact pointer is null
    And both contact elements remain in residual XML in their original order

  Scenario: Preserve unrecognized nested XML and mixed text
    Given the detail children are:
      """xml
      note <custom enabled="yes"><nested>é &amp; x</nested></custom> tail
      """
    When I decode the XML, encode as Protobuf, decode, and encode as XML
    Then the detail children retain equivalent text, attributes, and nesting
    And no standard typed message is populated

  Scenario: Typed mapping and residual XML coexist without splitting elements
    Given the detail contains <contact callsign="Alpha"/> and <custom value="keep"/>
    When I decode the XML
    Then contact is present with callsign "Alpha"
    And residual XML contains only the custom element semantically

  Scenario Outline: Residual XML takes precedence over a conflicting typed message
    Given a Protobuf event with residual XML containing <residual>
    And a present typed <message> with different values
    When I decode the Protobuf and encode its view as XML
    Then the output retains the residual <tag> element unchanged semantically
    And no second <tag> element is synthesized from the typed message

    Examples:
      | residual                                            | message            | tag               |
      | <contact callsign="Residual" custom="keep"/>          | contact            | contact           |
      | <__group name="Residual" role="Leader"/>              | group              | __group           |
      | <precisionlocation geopointsrc="R" altsrc="R"/>       | precision_location | precisionlocation |
      | <status battery="12"/>                               | status             | status            |
      | <takv device="R" platform="R" os="R" version="R"/>    | takv               | takv              |
      | <track speed="1" course="2"/>                        | track              | track             |

  Scenario: Forward both representations before applying XML precedence
    Given a Protobuf event with residual <track speed="1" course="2"/>
    And a present typed track with NaN speed and infinite course
    When I decode, encode as Protobuf, and decode again
    Then both the residual track and the typed nonfinite values remain
    When I encode the view as XML
    Then encoding returns COT_STATUS_OK
    And the XML contains only the residual track

  Scenario: Preserve ordered opaque extensions through Protobuf
    Given a caller-owned valid event with these extension entries in order:
      | id | hexadecimal bytes |
      | 42 | 000aff0d          |
      | 0  |                   |
      | 42 | 0100              |
    When I encode as Protobuf, decode, encode as Protobuf, and decode again
    Then the extension array has the same IDs and bytes in the same order

  Scenario Outline: Extensions prevent XML output without partial success
    Given a Protobuf event with one extension entry of id 42 and <bytes>
    When I decode the Protobuf
    Then decoding returns COT_STATUS_OK
    When I encode the view as XML into an empty buffer
    Then encoding returns COT_STATUS_UNSUPPORTED_CONTENT
    And the output buffer remains empty

    Examples:
      | bytes                  |
      | empty bytes            |
      | hexadecimal bytes 00ff |

  Scenario: Preserve finite track values without applying display policy
    Given the detail contains exactly <track speed="-1" course="360"/>
    When I decode the XML, encode as Protobuf, decode, and encode as XML
    Then track is represented with speed -1 and course 360
